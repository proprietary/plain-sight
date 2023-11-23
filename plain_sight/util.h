#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_

#include <cstdint>
#include <filesystem>
#include <glog/logging.h>
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

/// @brief Deleter for libav types. Points to a function pointer from the C API.
/// @tparam LibavType the libav struct type, e.g., `AVFrame`
/// @tparam Fn Function pointer type, e.g., `void(*)(AVFrame**)`
template <typename LibavType, typename Fn> class libav_deleter_t;

/// @brief Specialization of `libav_deleter_t` for "deleter"/"free" functions
/// that take 2-star pointers.
/// @tparam LibavType the libav struct type, e.g., `AVCodecContext`
template <typename LibavType>
class libav_deleter_t<LibavType, void (*)(LibavType **)> {
  public:
    using Fn = void (*)(LibavType **);
    libav_deleter_t(Fn fn) : fn_(fn) {}
    void operator()(LibavType *p) const { fn_(&p); }

  private:
    Fn fn_;
};

/// @brief Specialization of `libav_deleter_t` for "deleter"/"free" functions
/// that take pointers to the C struct object to be deleted.
/// @tparam LibavType the libav struct type, e.g., `AVFrame`
template <typename LibavType>
class libav_deleter_t<LibavType, void (*)(LibavType *)> {
  public:
    using Fn = void (*)(LibavType *);
    libav_deleter_t(Fn fn) : fn_(fn) {}
    void operator()(LibavType *p) const { fn_(p); }

  private:
    Fn fn_;
};

template <typename LibavType, auto Deleter>
using libav_ptr_t =
    std::unique_ptr<LibavType, libav_deleter_t<LibavType, decltype(Deleter)>>;

using libav_frame_ptr_t = libav_ptr_t<AVFrame, av_frame_free>;

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_