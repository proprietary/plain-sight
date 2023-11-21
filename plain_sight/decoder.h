#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_DECODER_H_

#include <fstream>
#include <istream>
#include <vector>
#include <cstdint>
#include <filesystem>

namespace net_zelcon::plain_sight {

void decode(std::ostream& dst, const std::istream& video);

void decode(std::vector<std::uint8_t>& dst, const std::istream& video);

void decode(std::vector<std::uint8_t>& dst, const std::filesystem::path video_path);

} // namespace net_zelcon::plain_sight

#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_DECODER_H_
#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_DECODER_H_