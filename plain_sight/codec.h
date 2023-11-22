#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_CODEC_H
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_CODEC_H

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace net_zelcon::plain_sight {

void encode_raw_data(std::vector<std::uint8_t> &dst,
                     const std::vector<std::uint8_t> &src);

void decode_raw_data(std::vector<std::uint8_t> &dst,
                     std::span<std::uint8_t> src);

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_CODEC_H