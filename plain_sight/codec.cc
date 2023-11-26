#include <memory>

#include "plain_sight/codec.h"
#include "plain_sight/decoder.h"
#include "plain_sight/encoder.h"
#include "plain_sight/qr_codes.h"

namespace net_zelcon::plain_sight {

void encode_raw_data(std::vector<std::uint8_t> &dst,
                     const std::vector<std::uint8_t> &src) {
    auto qr_codes =
        std::make_shared<std::vector<qrcodegen::QrCode>>(split_frames(src));
    auto encoder = encoder_t::builder()
                       .set_border_size(4)
                       .set_fps(30)
                       .set_scale(4)
                       .set_video_format("mp4")
                       .set_qr_codes(qr_codes)
                       .build();
    encoder.encode(std::make_unique<in_memory_video_output_t>(dst));
}

void decode_raw_data(std::vector<std::uint8_t> &dst,
                     std::span<std::uint8_t> src) {
    auto video_input = std::make_unique<in_memory_video_input_t>(src);
    decoder_t decoder;
    decoder.decode(dst, std::move(video_input));
}

void encode_file(std::filesystem::path dst,
                 const std::vector<std::uint8_t> &src) {
    auto qr_codes =
        std::make_shared<std::vector<qrcodegen::QrCode>>(split_frames(src));
    auto encoder = encoder_t::builder()
                       .set_border_size(4)
                       .set_fps(30)
                       .set_scale(4)
                       .set_video_format("mp4")
                       .set_qr_codes(qr_codes)
                       .build();
    auto video_output = std::make_unique<file_video_output_t>(dst);
    encoder.encode(std::move(video_output));
}

void decode_file(std::vector<std::uint8_t> &dst,
                 const std::filesystem::path &src) {
    auto video_input = std::make_unique<file_video_input_t>(src);
    decoder_t decoder;
    decoder.decode(dst, std::move(video_input));
}

} // namespace net_zelcon::plain_sight