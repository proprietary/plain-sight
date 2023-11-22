#include "plain_sight/encoder.h"
#include "plain_sight/util.h"

#include <algorithm>
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

void draw_frame(AVFrame *const frame, const qrcodegen::QrCode &qr_code,
                const int border_size, const int scale) {
    CHECK(frame != nullptr);
    CHECK_EQ(frame->width, frame->height);
    const int computed_size = qr_code.getSize() * scale + border_size * 2;
    CHECK_EQ(frame->width, computed_size)
        << "frame->width: " << frame->width
        << " != computed_size: " << computed_size;
    CHECK_EQ(frame->format, AV_PIX_FMT_YUV420P);
    // color channels: Y
    // See: https://en.wikipedia.org/wiki/YCbCr
    for (int y = 0; y < frame->height; ++y) {
        for (int x = 0; x < frame->width; ++x) {
            const bool is_border = x < border_size || y < border_size ||
                                   x >= frame->width - border_size ||
                                   y >= frame->height - border_size;
            if (is_border) {
                frame->data[0][y * frame->linesize[0] + x] = 255; // white
            } else {
                // draw black or white pixel of the QR code
                const int true_x = (x - border_size) / scale;
                const int true_y = (y - border_size) / scale;
                frame->data[0][y * frame->linesize[0] + x] =
                    qr_code.getModule(true_x, true_y) ? 0
                                                      : 255; // black or white
            }
        }
    }
    // Color channels: U and V (Cb and Cr). These are always the same value
    // because the QR code is grayscale. Therefore it is easier to just set them
    // all to 128 in a contiguous, branch-free loop.
    for (int y = 0; y < frame->height; ++y) {
        for (int x = 0; x < frame->width; ++x) {
            frame->data[1][y * frame->linesize[1] + x] = 128;
            frame->data[2][y * frame->linesize[2] + x] = 128;
        }
    }
}

void encoder_t::encode(std::unique_ptr<video_output_t> destination) {
    std::unique_ptr<AVFormatContext, decltype(&avformat_free_context)>
        format_context{avformat_alloc_context(), &avformat_free_context};
    CHECK(format_context) << "Failed to allocate AVFormatContext";
    format_context->oformat =
        av_guess_format(video_format_.c_str(), nullptr, nullptr);
    if (format_context->oformat == nullptr) {
        LOG(FATAL) << "No video format named " << std::quoted(video_format_);
    }
    format_context->pb = destination->get_io_context();
    CHECK(format_context->pb != nullptr);
    AVStream *const stream = avformat_new_stream(format_context.get(), nullptr);
    CHECK(stream != nullptr);
    const AVCodec *const codec = avcodec_find_encoder(
        format_context->oformat->video_codec); // AV_CODEC_ID_H264
    CHECK(codec != nullptr) << "Codec for " << std::quoted(video_format_)
                            << " not found on host system";
    std::unique_ptr<AVCodecContext, avcodec_context_deleter_t> codec_context{
        avcodec_alloc_context3(codec), avcodec_context_deleter_t{}};
    CHECK(codec_context) << "Failed to allocate AVCodecContext";
    int err =
        avcodec_parameters_to_context(codec_context.get(), stream->codecpar);
    if (err < 0) {
        LOG(FATAL) << "Could not initialize codec parameters:"
                   << libav_error(err);
    }
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
    // initialize codec
    err = avcodec_open2(codec_context.get(), codec, nullptr);
    if (err < 0) {
        LOG(FATAL) << "Could not open codec:" << libav_error(err);
    }
    stream->codecpar->extradata = codec_context->extradata;
    stream->codecpar->extradata_size = codec_context->extradata_size;
    // write file header
    err = avformat_write_header(format_context.get(), nullptr);
    if (err < 0) {
        LOG(FATAL) << "Could not write header:" << libav_error(err);
    }
    // allocate frame
    std::unique_ptr<AVFrame, av_frame_deleter_t> frame{av_frame_alloc(),
                                                       av_frame_deleter_t{}};
    CHECK(frame) << "Failed to allocate AVFrame";
    // set frame parameters
    frame->width = codec_context->width;
    frame->height = codec_context->height;
    frame->format = codec_context->pix_fmt;
    // allocate framebuffer
    int sz = av_image_alloc(frame->data, frame->linesize, frame->width,
                            frame->height, codec_context->pix_fmt, 1);
    CHECK_GE(sz, 0) << "Failed to allocate framebuffer";
    // allocate packet
    std::unique_ptr<AVPacket, av_packet_deleter_t> packet{
        av_packet_alloc(), av_packet_deleter_t{}};
    const auto receive_packet = [](AVCodecContext *const codec_context,
                                   AVPacket *const packet,
                                   AVFormatContext *const format_context) {
        int err;
        while (true) {
            err = avcodec_receive_packet(codec_context, packet);
            if (err == AVERROR(EAGAIN) || err == AVERROR_EOF) {
                break;
            } else if (err < 0) {
                LOG(FATAL) << "Could not receive packet:" << libav_error(err);
            }
            // write packet
            err = av_interleaved_write_frame(format_context, packet);
            if (err < 0) {
                LOG(FATAL) << "Could not write frame:" << libav_error(err);
            }
            av_packet_unref(packet);
        }
    };
    int frame_counter = 0;
    for (const auto &qr_code : *qr_codes_) {
        // encode frame
        draw_frame(frame.get(), qr_code, border_size_, scale_);
        frame->pts = frame_counter++;
        err = avcodec_send_frame(codec_context.get(), frame.get());
        if (err < 0) {
            LOG(FATAL) << "Could not send frame:" << libav_error(err);
        }
        receive_packet(codec_context.get(), packet.get(), format_context.get());
    }
    // Flush encoder with null flush packet, signaling end of the stream. If the
    // encoder still has packets buffered, it will return them.
    err = avcodec_send_frame(codec_context.get(), nullptr);
    if (err < 0) {
        LOG(FATAL) << "Could not send frame:" << libav_error(err);
    }
    receive_packet(codec_context.get(), packet.get(), format_context.get());
    // Write trailer
    err = av_write_trailer(format_context.get());
    if (err < 0) {
        LOG(FATAL) << "Could not write trailer:" << libav_error(err);
    }
}

