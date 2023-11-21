#include "plain_sight/decoder.h"
#include "plain_sight/qr_codes.h"
#include "plain_sight/util.h"
#include <glog/logging.h>
#include <limits>
#include <memory>
#include <opencv2/opencv.hpp>
#include <stdexcept>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace net_zelcon::plain_sight {

namespace {

auto get_frame_pixels(const AVFrame *frame) -> cv::Mat {
    SwsContext *sws_ctx = sws_getContext(
        frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
        frame->width, frame->height, AV_PIX_FMT_BGR24, SWS_BILINEAR, nullptr,
        nullptr, nullptr);
    if (sws_ctx == nullptr) {
        throw std::runtime_error{"Could not initialize sws context"};
    }
    AVFrame *bgr_frame = av_frame_alloc();
    if (bgr_frame == nullptr) {
        LOG(ERROR) << "Could not allocate bgr frame";
        throw std::runtime_error{"Could not allocate bgr frame"};
    }
    if (av_image_alloc(bgr_frame->data, bgr_frame->linesize, frame->width,
                       frame->height, AV_PIX_FMT_BGR24, 1) < 0) {
        LOG(ERROR) << "Could not allocate raw picture buffer";
        throw std::bad_alloc{};
    }

    auto err = sws_scale_frame(sws_ctx, bgr_frame, frame);
    if (err < 0) {
        LOG(ERROR) << "Could not scale frame:" << libav_error(err);
        throw std::runtime_error{"Could not scale frame"};
    }

    cv::Mat mat(frame->height, frame->width, CV_8UC3, bgr_frame->data[0], bgr_frame->linesize[0]);

    av_frame_free(&bgr_frame);
    sws_freeContext(sws_ctx);

    return mat;
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
        return;
    }
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
        LOG(ERROR) << "Could not allocate codec context";
        throw std::bad_alloc{};
    }
    if (auto err = avcodec_parameters_to_context(codec_context, codec_params);
        err < 0) {
        LOG(ERROR) << "Could not copy codec params to codec context:"
                   << libav_error(err);
        throw std::runtime_error{"Could not copy codec params to codec context"};
    }
    if (int err = avcodec_open2(codec_context, codec, nullptr); err < 0) {
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
    while (av_read_frame(format_context, packet) >= 0) {
        if (static_cast<size_t>(packet->stream_index) == video_stream_idx) {
            int ret = avcodec_send_packet(codec_context, packet);
            if (ret < 0) {
                LOG(ERROR) << "Error sending packet to decoder:"
                           << libav_error(ret);
                return;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_context, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    LOG(ERROR) << "Error during decoding:" << libav_error(ret);
                    return;
                }
                cv::Mat mat = get_frame_pixels(frame);
                decode_qr_code(dst, mat);
            }
        }
        av_packet_unref(packet);
    }
    avformat_close_input(&format_context);
    if (packet != nullptr)
        av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&codec_context);
}

} // namespace net_zelcon::plain_sight