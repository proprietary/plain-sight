#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_

#include <cstdint>
#include <memory>
#include <opencv2/opencv.hpp>
#include <quirc.h>
#include <span>
#include <string_view>
#include <vector>

#include <qrcodegen.hpp>

namespace net_zelcon::plain_sight {

auto split_frames(const std::vector<uint8_t> &src)
    -> std::vector<qrcodegen::QrCode>;

auto split_frames(std::string_view src) -> std::vector<qrcodegen::QrCode>;

auto decode_qr_code(const std::span<std::uint8_t> src)
    -> std::vector<std::uint8_t>;

void decode_qr_code(std::vector<uint8_t> &dst, cv::Mat src);

class qr_code_decoder_t {
  public:
    explicit qr_code_decoder_t(int width, int height);
    void decode(std::vector<std::uint8_t> &dst,
                const std::span<std::uint8_t> src);

  private:
    std::unique_ptr<quirc, decltype(&quirc_destroy)> qr_;
    int width_, height_;
};

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_QR_CODES_H_