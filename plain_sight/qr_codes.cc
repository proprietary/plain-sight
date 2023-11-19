#include "plain_sight/qr_codes.h"
#include <qrcodegen.hpp>

namespace net_zelcon::plain_sight {
auto make_frames(const std::vector<std::uint8_t> &src)
    -> std::vector<QRCodeFrame> {
  // TODO: Implement this function.
  return {};
}

auto split_frames(const std::vector<std::uint8_t> &src)
    -> std::vector<qrcodegen::QrCode> {
  std::vector<qrcodegen::QrCode> qr_codes;
  constexpr size_t max_size = 500;
  for (size_t i = 0; i < src.size(); i += max_size) {
    size_t size = std::min(max_size, src.size() - i);
    std::vector<std::uint8_t> chunk(src.begin() + i, src.begin() + i + size);
    qr_codes.emplace_back(
        qrcodegen::QrCode::encodeBinary(chunk, qrcodegen::QrCode::Ecc::HIGH));
  }
  return qr_codes;
}
} // namespace net_zelcon::plain_sight