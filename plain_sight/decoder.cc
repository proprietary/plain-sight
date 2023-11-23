#include "plain_sight/decoder.h"
#include "plain_sight/qr_codes.h"
#include "plain_sight/util.h"

#include <fmt/core.h>
#include <glog/logging.h>
#include <limits>
#include <memory>
#include <stdexcept>
#include <tuple>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace net_zelcon::plain_sight {

namespace {

struct image_buf_t {
    std::vector<std::uint8_t> buf;
    int width;
    int height;
};

auto get_frame_pixels(image_buf_t &img, const AVFrame *frame) -> void {
    constexpr auto destination_format = AV_PIX_FMT_BGR8;
    std::unique_ptr<SwsContext, decltype(&sws_freeContext)> sws_ctx{
        sws_getContext(frame->width, frame->height,
                       static_cast<AVPixelFormat>(frame->format), frame->width,
                       frame->height, destination_format, SWS_BILINEAR, nullptr,
                       nullptr, nullptr),
        &sws_freeContext};
    if (!sws_ctx) {
        throw std::runtime_error{"Could not initialize sws context"};
    }
    libav_2_star_ptr_t<AVFrame, av_frame_free> grayscale_frame{
        av_frame_alloc(), av_frame_free};
    CHECK(grayscale_frame) << "Could not allocate bgr frame";
    int err =
        av_image_alloc(grayscale_frame->data, grayscale_frame->linesize,
                       frame->width, frame->height, destination_format, 1);
    CHECK(err >= 0) << "Could not allocate raw picture buffer";

    err = sws_scale_frame(sws_ctx.get(), grayscale_frame.get(), frame);
    if (err < 0) {
        LOG(ERROR) << "Could not scale frame:" << libav_error(err);
        throw std::runtime_error{"Could not scale frame"};
    }

    img.height = grayscale_frame->height;
    img.width = grayscale_frame->linesize[0];
    img.buf.clear();
    std::copy(grayscale_frame->data[0],
              grayscale_frame->data[0] +
                  grayscale_frame->linesize[0] * img.height,
              std::back_inserter(img.buf));
}

auto find_video_stream(AVFormatContext *const format_context)
    -> std::tuple<const AVCodec *, const AVCodecParameters *, int> {
    const AVCodec *decoder = nullptr;
    const int video_stream_idx = av_find_best_stream(
        format_context, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (video_stream_idx < 0) {
        const auto error_message = libav_error(video_stream_idx);
        switch (video_stream_idx) {
        case AVERROR_STREAM_NOT_FOUND:
            LOG(ERROR) << "Could not find video stream in input file: "
                       << error_message;
            throw std::runtime_error{
                fmt::format("Could not find video stream in input file: {}",
                            error_message)};
        case AVERROR_DECODER_NOT_FOUND:
            LOG(ERROR) << "Could not find decoder for video stream: "
                       << error_message;
            throw std::runtime_error{fmt::format(
                "Could not find decoder for video stream: {}", error_message)};
        default:
            LOG(ERROR) << "Could not find video stream: " << error_message;
            throw std::runtime_error{
                fmt::format("Could not find video stream: {}", error_message)};
        }
    }
    const AVCodecParameters *codec_params =
        format_context->streams[video_stream_idx]->codecpar;
    return {decoder, codec_params, video_stream_idx};
}

} // namespace

in_memory_video_input_t::in_memory_video_input_t(std::span<std::uint8_t> video)
    : video_{video},
      format_context_{avformat_alloc_context(), &avformat_free_context} {
    buffer_ = static_cast<std::uint8_t *>(av_malloc(buffer_size_));
    CHECK(buffer_ != nullptr) << "Could not allocate libav buffer";
    io_context_ = avio_alloc_context(buffer_, buffer_size_, 0, this,
                                     &read_packet, nullptr, &seek);
    CHECK(io_context_ != nullptr) << "Could not allocate libav io context";
    format_context_->pb = io_context_;
}

in_memory_video_input_t::~in_memory_video_input_t() noexcept {
    if (buffer_)
        av_free(buffer_);
    if (io_context_)
        avio_context_free(&io_context_);
}

int in_memory_video_input_t::read_packet(void *opaque, std::uint8_t *buf,
                                         int buf_size) {
    CHECK(opaque != nullptr) << "Opaque pointer is null. It should point to a "
                                "`in_memory_video_input_t`.";
    auto *const self = static_cast<in_memory_video_input_t *>(opaque);
    const auto bytes_left = self->video_.size() - self->offset_;
    if (bytes_left <= 0) {
        return AVERROR_EOF;
    }
    const auto bytes_to_read =
        std::min(bytes_left, static_cast<std::size_t>(buf_size));
    std::copy(self->video_.begin() + self->offset_,
              self->video_.begin() + self->offset_ + bytes_to_read, buf);
    self->offset_ += bytes_to_read;
    return bytes_to_read;
}

int64_t in_memory_video_input_t::seek(void *opaque, int64_t offset,
                                      int whence) {
    CHECK(opaque != nullptr) << "Opaque pointer is null. It should point to a "
                                "`in_memory_video_input_t`.";
    auto *const self = static_cast<in_memory_video_input_t *>(opaque);
    switch (whence) {
    case SEEK_SET:
        self->offset_ = offset;
        break;
    case SEEK_CUR:
        self->offset_ += offset;
        break;
    case SEEK_END:
        self->offset_ = self->video_.size() + offset;
        break;
    case AVSEEK_SIZE:
        return self->video_.size();
    default:
        LOG(ERROR) << "Invalid whence value: " << whence;
        return -1;
    }
    return 0;
}

auto in_memory_video_input_t::format_context() const -> AVFormatContext * {
    CHECK(format_context_) << "Attempted null pointer access on "
                              "`format_context_`. This should never happen.";
    return format_context_.get();
}

file_video_input_t::file_video_input_t(const std::filesystem::path &video_path)
    : video_path_{video_path}, format_context_{nullptr, &avformat_close_input} {
    CHECK(!video_path.empty()) << "Video path is empty";
    if (!std::filesystem::exists(video_path)) {
        const auto error_message = fmt::format(
            "Video file \"{}\" does not exist", video_path.string());
        LOG(ERROR) << error_message;
        throw std::runtime_error{error_message};
    }
    AVFormatContext *p = nullptr;
    int err = avformat_open_input(&p, video_path.c_str(), nullptr, nullptr);
    if (err < 0) {
        LOG(ERROR) << "Could not open input file: " << libav_error(err);
        throw std::runtime_error{libav_error(err)};
    }
    CHECK(p != nullptr) << "Could not allocate AVFormatContext";
    format_context_.reset(p);
}

file_video_input_t::~file_video_input_t() noexcept {}

auto file_video_input_t::format_context() const -> AVFormatContext * {
    CHECK(format_context_)
        << "Attempted null pointer access on `format_context_`. "
           "This should never happen.";
    return format_context_.get();
}

void decoder_t::decode(std::vector<std::uint8_t> &dst,
                       std::unique_ptr<video_input_t> src) {
    int err = 0;
    CHECK(src) << "Video input IO context must be usable";
    AVFormatContext *format_context = src->format_context();
    err = avformat_find_stream_info(format_context, nullptr);
    if (err < 0) {
        LOG(ERROR) << "Could not find stream info:" << libav_error(err);
        throw std::runtime_error{
            fmt::format("Could not find stream info: {}", libav_error(err))};
    }
    // find video stream index
    const auto [codec, codec_params, video_stream_idx] =
        find_video_stream(format_context);
    // allocate codec context
    libav_2_star_ptr_t<AVCodecContext, avcodec_free_context> codec_context{
        avcodec_alloc_context3(codec), avcodec_free_context};
    CHECK(codec_context) << "Could not allocate codec context";
    err = avcodec_parameters_to_context(codec_context.get(), codec_params);
    if (err < 0) {
        LOG(ERROR) << "Could not copy codec params to codec context:"
                   << libav_error(err);
        throw std::runtime_error{
            fmt::format("Could not copy codec params to codec context: {}",
                        libav_error(err))};
    }
    err = avcodec_open2(codec_context.get(), codec, nullptr);
    if (err < 0) {
        LOG(ERROR) << "Could not open codec:" << libav_error(err);
        throw std::runtime_error{
            fmt::format("Could not open codec: {}", libav_error(err))};
    }
    libav_2_star_ptr_t<AVFrame, av_frame_free> frame{av_frame_alloc(),
                                                     av_frame_free};
    CHECK(frame) << "Could not allocate frame";
    libav_2_star_ptr_t<AVPacket, av_packet_free> packet{av_packet_alloc(),
                                                 av_packet_free};
    CHECK(packet) << "Could not allocate packet";
    std::unique_ptr<qr_code_decoder_t> qr_code_decoder{nullptr};
    image_buf_t img{};
    int frame_counter = 0;
    err = 0;
    while (err >= 0) {
        err = av_read_frame(format_context, packet.get());
        if (err >= 0 &&
            packet->stream_index != video_stream_idx) {
            continue;
        }
        if (err < 0) {
            // send flush packet
            err = avcodec_send_packet(codec_context.get(), nullptr);
        } else {
            if (packet->pts ==
                AV_NOPTS_VALUE) { // no timestamp value available for this frame
                packet->pts = packet->dts = frame_counter;
            }
            err = avcodec_send_packet(codec_context.get(), packet.get());
        }
        av_packet_unref(packet.get());
        if (err < 0) {
            LOG(ERROR) << "Error sending packet to decoder:"
                       << libav_error(err);
            throw std::runtime_error{fmt::format(
                "Error sending packet to decoder: {}", libav_error(err))};
        }
        while (err >= 0) {
            // process decoded frame
            err = avcodec_receive_frame(codec_context.get(), frame.get());
            if (err == AVERROR_EOF) {
                return;
            } else if (err == AVERROR(EAGAIN)) {
                DLOG(INFO) << "EAGAIN";
                err = 0;
                break;
            } else if (err < 0) {
                LOG(ERROR) << "Error during decoding:" << libav_error(err);
                throw std::runtime_error{
                    fmt::format("Error during decoding: {}", libav_error(err))};
            } else if (err >= 0) {
                DLOG(INFO) << "Received frame " << frame_counter
                           << " from decoder";
                get_frame_pixels(img, frame.get());
                av_frame_unref(frame.get());
                if (!qr_code_decoder) {
                    qr_code_decoder = std::make_unique<qr_code_decoder_t>(
                        img.width, img.height);
                }
                qr_code_decoder->decode(dst, img.buf);
            } else {
                LOG(FATAL) << "should be unreachable!";
            }
            frame_counter++;
        }
    }
}

template <typename OutputIt>
    requires std::output_iterator<OutputIt, std::uint8_t>
void copy_img_buf(OutputIt dst, const AVFrame *frame) {
    const int required_size =
        ::av_image_get_buffer_size(static_cast<AVPixelFormat>(frame->format),
                                   frame->width, frame->height, 1);
    CHECK_GT(required_size, 0)
        << "Could not compute required size for image buffer";
    auto dst_start = dst;
    std::fill_n(dst, required_size, 0);
    auto dst_end = dst + required_size;
    DCHECK(dst_end - dst_start == required_size);
    int err = ::av_image_copy_to_buffer(
        dst_start, required_size, frame->data, frame->linesize,
        static_cast<AVPixelFormat>(frame->format), frame->width, frame->height,
        1);
    CHECK_GE(err, 0) << "Could not copy image buffer: " << libav_error(err);
}

} // namespace net_zelcon::plain_sight