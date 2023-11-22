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
    AVFrame *grayscale_frame = av_frame_alloc();
    if (grayscale_frame == nullptr) {
        LOG(ERROR) << "Could not allocate bgr frame";
        throw std::runtime_error{"Could not allocate bgr frame"};
    }
    if (av_image_alloc(grayscale_frame->data, grayscale_frame->linesize,
                       frame->width, frame->height, destination_format,
                       1) < 0) {
        LOG(ERROR) << "Could not allocate raw picture buffer";
        throw std::bad_alloc{};
    }

    auto err = sws_scale_frame(sws_ctx.get(), grayscale_frame, frame);
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

    // av_freep(&grayscale_frame->data[0]);
    av_frame_free(&grayscale_frame);
}

auto find_video_stream(const AVFormatContext *const format_context)
    -> std::tuple<const AVCodec *, const AVCodecParameters *, size_t> {
    const AVCodec *codec = nullptr;
    const AVCodecParameters *codec_params = nullptr;
    size_t video_stream_idx = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < format_context->nb_streams; ++i) {
        codec_params = format_context->streams[i]->codecpar;
        codec = avcodec_find_decoder(codec_params->codec_id);
        if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx == std::numeric_limits<size_t>::max()) {
        LOG(ERROR) << "Could not find video stream";
        throw std::runtime_error{"Could not find video stream"};
    }
    return {codec, codec_params, video_stream_idx};
}

} // namespace

in_memory_video_input_t::in_memory_video_input_t(std::span<std::uint8_t> video)
    : video_{video} {
    buffer_ = static_cast<std::uint8_t *>(av_malloc(buffer_size_));
    CHECK(buffer_ != nullptr) << "Could not allocate libav buffer";
    io_context_ = avio_alloc_context(buffer_, buffer_size_, 0, this,
                                     &read_packet, nullptr, &seek);
    CHECK(io_context_ != nullptr) << "Could not allocate libav io context";
}

in_memory_video_input_t::~in_memory_video_input_t() noexcept {
    if (buffer_)
        av_free(buffer_);
    if (io_context_)
        avio_context_free(&io_context_);
}

auto in_memory_video_input_t::get_io_context() const noexcept -> AVIOContext * {
    return io_context_;
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
        self->offset_ = self->video_.size();
        break;
    case AVSEEK_SIZE:
        return self->video_.size();
    default:
        LOG(ERROR) << "Invalid whence value: " << whence;
        return -1;
    }
    return 0;
}

file_video_input_t::file_video_input_t(const std::filesystem::path &video_path)
    : video_path_{video_path} {
    CHECK(!video_path.empty()) << "Video path is empty";
    if (!std::filesystem::exists(video_path)) {
        const auto error_message = fmt::format(
            "Video file \"{}\" does not exist", video_path.string());
        LOG(ERROR) << error_message;
        throw std::runtime_error{error_message};
    }
    CHECK(io_context_ == nullptr)
        << "This should have been the first time `io_context_` was set.";
    if (auto err = avio_open(&io_context_, video_path_.c_str(), AVIO_FLAG_READ);
        err < 0) {
        const auto error_message =
            fmt::format("Could not open video file \"{}\": {}",
                        video_path.string(), libav_error(err));
        LOG(ERROR) << error_message;
        throw std::runtime_error{error_message};
    }
}

file_video_input_t::~file_video_input_t() noexcept {
    if (io_context_ != nullptr) {
        avio_close(io_context_);
    }
}

auto file_video_input_t::get_io_context() const noexcept -> AVIOContext * {
    return io_context_;
}

void decoder_t::decode(std::vector<std::uint8_t> &dst,
                       std::unique_ptr<video_input_t> src) {
    CHECK(src) << "Video input IO context must be usable";
    CHECK(src->get_io_context() != nullptr)
        << "Video input IO context must be usable";
    std::unique_ptr<AVFormatContext, decltype(&avformat_free_context)>
        format_context{avformat_alloc_context(), &avformat_free_context};
    CHECK(format_context) << "Could not allocate format context";
    format_context->pb = src->get_io_context();
    int err = avformat_find_stream_info(format_context.get(), nullptr);
    if (err < 0) {
        LOG(ERROR) << "Could not find stream info:" << libav_error(err);
        throw std::runtime_error{
            fmt::format("Could not find stream info: {}", libav_error(err))};
    }
    // find video stream index
    const auto [codec, codec_params, video_stream_idx] =
        find_video_stream(format_context.get());
    std::unique_ptr<AVCodecContext, avcodec_context_deleter_t> codec_context{
        avcodec_alloc_context3(codec), avcodec_context_deleter_t{}};
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
    std::unique_ptr<AVFrame, av_frame_deleter_t> frame{av_frame_alloc(),
                                                       av_frame_deleter_t{}};
    CHECK(frame) << "Could not allocate frame";
    std::unique_ptr<AVPacket, av_packet_deleter_t> packet{
        av_packet_alloc(), av_packet_deleter_t{}};
    CHECK(packet) << "Could not allocate packet";
    std::unique_ptr<qr_code_decoder_t> qr_code_decoder{nullptr};
    image_buf_t img{};
    while (av_read_frame(format_context.get(), packet.get()) >= 0) {
        if (static_cast<size_t>(packet->stream_index) == video_stream_idx) {
            int ret = avcodec_send_packet(codec_context.get(), packet.get());
            if (ret < 0) {
                LOG(ERROR) << "Error sending packet to decoder:"
                           << libav_error(ret);
                throw std::runtime_error{fmt::format(
                    "Error sending packet to decoder: {}", libav_error(ret))};
            }
            while (ret >= 0) {
                // process decoded frame
                ret = avcodec_receive_frame(codec_context.get(), frame.get());
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    LOG(ERROR) << "Error during decoding:" << libav_error(ret);
                    throw std::runtime_error{fmt::format(
                        "Error during decoding: {}", libav_error(ret))};
                } else if (ret >= 0) {
                    get_frame_pixels(img, frame.get());
                    if (!qr_code_decoder) {
                        qr_code_decoder = std::make_unique<qr_code_decoder_t>(
                            img.width, img.height);
                    }
                    qr_code_decoder->decode(dst, img.buf);
                }
            }
        }
        av_packet_unref(packet.get());
    }
}

