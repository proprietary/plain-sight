#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_

#include <cstdint>
#include <vector>
#include <span>
#include <opencv2/opencv.hpp>

#include <qrcodegen.hpp>

namespace net_zelcon::plain_sight {

auto split_frames(const std::vector<uint8_t> &src)
    -> std::vector<qrcodegen::QrCode>;

auto decode_qr_code(const std::span<std::uint8_t> src) -> std::vector<std::uint8_t>;

void decode_qr_code(std::vector<uint8_t>& dst, cv::Mat src);
} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_