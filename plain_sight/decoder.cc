#include "plain_sight/decoder.h"
#include "plain_sight/qr_codes.h"
#include "plain_sight/util.h"
#include <glog/logging.h>
#include <limits>
#include <memory>
#include <stdexcept>

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
                       frame->width, frame->height, destination_format, 1) < 0) {
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

    //av_freep(&grayscale_frame->data[0]);
    av_frame_free(&grayscale_frame);
}

} // namespace

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
            qr_code_decoder = std::make_unique<qr_code_decoder_t>(
                img.width, img.height);
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