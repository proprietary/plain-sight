#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_

#include <cstdint>
#include <vector>

#include <qrcodegen.hpp>

namespace net_zelcon::plain_sight {
/**
 * @brief Represents a frame of a QR code.
 */
struct QRCodeFrame {
  std::vector<uint8_t> data; /**< The data of the QR code frame. */
  uint64_t width;            /**< The width of the QR code frame. */
  uint64_t height;           /**< The height of the QR code frame. */
};

/**
 * @brief Generates QR code frames from the given source data.
 *
 * @param src The source data to generate QR code frames from.
 * @return std::vector<QRCodeFrame> The generated QR code frames.
 */
auto make_frames(const std::vector<uint8_t> &src) -> std::vector<QRCodeFrame>;

auto split_frames(const std::vector<uint8_t> &src)
    -> std::vector<qrcodegen::QrCode>;
} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_