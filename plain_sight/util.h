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

class io_context_type_t {
public:
  bool is_using_custom_io() const noexcept { return is_using_custom_io_; }
  void set_custom_io_context(AVIOContext *io_context) noexcept {
    io_context_ = io_context;
    is_using_custom_io_ = true;
  }
  auto get_io_context() const noexcept -> AVIOContext* { return io_context_; }
private:
  bool is_using_custom_io_ = false;
  AVIOContext* io_context_ = nullptr;
};

class av_format_context_deleter_t {
  public:
    void operator()(AVFormatContext *p) const {
        CHECK(p != nullptr);
        CHECK(p->opaque != nullptr);
        const auto io_context_type = static_cast<io_context_type_t *>(p->opaque);
        if (!io_context_type->is_using_custom_io()) {
            avformat_close_input(&p);
        }
        avformat_free_context(p);
    }
};

class av_format_context_t
    : public std::unique_ptr<AVFormatContext, av_format_context_deleter_t> {
  public:
    explicit av_format_context_t() noexcept
        : std::unique_ptr<AVFormatContext, av_format_context_deleter_t>(
              avformat_alloc_context()), io_context_type_{} {
        if (!get()) {
            throw std::runtime_error("Could not allocate AVFormatContext");
        }
        get()->opaque = io_context_type_.get();
    }

    void set_pb(AVIOContext *pb) noexcept {
        io_context_type_->set_custom_io_context(pb);
        get()->pb = pb;
    }

  private:
    std::unique_ptr<io_context_type_t> io_context_type_;
};

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_