#include "plain_sight/encoder.h"
#include "plain_sight/util.h"

#include <algorithm>
#include <fmt/core.h>
#include <functional>
#include <glog/logging.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace net_zelcon::plain_sight {

namespace {

void write_frame(AVFormatContext *fmt_ctx, AVCodecContext *enc_ctx,
                 AVFrame *frame, AVPacket *pkt) {
    int err = avcodec_send_frame(enc_ctx, frame);
    if (err < 0) {
        LOG(FATAL) << "Could not send frame: " << libav_error(err);
    }
    while (err >= 0) {
        err = avcodec_receive_packet(enc_ctx, pkt);
        if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
            return;
        } else if (err < 0 && err != AVERROR_EOF) {
            LOG(FATAL) << "Could not receive packet: " << libav_error(err);
        } else if (err >= 0) {
            // rescale output packet timestamp values from codec to stream
            // timebase
            av_packet_rescale_ts(pkt, enc_ctx->time_base,
                                 fmt_ctx->streams[0]->time_base);
            pkt->stream_index = fmt_ctx->streams[0]->index;
            // write packet
            err = av_interleaved_write_frame(fmt_ctx, pkt);
            if (err < 0) {
                LOG(FATAL) << "Could not write frame: " << libav_error(err);
            }
        }
        av_packet_unref(pkt);
    }
}

void prepare_frame(AVFrame *dst, const AVCodecContext *const codec_context) {
    dst->width = codec_context->width;
    dst->height = codec_context->height;
    dst->format = codec_context->pix_fmt;
    int err = av_frame_get_buffer(dst, 1);
    CHECK(err >= 0) << "Could not allocate frame buffers: " << libav_error(err);
}

} // namespace

void draw_QR_code(AVFrame *dst, const qrcodegen::QrCode &qr_code,
                  const int border_size, const int scale) {
    // TODO: Parallelize this. It is embarassingly parallelizable.
    CHECK(dst != nullptr);
    CHECK_EQ(dst->width, dst->height);
    const int computed_size = qr_code.getSize() * scale + border_size * 2;
    CHECK_EQ(dst->width, computed_size)
        << "dst->width: " << dst->width
        << " != computed_size: " << computed_size;
    CHECK_EQ(dst->format, AV_PIX_FMT_YUV420P);
    // color channels: Y
    // See: https://en.wikipedia.org/wiki/YCbCr
    for (int y = 0; y < dst->height; ++y) {
        for (int x = 0; x < dst->width; ++x) {
            const bool is_border = x < border_size || y < border_size ||
                                   x >= dst->width - border_size ||
                                   y >= dst->height - border_size;
            if (is_border) {
                dst->data[0][y * dst->linesize[0] + x] = 255; // white
            } else {
                // draw black or white pixel of the QR code
                const int true_x = (x - border_size) / scale;
                const int true_y = (y - border_size) / scale;
                dst->data[0][y * dst->linesize[0] + x] =
                    qr_code.getModule(true_x, true_y) ? 0
                                                      : 255; // black or white
            }
        }
    }
    // Color channels: U and V (Cb and Cr). These are always the same value
    // because the QR code is grayscale. Therefore it is easier to just set them
    // all to 128 in a contiguous, branch-free loop.
    for (int y = 0; y < dst->height / 2; ++y) {
        for (int x = 0; x < dst->width / 2; ++x) {
            dst->data[1][y * dst->linesize[1] + x] = 128;
            dst->data[2][y * dst->linesize[2] + x] = 128;
        }
    }
}