auto encoder_t::builder_t::build() const -> encoder_t {
    CHECK(qr_codes_.operator bool());
    CHECK(!video_format_.empty());
    CHECK_GT(scale_, 0);
    CHECK_GT(border_size_, 0);
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
    int err = avio_open(&io_context_, filename.c_str(), AVIO_FLAG_WRITE);
    if (err < 0) {
        LOG(FATAL) << "avio_open failed: " << libav_error(err);
    }
}

file_video_output_t::~file_video_output_t() noexcept {
    if (io_context_ != nullptr)
        avio_close(io_context_);
}

auto file_video_output_t::get_io_context() const noexcept -> AVIOContext * {
    return io_context_;
}

in_memory_video_output_t::in_memory_video_output_t(
    std::vector<std::uint8_t> &sink)
    : sink_{sink} {
    buffer_ = static_cast<std::uint8_t *>(av_malloc(buffer_size_));
    CHECK(buffer_ != nullptr);
    constexpr bool write_flag = 1;
    io_context_ =
        avio_alloc_context(buffer_, buffer_size_, write_flag, this, nullptr,
                           &in_memory_video_output_t::write_packet, nullptr);
    CHECK(io_context_ != nullptr);
}

in_memory_video_output_t::~in_memory_video_output_t() noexcept {
    if (buffer_ != nullptr)
        av_free(buffer_);
    if (io_context_ != nullptr)
        avio_context_free(&io_context_);
}

auto in_memory_video_output_t::get_video_contents() noexcept
    -> std::span<std::uint8_t> {
    std::span<std::uint8_t> s(sink_.data(), sink_.size());
    return s;
}

auto in_memory_video_output_t::get_io_context() const noexcept
    -> AVIOContext * {
    return io_context_;
}

int in_memory_video_output_t::write_packet(void *opaque, std::uint8_t *buf,
                                           int buf_size) noexcept {
    // A function for writing the buffer contents, may be NULL. The function may
    // not change the input buffers content.
    CHECK(buf != nullptr);
    CHECK(opaque != nullptr);
    auto *const self = static_cast<in_memory_video_output_t *>(opaque);
    CHECK(self->buffer_ != nullptr);
    CHECK(self->io_context_ != nullptr);
    std::copy_n(buf, buf_size, std::back_inserter(self->sink_));
    return buf_size;
}

} // namespace net_zelcon::plain_sight