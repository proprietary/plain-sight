#ifndef _INCLUDE_VIDEO_GENERATOR_H_
#define _INCLUDE_VIDEO_GENERATOR_H_

#include <cstdint>
#include <qrcodegen.hpp>
#include <vector>
#include <filesystem>

namespace net_zelcon::plain_sight {
void write_qr_codes(const std::vector<qrcodegen::QrCode> &qr_codes, std::filesystem::path output_path);
}

#endif // _INCLUDE_VIDEO_GENERATOR_H_