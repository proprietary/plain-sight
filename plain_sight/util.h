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

/// @brief RAII-style deleter for libav objects that take a pointer to a pointer.
template<typename Deleter>
class libav_2_star_deleter_t {
public:
  libav_2_star_deleter_t(Deleter fn) : fn_(fn) {}
  void operator()(auto *p) const { fn_(&p); }
private:
  Deleter fn_;
};

template<typename LibavType, auto Deleter>
using libav_2_star_ptr_t = std::unique_ptr<LibavType, libav_2_star_deleter_t<decltype(Deleter)>>;

/// @brief RAII-style deleter for libav objects that take a pointer.
/// @tparam Deleter C-style function pointer to the "free" function for the libav object.
template<typename Deleter>
class libav_deleter_t {
public:
  libav_deleter_t(Deleter fn) : fn_(fn) {}
  void operator()(auto *p) const { fn_(p); } 
private:
  Deleter fn_;
};

template<typename LibavType, auto Deleter>
using libav_ptr_t = std::unique_ptr<LibavType, libav_deleter_t<decltype(Deleter)>>;

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_