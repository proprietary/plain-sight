#include "plain_sight/video_generator.h"
#include "plain_sight/qr_codes.h"
#include "plain_sight/util.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <glog/logging.h>
#include <iostream>
#include <string>
#include <vector>

#include <qrcodegen.hpp>

namespace net_zelcon::plain_sight {

void write_qr_codes(const std::vector<qrcodegen::QrCode> &qr_codes,
                    std::filesystem::path output_path) {
    if (qr_codes.size() < 1)
        return;

#ifdef DEBUG
    av_log_set_level(AV_LOG_DEBUG);
#endif

    // Compute size of QR Codes
    const auto qr_code_size = qr_codes[0].getSize();
    CHECK(std::all_of(qr_codes.begin(), qr_codes.end(),
                      [qr_code_size](const auto &qr_code) {
                          return qr_code.getSize() == qr_code_size;
                      }))
        << "QR Codes must all be the same size";
    constexpr auto border = 10;
    constexpr auto scale = 8;
    auto max_size_value = scale * qr_code_size + border * 2;
    while (max_size_value % 2 != 0) // because libav needs a multiple of 2
        max_size_value++;

    // Open output file
    AVFormatContext *format_context = nullptr;
    if (avformat_alloc_output_context2(&format_context, nullptr, nullptr,
                                       output_path.c_str()) < 0) {
        std::cerr << "Error creating output context" << std::endl;
        return;
    }

    const AVOutputFormat *output_format =
        av_guess_format(nullptr, output_path.c_str(), nullptr);
    if (output_format == nullptr) {
        std::cerr << "Error guessing output format" << std::endl;
        return;
    }

    if (avio_open(&format_context->pb, output_path.c_str(),
                  AVIO_FLAG_READ_WRITE) < 0) {
        std::cerr << "Error opening output file" << std::endl;
        return;
    }

    // Find video encoder
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (codec == nullptr) {
        std::cerr << "Codec not found" << std::endl;
        return;
    }
    // Create new codec context
    AVCodecContext *codec_context = avcodec_alloc_context3(codec);
    if (codec_context == nullptr) {
        std::cerr << "Error allocating codec context" << std::endl;
        return;
    }

    // Set codec parameters
    codec_context->codec_id = output_format->video_codec;
    codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
    // Resolution
    codec_context->width = max_size_value;
    codec_context->height = max_size_value;
    // frame rate
    codec_context->time_base = AVRational{
        .num = 1, .den = 20}; // most social media don't accept >30fps
    codec_context->gop_size = 1;
    codec_context->max_b_frames = 1;

    // Open video codec
    if (avcodec_open2(codec_context, codec, nullptr) < 0) {
        LOG(ERROR) << "Error opening video codec";
        return;
    }

    // Create new video stream
    AVStream *video_stream = avformat_new_stream(format_context, codec);
    if (video_stream == nullptr) {
        LOG(ERROR) << "Error creating new stream";
        throw std::runtime_error{"Error creating new stream"};
    }

    // Copy codec parameters to video stream
    if (avcodec_parameters_from_context(video_stream->codecpar, codec_context) <
        0) {
        LOG(ERROR) << "Error copying codec parameters to stream";
        throw std::runtime_error{"Error copying codec parameters to stream"};
    }

    // Write stream header
    if (avformat_write_header(format_context, nullptr) < 0) {
        LOG(ERROR) << "Error writing stream header";
        throw std::runtime_error{"Error writing stream header"};
    }

    // Create video frame
    AVFrame *video_frame = av_frame_alloc();
    if (!video_frame) {
        LOG(ERROR) << "Error allocating video frame";
        throw std::runtime_error{"Error allocating video frame"};
    }

    // Set frame parameters
    video_frame->width = max_size_value;
    video_frame->height = max_size_value;
    video_frame->format = codec_context->pix_fmt;

    // allocate framebuffer
    int sz = av_image_alloc(video_frame->data, video_frame->linesize,
                            video_frame->width, video_frame->height,
                            codec_context->pix_fmt, 1);
    if (sz < 0) {
        LOG(ERROR) << "Error allocating frame buffer";
        throw std::bad_alloc{};
    }

    AVPacket *pkt = av_packet_alloc();
    av_new_packet(pkt, sz);
    pkt->data = nullptr;
    pkt->size = 0;

    int ret;
    int frame_counter = 0;

    // Write QR Codes to video frames.
    for (const auto &qr_code : qr_codes) {
        // TODO: this is embarassingly parallel;
        // parallelize it (gotta stick with CPU only tho; OpenMP?)

        // Draw QR code image with a white border and some zooming
        int row = 0;
        // fill top border with white
        for (; row < border; ++row) {
            for (int col = 0; col < scale * qr_code_size + 2 * border; ++col) {
                video_frame->data[0][row * video_frame->linesize[0] + col] =
                    255; // Y
                video_frame
                    ->data[1][row / 2 * video_frame->linesize[1] + col / 2] =
                    128; // U
                video_frame
                    ->data[2][row / 2 * video_frame->linesize[2] + col / 2] =
                    128; // V
            }
        }
        for (; row < scale * qr_code_size + border; ++row) {
            // fill left border with white
            int col = 0;
            for (; col < border; ++col) {
                video_frame->data[0][row * video_frame->linesize[0] + col] =
                    255; // Y
                video_frame
                    ->data[1][row / 2 * video_frame->linesize[1] + col / 2] =
                    128; // U
                video_frame
                    ->data[2][row / 2 * video_frame->linesize[2] + col / 2] =
                    128; // V
            }
            for (; col < scale * qr_code_size + border; ++col) {
                // fill QR Code with black or white
                const int y = (row - border) / scale;
                const int x = (col - border) / scale;
                // this tells us whether to draw a black or white pixel
                const bool pixel = qr_code.getModule(x, y);
                for (int i = 0; i < scale; ++i) {
                    const int rdx = border + (y * scale + i);
                    for (int j = 0; j < scale; ++j) {
                        const int cdx = border + (x * scale + j);
                        // Convert QR Code image to YUV420P format
                        video_frame
                            ->data[0][rdx * video_frame->linesize[0] + cdx] =
                            pixel ? 0 : 255; // Y
                        video_frame
                            ->data[1][rdx / 2 * video_frame->linesize[1] +
                                      cdx / 2] = 128; // U
                        video_frame
                            ->data[2][rdx / 2 * video_frame->linesize[2] +
                                      cdx / 2] = 128; // V
                    }
                }
            }
            // fill right border with white
            for (; col < scale * qr_code_size + border * 2; ++col) {
                video_frame->data[0][row * video_frame->linesize[0] + col] =
                    255; // Y
                video_frame
                    ->data[1][row / 2 * video_frame->linesize[1] + col / 2] =
                    128; // U
                video_frame
                    ->data[2][row / 2 * video_frame->linesize[2] + col / 2] =
                    128; // V
            }
        }
        // fill bottom border with white
        for (; row < scale * qr_code_size + border * 2; ++row) {
            for (int col = 0; col < scale * qr_code_size + 2 * border; ++col) {
                video_frame->data[0][row * video_frame->linesize[0] + col] =
                    255; // Y
                video_frame
                    ->data[1][row / 2 * video_frame->linesize[1] + col / 2] =
                    128; // U
                video_frame
                    ->data[2][row / 2 * video_frame->linesize[2] + col / 2] =
                    128; // V
            }
        }

        // Set the PTS for the frame
        video_frame->pts = frame_counter++;

        // Write video frame
        size_t sz = av_image_get_buffer_size(
            AV_PIX_FMT_YUV420P, video_frame->width, video_frame->height, 1);
        if (sz < 0) {
            std::cerr << "Error getting image buffer size" << std::endl;
            return;
        }
        if (avcodec_send_frame(codec_context, video_frame) < 0) {
            std::cerr << "Error sending frame to codec context" << std::endl;
            return;
        }
        while ((ret = avcodec_receive_packet(codec_context, pkt)) == 0) {
            pkt->stream_index = video_stream->index;
            av_packet_rescale_ts(pkt, codec_context->time_base,
                                 video_stream->time_base);
            ret = av_interleaved_write_frame(format_context, pkt);
            if (ret < 0) {
                std::cerr << "Error writing video frame" << std::endl;
                return;
            }
            av_packet_unref(pkt);
        }
    }

    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        std::cerr << "Error receiving packet from codec context" << std::endl;
        return;
    }

    // Flush encoder with a null frame
    avcodec_send_frame(codec_context, nullptr);

    while ((ret = avcodec_receive_packet(codec_context, pkt)) >= 0) {
        pkt->stream_index = video_stream->index;
        av_packet_rescale_ts(pkt, codec_context->time_base,
                             video_stream->time_base);
        av_interleaved_write_frame(format_context, pkt);
        av_packet_unref(pkt);
    }
    if (ret != AVERROR_EOF) {
        LOG(ERROR) << "Error flushing encoder:" << libav_error(ret);
        throw std::runtime_error{"Error flushing encoder"};
    }

    // Write trailer
    if (av_write_trailer(format_context) < 0) {
        std::cerr << "Error writing trailer" << std::endl;
        return;
    }

    // Clean up resources
    // Free the packet
    av_packet_unref(pkt);
    // Free the YUV frame
    if (video_frame)
        av_frame_free(&video_frame);
    if (codec_context != nullptr)
        avcodec_free_context(&codec_context);
    if (format_context != nullptr && format_context->pb != nullptr &&
        !(format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&format_context->pb);
    if (format_context != nullptr)
        avformat_free_context(format_context);
}

} //  namespace net_zelcon::plain_sight