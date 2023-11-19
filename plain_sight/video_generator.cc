#include "plain_sight/video_generator.h"
#include "plain_sight/qr_codes.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <qrcodegen.hpp>

namespace net_zelcon::plain_sight {

void stuff() {}

void write_qr_codes(const std::vector<qrcodegen::QrCode> &qr_codes) {
  if (qr_codes.size() < 1)
    return;

  av_log_set_level(AV_LOG_DEBUG);

  // Get maximal size of QR Codes
  const auto max_size = std::max_element(
      qr_codes.begin(), qr_codes.end(),
      [](const qrcodegen::QrCode &a, const qrcodegen::QrCode &b) -> bool {
        return a.getSize() < b.getSize();
      });
  assert(max_size != qr_codes.cend());
  auto max_size_value = max_size->getSize();
  if (max_size_value % 2 > 0) {
    max_size_value++;
  }

  // Open output file
  std::string outputFilename = "/tmp/output.mp4";
  AVFormatContext *format_context = nullptr;
  if (avformat_alloc_output_context2(&format_context, nullptr, nullptr,
                                     outputFilename.c_str()) < 0) {
    std::cerr << "Error creating output context" << std::endl;
    return;
  }

  const AVOutputFormat *output_format =
      av_guess_format(nullptr, outputFilename.c_str(), nullptr);
  if (output_format == nullptr) {
    std::cerr << "Error guessing output format" << std::endl;
    return;
  }

  if (avio_open(&format_context->pb, outputFilename.c_str(),
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
  codec_context->width = max_size_value;  // Set your desired video width
  codec_context->height = max_size_value; // Set your desired video height
  // frame rate
  codec_context->time_base =
      AVRational{.num = 1, .den = 25}; // Set your desired frame rate
  codec_context->gop_size = 1;
  codec_context->max_b_frames = 1;

  // Open video codec
  if (avcodec_open2(codec_context, codec, nullptr) < 0) {
    std::cerr << "Error opening video codec" << std::endl;
    return;
  }

  // Create new video stream
  AVStream *video_stream = avformat_new_stream(format_context, codec);
  if (video_stream == nullptr) {
    std::cerr << "Error creating new stream" << std::endl;
    return;
  }

  // Copy codec parameters to video stream
  if (avcodec_parameters_from_context(video_stream->codecpar, codec_context) <
      0) {
    std::cerr << "Error copying codec parameters to stream" << std::endl;
    return;
  }

  // Write stream header
  if (avformat_write_header(format_context, nullptr) < 0) {
    std::cerr << "Error writing stream header" << std::endl;
    return;
  }

  // Create video frame
  AVFrame *pFrame = av_frame_alloc();
  if (!pFrame) {
    std::cerr << "Error allocating video frame" << std::endl;
    return;
  }

  // Set frame parameters
  pFrame->width = max_size_value;
  pFrame->height = max_size_value;
  pFrame->format = AV_PIX_FMT_YUV420P;

  // allocate framebuffer
  int sz = av_image_alloc(pFrame->data, pFrame->linesize, pFrame->width,
                          pFrame->height, AV_PIX_FMT_YUV420P, 1);
  if (sz < 0) {
    std::cerr << "Error allocating frame buffer" << std::endl;
    return;
  }

  AVPacket *pkt = av_packet_alloc();
  av_new_packet(pkt, sz);
  pkt->data = nullptr;
  pkt->size = 0;

  int ret;
  int frame_counter = 0;

  // Write QR Codes to video frames
  for (const auto &qr_code : qr_codes) {

    // Generate QR Code image
    for (int y = 0; y < qr_code.getSize(); y++) {
      for (int x = 0; x < qr_code.getSize(); x++) {
        // Convert QR Code image to YUV420P format
        const bool pixel_set = qr_code.getModule(x, y);
        pFrame->data[0][y * pFrame->linesize[0] + x] = pixel_set ? 0 : 255; // Y
        pFrame->data[1][y * pFrame->linesize[1] + x] = 0;                   // U
        pFrame->data[2][y * pFrame->linesize[2] + x] = 0;                   // V
      }
    }

    // Set the PTS for the frame
    pFrame->pts = frame_counter++;

    // Write video frame
    size_t sz = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pFrame->width,
                                         pFrame->height, 1);
    if (sz < 0) {
      std::cerr << "Error getting image buffer size" << std::endl;
      return;
    }
    if (avcodec_send_frame(codec_context, pFrame) < 0) {
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

  while (avcodec_receive_packet(codec_context, pkt) == 0) {
    pkt->stream_index = video_stream->index;
    av_packet_rescale_ts(pkt, codec_context->time_base,
                         video_stream->time_base);
    av_interleaved_write_frame(format_context, pkt);
    av_packet_unref(pkt);
  }

  // Write trailer
  if (av_write_trailer(format_context) < 0) {
    std::cerr << "Error writing trailer" << std::endl;
    return;
  }

  // Clean up resources
  // Free the packet
  //av_packet_unref(pkt);
  // Free the YUV frame
  if (pFrame)
    av_frame_free(&pFrame);
  if (codec_context != nullptr)
    avcodec_free_context(&codec_context);
  if (format_context != nullptr && format_context->pb != nullptr &&
      !(format_context->oformat->flags & AVFMT_NOFILE))
    avio_closep(&format_context->pb);
  if (format_context != nullptr)
    avformat_free_context(format_context);
}

} //  namespace net_zelcon::plain_sight