#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_

#include <vector>
#include <cstdint>

namespace net_zelcon::plain_sight {
    struct QRCodeFrame {
        std::vector<uint8_t> data;
        uint64_t width;
        uint64_t height;
    };

    auto make_frames(const std::vector<uint8_t> &src) -> std::vector<QRCodeFrame>;
} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_