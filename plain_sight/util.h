#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_

#include <glog/logging.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace net_zelcon::plain_sight {

void read_file(std::vector<std::uint8_t> &dst, std::filesystem::path path);

void read_file(std::string &dst, const std::filesystem::path &path);

std::string libav_error(int error);

void AVCodecContextDeleter(AVCodecContext *p);

struct avcodec_context_deleter_t {
    void operator()(AVCodecContext *p) const;
};

struct av_packet_deleter_t {
    void operator()(AVPacket *p) const;
};

struct av_frame_deleter_t {
    void operator()(AVFrame *p) const;
};

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_