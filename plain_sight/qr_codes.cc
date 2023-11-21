#include <glog/logging.h>
#include "plain_sight/qr_codes.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <qrcodegen.hpp>

namespace net_zelcon::plain_sight {

auto split_frames(const std::vector<std::uint8_t> &src)
    -> std::vector<qrcodegen::QrCode> {
    std::vector<qrcodegen::QrCode> qr_codes;
    constexpr size_t max_size = 500;
    for (size_t i = 0; i < src.size(); i += max_size) {
        size_t size = std::min(max_size, src.size() - i);
        std::vector<std::uint8_t> chunk(src.begin() + i,
                                        src.begin() + i + size);
        qr_codes.emplace_back(qrcodegen::QrCode::encodeBinary(
            chunk, qrcodegen::QrCode::Ecc::HIGH));
    }
    return qr_codes;
}

auto decode_qr_code(const std::span<std::uint8_t> src)
    -> std::vector<std::uint8_t> {
    cv::QRCodeDetector qr_decoder{};
    cv::Mat mat{cv::Size{static_cast<int>(src.size()), 1}, CV_8UC1, src.data()};
    auto data = qr_decoder.detectAndDecode(mat);
    return std::vector<std::uint8_t>{data.begin(), data.end()};
}

void decode_qr_code(std::vector<std::uint8_t> &dst,
                    cv::Mat src) {
    cv::QRCodeDetector qr_decoder{};
    auto data = qr_decoder.detectAndDecode(src);
    DLOG(INFO) << data;
    std::copy(data.begin(), data.end(), std::back_inserter(dst));
}

} // namespace net_zelcon::plain_sight