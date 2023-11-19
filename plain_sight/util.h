#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_

#include <cstdint>
#include <filesystem>
#include <vector>

namespace net_zelcon::plain_sight {

void read_file(std::vector<std::uint8_t> &dst, std::filesystem::path path);

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_UTIL_H_