void draw_frame(AVCodecContext *const codec_context, AVFrame *const frame,
                const qrcodegen::QrCode &qr_code, const int border_size,
                const int scale) {
    CHECK(frame != nullptr);
    CHECK_EQ(frame->width, frame->height);
    const int computed_size = qr_code.getSize() * scale + border_size * 2;
    CHECK_EQ(frame->width, computed_size)
        << "frame->width: " << frame->width
        << " != computed_size: " << computed_size;
    if (codec_context->pix_fmt != AV_PIX_FMT_YUV420P) {
        // Convert from YUV420P because `draw_QR_code` only writes a YUV420P
        // image.
        libav_ptr_t<SwsContext, sws_freeContext> sws_context{
            sws_getContext(frame->width, frame->height, AV_PIX_FMT_YUV420P,
                           frame->width, frame->height, codec_context->pix_fmt,
                           SWS_BILINEAR, nullptr, nullptr, nullptr),
            sws_freeContext};
        CHECK(sws_context) << "Failed to allocate SwsContext";
        libav_ptr_t<AVFrame, av_frame_free> temp_frame{av_frame_alloc(),
                                                       av_frame_free};
        CHECK(temp_frame) << "Failed to allocate AVFrame";
        prepare_frame(temp_frame.get(), codec_context);
        DCHECK_EQ(temp_frame->width, frame->width);
        DCHECK_EQ(temp_frame->height, frame->height);
        DCHECK_EQ(static_cast<AVPixelFormat>(temp_frame->format),
                  AV_PIX_FMT_YUV420P);
        draw_QR_code(temp_frame.get(), qr_code, border_size, scale);
        sws_scale(sws_context.get(), temp_frame->data, temp_frame->linesize, 0,
                  frame->height, frame->data, frame->linesize);
        // TODO: use some associated class object to obviate some of these
        // allocations above
    } else {
        draw_QR_code(frame, qr_code, border_size, scale);
    }
}

