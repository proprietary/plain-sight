#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_ENCODER_H
#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_ENCODER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "plain_sight/qr_codes.h"
#include <qrcodegen.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
}

namespace net_zelcon::plain_sight {

/// @brief Draws the frame with the QR code.
/// @param frame libavcodec frame that is already allocated
/// @param qr_code
/// @param border_size Whitespace on each side of the QR code
/// @param scale How many pixels per QR code module
void draw_frame(const AVCodecContext *const, AVFrame *,
                const qrcodegen::QrCode &, const int border_size,
                const int scale);

class video_format_t {
  public:
    auto filename() const noexcept -> std::string_view;
    auto file_contents() const noexcept -> std::span<std::uint8_t>;
    auto width() const noexcept -> int;
    auto height() const noexcept -> int;
    auto fps() const noexcept -> int;
    auto bitrate() const noexcept -> int;
    auto gop_size() const noexcept -> int;
    auto pixel_format() const noexcept -> int;
    auto codec() const noexcept -> int;

    auto decode() const -> std::vector<std::uint8_t>;
    static auto from_file(const std::filesystem::path &path) -> video_format_t;
    static auto encode(const std::span<std::uint8_t> file_contents,
                       std::string_view filename) -> video_format_t;

  private:
    std::string filename_;
    std::vector<std::uint8_t> file_contents_;
    int width_, height_, fps_, bitrate_, gop_size_, pixel_format_, codec_;
};

class video_output_t {
  public:
    virtual ~video_output_t() noexcept {}
    virtual auto format_context() -> AVFormatContext * = 0;
};

class in_memory_video_output_t : public video_output_t {
  public:
    explicit in_memory_video_output_t(std::vector<std::uint8_t> &sink);
    auto get_video_contents() noexcept -> std::span<std::uint8_t>;
    ~in_memory_video_output_t() noexcept override;
    auto format_context() -> AVFormatContext * override;
    auto io_context() -> AVIOContext *;

  private:
    using rw_packet_callback_t = int (*)(void *, std::uint8_t *, int);
    AVIOContext *io_context_;
    AVFormatContext *format_context_;
    std::uint8_t *buffer_;
    std::int64_t offset_ = 0;
    std::vector<std::uint8_t> &sink_;
    // The buffer size is very important for performance. For protocols with
    // fixed blocksize it should be set to this blocksize. For others a typical
    // size is a cache page, e.g. 4kb.
    constexpr static std::size_t buffer_size_ = 4096;

    // @brief Callbacks for `avio_alloc_context()`.
    // @param opaque Pointer back to `this`.
    // @note These callbacks are static because they are called from C code.
    // @see `avio_alloc_context()`
    static int write_packet(void *opaque, std::uint8_t *buf,
                            int buf_size) noexcept;

    static std::int64_t seek(void *opaque, std::int64_t offset,
                             int whence) noexcept;
};

class file_video_output_t : public video_output_t {
  public:
    explicit file_video_output_t(const std::filesystem::path &filename);
    auto get_file_contents() const noexcept -> std::span<std::uint8_t>;
    ~file_video_output_t() noexcept override;
    auto format_context() -> AVFormatContext * override;

    file_video_output_t() = delete;
    file_video_output_t(file_video_output_t&&) noexcept;
    file_video_output_t& operator=(file_video_output_t&&) noexcept;
    file_video_output_t(const file_video_output_t&) = delete;
    file_video_output_t& operator=(const file_video_output_t&) = delete;

  private:
    std::filesystem::path filename_;
    AVIOContext* io_context_;
    AVFormatContext* format_context_;
};

class encoding_session_t {
  public:
    void encode();

  private:
    AVPacket *packet_;
    AVStream *video_stream_;
    int video_stream_index_;
    AVCodecContext *codec_context_;
    AVFormatContext *format_context_;
};

class encoder_t {
  public:
    class builder_t {
      public:
        auto set_qr_codes(std::shared_ptr<std::vector<qrcodegen::QrCode>>
                              qr_codes) noexcept -> builder_t &;

        /// @brief Set the video format to be encoded.
        /// @param video_format short name of the video format (e.g., "mp4")
        /// @see `$ ffmpeg -formats` for full list of supported formats on the
        /// host system
        /// @return builder object reference
        auto
        set_video_format(std::string_view video_format) noexcept -> builder_t &;

        auto set_border_size(const size_t border_size) noexcept -> builder_t &;
        auto set_scale(const size_t scale) noexcept -> builder_t &;
        auto set_fps(const int fps) noexcept -> builder_t &;

        [[nodiscard]] auto video_format() const noexcept -> std::string_view;
        [[nodiscard]] auto qr_codes() const noexcept
            -> std::shared_ptr<std::vector<qrcodegen::QrCode>>;
        [[nodiscard]] auto build() const -> encoder_t;

      private:
        std::shared_ptr<std::vector<qrcodegen::QrCode>> qr_codes_;
        std::string video_format_;
        size_t scale_, border_size_;
        int fps_;
    };
    static auto builder() -> builder_t { return builder_t{}; }

    auto encode(std::unique_ptr<video_output_t> destination) -> void;

    encoder_t(const encoder_t &) = delete;
    encoder_t &operator=(const encoder_t &) = delete;
    encoder_t(encoder_t &&) noexcept = default;
    encoder_t &operator=(encoder_t &&) noexcept = default;
    ~encoder_t() noexcept = default;

  private:
    explicit encoder_t(std::shared_ptr<std::vector<qrcodegen::QrCode>> qr_codes,
                       std::string video_format, const size_t scale,
                       const size_t border_size, const int fps = 20) noexcept
        : qr_codes_{qr_codes}, video_format_{video_format}, scale_{scale},
          border_size_{border_size}, fps_{fps} {}
    auto calculate_dimensions() const -> size_t;
    std::shared_ptr<std::vector<qrcodegen::QrCode>> qr_codes_;
    std::string video_format_;
    size_t scale_, border_size_;
    int fps_;
    constexpr static int gop_size_ = 12;
    constexpr static int bitrate_ = 400000;
};

} // namespace net_zelcon::plain_sight

#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_ENCODER_H