void decode(std::vector<std::uint8_t> &dst,
            const std::filesystem::path video_path) {
    if (std::filesystem::exists(video_path) == false) {
        LOG(ERROR) << "Video file " << "\"" << video_path
                   << "\" does not exist";
        return;
    }
    AVFormatContext *format_context = avformat_alloc_context();
    if (avformat_open_input(&format_context, video_path.c_str(), nullptr,
                            nullptr) != 0) {
        LOG(ERROR) << "Error opening input file";
        return;
    }

    if (int err = avformat_find_stream_info(format_context, nullptr); err < 0) {
        LOG(ERROR) << "Could not find stream info:" << libav_error(err);
        return;
    }
    const AVCodec *codec = nullptr;
    const AVCodecParameters *codec_params = nullptr;
    size_t video_stream_idx = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < format_context->nb_streams; ++i) {
        codec_params = format_context->streams[i]->codecpar;
        codec = avcodec_find_decoder(codec_params->codec_id);
        if (codec_params->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_idx = i;
            break;
        }
    }
    if (video_stream_idx == std::numeric_limits<size_t>::max()) {
        LOG(ERROR) << "Could not find video stream";
        throw std::runtime_error{"Could not find video stream"};
    }
    std::unique_ptr<AVCodecContext, decltype(&AVCodecContextDeleter)>
        codec_context{avcodec_alloc_context3(codec), &AVCodecContextDeleter};
    if (!codec_context) {
        LOG(ERROR) << "Could not allocate codec context";
        throw std::bad_alloc{};
    }
    if (auto err =
            avcodec_parameters_to_context(codec_context.get(), codec_params);
        err < 0) {
        LOG(ERROR) << "Could not copy codec params to codec context:"
                   << libav_error(err);
        throw std::runtime_error{
            "Could not copy codec params to codec context"};
    }
    if (int err = avcodec_open2(codec_context.get(), codec, nullptr); err < 0) {
        LOG(ERROR) << "Could not open codec:" << libav_error(err);
        return;
    }
    AVFrame *frame = av_frame_alloc();
    if (frame == nullptr) {
        LOG(ERROR) << "Could not allocate frame";
        throw std::bad_alloc{};
    }
    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
        LOG(ERROR) << "Could not allocate packet";
        throw std::bad_alloc{};
    }
    std::unique_ptr<qr_code_decoder_t> qr_code_decoder{nullptr};
    image_buf_t img{};
    while (av_read_frame(format_context, packet) >= 0) {
        if (static_cast<size_t>(packet->stream_index) == video_stream_idx) {
            int ret = avcodec_send_packet(codec_context.get(), packet);
            if (ret < 0) {
                LOG(ERROR) << "Error sending packet to decoder:"
                           << libav_error(ret);
                return;
            }
            while (ret >= 0) {
                // process decoded frame
                ret = avcodec_receive_frame(codec_context.get(), frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    LOG(ERROR) << "Error during decoding:" << libav_error(ret);
                    return;
                }
                get_frame_pixels(img, frame);
                if (!qr_code_decoder) {
                    qr_code_decoder = std::make_unique<qr_code_decoder_t>(
                        img.width, img.height);
                }
                qr_code_decoder->decode(dst, img.buf);
            }
        }
        av_packet_unref(packet);
    }
    // Flush the decoder with a null packet
    int ret = avcodec_send_packet(codec_context.get(), packet);
    if (ret < 0) {
        LOG(ERROR) << "Error sending packet to decoder:" << libav_error(ret);
        throw std::runtime_error{"Error sending packet to decoder"};
    }
    while ((ret = avcodec_receive_frame(codec_context.get(), frame)) >= 0) {
        get_frame_pixels(img, frame);
        if (!qr_code_decoder) {
            qr_code_decoder =
                std::make_unique<qr_code_decoder_t>(img.width, img.height);
        }
        qr_code_decoder->decode(dst, img.buf);
    }
    if (ret != AVERROR_EOF) {
        LOG(ERROR) << "Error during decoding:" << libav_error(ret);
        throw std::runtime_error{"Error during decoding"};
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    avformat_close_input(&format_context);
}

} // namespace net_zelcon::plain_sight