void encoder_t::encode(std::unique_ptr<video_output_t> destination) {
    AVFormatContext *format_context = destination->format_context();
    CHECK(format_context) << "Failed to allocate AVFormatContext";
    format_context->oformat =
        av_guess_format(video_format_.c_str(), nullptr, nullptr);
    if (format_context->oformat == nullptr) {
        LOG(FATAL) << "No video format named " << std::quoted(video_format_);
    }
    AVStream *const stream = avformat_new_stream(format_context, nullptr);
    CHECK(stream != nullptr);
    CHECK_EQ(stream, format_context->streams[0]);
    const AVCodec *const codec = avcodec_find_encoder(
        format_context->oformat->video_codec); // probably is AV_CODEC_ID_H264
    CHECK(codec != nullptr) << "Codec for " << std::quoted(video_format_)
                            << " not found on host system";
    libav_ptr_t<AVCodecContext, avcodec_free_context> codec_context{
        avcodec_alloc_context3(codec), avcodec_free_context};
    CHECK(codec_context) << "Failed to allocate AVCodecContext";
    if (format_context->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    codec_context->codec_id = format_context->oformat->video_codec;
    codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
    const auto size = calculate_dimensions();
    codec_context->width = size;
    codec_context->height = size;
    // frame rate
    codec_context->time_base = AVRational{1, fps_};
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    codec_context->gop_size = gop_size_;
    codec_context->bit_rate = bitrate_;
    //  initialize codec
    int err = avcodec_open2(codec_context.get(), codec, nullptr);
    if (err < 0) {
        LOG(FATAL) << "Could not open codec:" << libav_error(err);
    }
    err =
        avcodec_parameters_from_context(stream->codecpar, codec_context.get());
    if (err < 0) {
        LOG(FATAL) << "Could not initialize codec parameters:"
                   << libav_error(err);
    }
    //  write file header
    err = avformat_write_header(format_context, nullptr);
    if (err < 0) {
        LOG(FATAL) << "Could not write header:" << libav_error(err);
    }
    // allocate frame
    libav_ptr_t<AVFrame, av_frame_free> frame{av_frame_alloc(), av_frame_free};
    CHECK(frame) << "Failed to allocate AVFrame";
    prepare_frame(frame.get(), codec_context.get());
    // allocate packet
    libav_ptr_t<AVPacket, av_packet_free> packet{av_packet_alloc(),
                                                 av_packet_free};
    int frame_counter = 1;
    for (const auto &qr_code : *qr_codes_) {
        // encode frame
        draw_frame(codec_context.get(), frame.get(), qr_code, border_size_,
                   scale_);
        frame->pts = frame_counter;
        DLOG(INFO) << "Sending frame " << frame_counter << " / "
                   << qr_codes_->size() << " to encoder";
        write_frame(format_context, codec_context.get(), frame.get(),
                    packet.get());
        frame_counter++;
    }
    CHECK_EQ(static_cast<size_t>(frame_counter), qr_codes_->size() + 1);
    // Flush encoder with null flush packet, signaling end of the stream. If the
    // encoder still has packets buffered, it will return them.
    write_frame(format_context, codec_context.get(), nullptr, packet.get());
    //  Write trailer
    err = av_write_trailer(format_context);
    if (err < 0) {
        LOG(FATAL) << "Could not write trailer:" << libav_error(err);
    }
}

auto encoder_t::builder_t::build() const -> encoder_t {
    CHECK(qr_codes_.operator bool());
    CHECK(!video_format_.empty());
    CHECK_GT(scale_, 0UL);
    CHECK_GT(border_size_, 0UL);
    CHECK_GT(fps_, 0);
    return encoder_t{std::move(qr_codes_), video_format_, scale_, border_size_,
                     fps_};
}

auto encoder_t::builder_t::video_format() const noexcept -> std::string_view {
    return video_format_;
}

auto encoder_t::builder_t::qr_codes() const noexcept
    -> std::shared_ptr<std::vector<qrcodegen::QrCode>> {
    return qr_codes_;
}

auto encoder_t::builder_t::set_qr_codes(
    std::shared_ptr<std::vector<qrcodegen::QrCode>> qr_codes) noexcept
    -> encoder_t::builder_t & {
    qr_codes_ = std::move(qr_codes);
    return *this;
}

auto encoder_t::calculate_dimensions() const -> size_t {
    CHECK(qr_codes_);
    CHECK(!qr_codes_->empty());
    const auto &first_qr_code = qr_codes_->front();
    CHECK(std::all_of(qr_codes_->begin(), qr_codes_->end(),
                      [&first_qr_code](const auto &qr_code) {
                          return first_qr_code.getSize() == qr_code.getSize();
                      }))
        << "All QR codes must be the same size";
    const int computed_size =
        first_qr_code.getSize() * scale_ + border_size_ * 2;
    return computed_size;
}

auto encoder_t::builder_t::set_video_format(
    std::string_view video_format) noexcept -> builder_t & {
    video_format_ = video_format;
    return *this;
}

auto encoder_t::builder_t::set_border_size(const size_t border_size) noexcept
    -> builder_t & {
    border_size_ = border_size;
    return *this;
}

auto encoder_t::builder_t::set_scale(const size_t scale) noexcept
    -> builder_t & {
    scale_ = scale;
    return *this;
}

auto encoder_t::builder_t::set_fps(const int fps) noexcept -> builder_t & {
    fps_ = fps;
    return *this;
}

file_video_output_t::file_video_output_t(const std::filesystem::path &filename)
    : filename_{filename} {
    CHECK(!filename.empty());
    CHECK((std::filesystem::perms::owner_write &
           std::filesystem::status(filename.parent_path()).permissions()) ==
          std::filesystem::perms::owner_write)
        << "Cannot write to " << std::quoted(filename.parent_path().string());
    // deduce oformat from filename; may change this if accurate filenames are
    // not available
    int err = avformat_alloc_output_context2(&format_context_, nullptr, nullptr,
                                             filename_.c_str());
    if (err < 0) {
        LOG(ERROR) << "avformat_alloc_output_context2 failed: "
                   << libav_error(err);
        throw std::runtime_error{fmt::format("avformat_alloc_output_context2 "
                                             "failed: {}",
                                             libav_error(err))};
    }
    CHECK(format_context_ != nullptr);
    err = avio_open(&format_context_->pb, filename.c_str(), AVIO_FLAG_WRITE);
    if (err < 0) {
        LOG(FATAL) << "avio_open failed: " << libav_error(err);
    }
}

file_video_output_t::~file_video_output_t() noexcept {
    if (io_context_ != nullptr)
        avio_closep(&format_context_->pb);
    avformat_free_context(format_context_);
}

auto file_video_output_t::format_context() -> AVFormatContext * {
    return format_context_;
}

file_video_output_t::file_video_output_t(file_video_output_t &&src) noexcept {
    this->filename_ = std::move(src.filename_);
    this->io_context_ = src.io_context_;
    src.io_context_ = nullptr;
    this->format_context_ = src.format_context_;
    src.format_context_ = nullptr;
}

file_video_output_t &
file_video_output_t::operator=(file_video_output_t &&src) noexcept {
    file_video_output_t temp{std::move(src)};
    std::swap(*this, temp);
    return *this;
}

in_memory_video_output_t::in_memory_video_output_t(
    std::vector<std::uint8_t> &sink)
    : sink_{sink} {
    std::uint8_t *buffer = static_cast<std::uint8_t *>(av_malloc(buffer_size_));
    CHECK(buffer != nullptr) << "Failed to allocate AVIO buffer";
    io_context_ = avio_alloc_context(buffer, buffer_size_, AVIO_FLAG_WRITE,
                                     this, nullptr, &write_packet, &seek);
    CHECK(io_context_ != nullptr);
    format_context_ = avformat_alloc_context();
    format_context_->pb = io_context_;
    format_context_->flags |= AVFMT_FLAG_CUSTOM_IO;
}

in_memory_video_output_t::~in_memory_video_output_t() noexcept {
    av_free(io_context_->buffer);
    avio_context_free(&io_context_);
    avformat_free_context(format_context_);
}

auto in_memory_video_output_t::format_context() -> AVFormatContext * {
    return format_context_;
}

auto in_memory_video_output_t::get_video_contents() noexcept
    -> std::span<std::uint8_t> {
    std::span<std::uint8_t> s(sink_.data(), sink_.size());
    return s;
}

int in_memory_video_output_t::write_packet(void *opaque, std::uint8_t *buf,
                                           int buf_size) noexcept {
    // A function for writing the buffer contents, may be NULL. The function may
    // not change the input buffers content.
    CHECK(buf != nullptr);
    CHECK(opaque != nullptr);
    auto *const self = static_cast<in_memory_video_output_t *>(opaque);
    if (buf_size <= 0) {
        return AVERROR_EOF;
    }
    DLOG(INFO) << "writing packet at offset " << self->offset_ << " size "
               << buf_size << " in in_memory_video_output_t";
    if (self->offset_ + buf_size > static_cast<int64_t>(self->sink_.size())) {
        self->sink_.resize(self->offset_ + buf_size, 0);
    }
    std::copy_n(buf, buf_size, self->sink_.begin() + self->offset_);
    self->offset_ += buf_size;
    CHECK_LE(self->offset_, static_cast<int64_t>(self->sink_.size()));
    return buf_size;
}

std::int64_t in_memory_video_output_t::seek(void *opaque, std::int64_t offset,
                                            int whence) noexcept {
    DLOG(INFO) << "Seeking to offset " << offset << " whence " << whence
               << " in in_memory_video_output_t";
    // A function for seeking to specified byte position, may be NULL.
    CHECK(opaque != nullptr);
    auto *const self = static_cast<in_memory_video_output_t *>(opaque);
    if (offset < 0) {
        LOG(ERROR) << "Invalid offset: " << offset;
        return AVERROR(EINVAL);
    }
    switch (whence) {
    case SEEK_SET:
        self->offset_ = offset;
        break;
    case SEEK_CUR:
    case SEEK_END:
    case AVSEEK_SIZE:
    default:
        LOG(ERROR) << "Invalid whence: " << whence;
        return AVERROR(ENOSYS);
    }
    return self->offset_;
}

} // namespace net_zelcon::plain_sight