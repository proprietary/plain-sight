#include "plain_sight/qr_codes.h"
#include <glog/logging.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <qrcodegen.hpp>
#include <string>
#include <iterator>

namespace net_zelcon::plain_sight {

auto split_frames(const std::vector<std::uint8_t> &src)
    -> std::vector<qrcodegen::QrCode> {
    std::vector<qrcodegen::QrCode> qr_codes;
    constexpr std::ptrdiff_t max_size = 100;
    constexpr auto qr_version = 20;
    for (auto it = src.begin(); it != src.end(); std::advance(it, std::min(max_size, std::distance(it, src.end())))) {
        const std::vector<std::uint8_t> chunk(it,
                                        std::next(it, std::min(max_size, std::distance(it, src.end()))));
        const std::vector<qrcodegen::QrSegment> segments = {
            qrcodegen::QrSegment::makeBytes(chunk)};
        const auto qr_code = qrcodegen::QrCode::encodeSegments(
            segments, qrcodegen::QrCode::Ecc::HIGH, qr_version, qr_version, -1,
            true);
        qr_codes.emplace_back(std::move(qr_code));
    }
    return qr_codes;
}

auto split_frames(std::string_view src) -> std::vector<qrcodegen::QrCode> {
    std::vector<qrcodegen::QrCode> qr_codes;
    constexpr size_t max_size = 500;
    for (size_t i = 0; i < src.size(); i += max_size) {
        const auto size = std::min(max_size, src.size() - i);
        std::string chunk{src.substr(i, size)};
        qr_codes.emplace_back(qrcodegen::QrCode::encodeText(
            chunk.c_str(), qrcodegen::QrCode::Ecc::HIGH));
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

void decode_qr_code(std::vector<std::uint8_t> &dst, cv::Mat src) {
    cv::QRCodeDetector qr_decoder{};
    auto data = qr_decoder.detectAndDecode(src);
    DLOG(INFO) << data;
    std::copy(data.begin(), data.end(), std::back_inserter(dst));
}

qr_code_decoder_t::qr_code_decoder_t(int width, int height)
    : qr_{quirc_new(), &quirc_destroy}, width_{width}, height_{height} {
    if (!qr_) {
        LOG(FATAL) << "Could not allocate quirc";
        throw std::bad_alloc{};
    }
    auto err = quirc_resize(qr_.get(), width_, height_);
    if (err < 0) {
        PLOG(FATAL) << "Failed to allocate video memory";
        throw std::runtime_error{"Failed to allocate video memory"};
    }
}

void qr_code_decoder_t::decode(std::vector<std::uint8_t> &dst,
                               const std::span<std::uint8_t> src) {
    CHECK(src.size() > 0) << "Empty image; no QR codes to possibly find";
    DLOG(INFO) << "Decoding " << src.size() << " bytes";
    std::uint8_t *image = quirc_begin(qr_.get(), &width_, &height_);
    // one byte per pixel, `width_` pixels per line, `height_` lines in the
    // buffer
    CHECK(static_cast<size_t>(width_) * static_cast<size_t>(height_) <=
          src.size())
        << "Buffer too small";
    std::copy(src.begin(), src.end(), image);
    DCHECK(std::equal(src.begin(), src.end(), image)) << "Copy failed";
    quirc_end(qr_.get());
    int num_codes = quirc_count(qr_.get());
    DLOG(INFO) << "Found " << num_codes << " QR codes";
    CHECK(num_codes > 0) << "No QR codes found";
    for (int i = 0; i < num_codes; i++) {
        quirc_code code;
        quirc_extract(qr_.get(), i, &code);
        quirc_data data;
        quirc_decode_error_t err = quirc_decode(&code, &data);
        if (err != QUIRC_SUCCESS) {
            LOG(ERROR) << "Failed to decode QR code: " << quirc_strerror(err);
            continue;
        }
        DLOG(INFO) << "Payload: "
                   << std::string{reinterpret_cast<char *>(data.payload),
                                  static_cast<size_t>(data.payload_len)};
        std::copy(data.payload, data.payload + data.payload_len,
                  std::back_inserter(dst));
    }
}

} // namespace net_zelcon::plain_sight