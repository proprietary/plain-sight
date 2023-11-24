#ifndef _INCLUDE_NET_ZELCON_PLAIN_SIGHT_DECODER_H_

#include "plain_sight/util.h"
#include <concepts>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <istream>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
}

namespace net_zelcon::plain_sight {

void decode(std::ostream &dst, const std::istream &video);

void decode(std::vector<std::uint8_t> &dst, const std::istream &video);

void decode(std::vector<std::uint8_t> &dst,
            const std::filesystem::path video_path);

class video_input_t {
  public:
    virtual ~video_input_t() noexcept {}
    virtual auto format_context() const -> AVFormatContext * = 0;
};

class in_memory_video_input_t : public video_input_t {
  public:
    explicit in_memory_video_input_t(std::span<std::uint8_t> video);
    ~in_memory_video_input_t() noexcept override;
    auto format_context() const -> AVFormatContext * override;

  private:
    const std::span<std::uint8_t> video_;
    size_t offset_ = 0;
    AVIOContext *io_context_;
    std::uint8_t *buffer_;
    constexpr static std::size_t buffer_size_ = 4096;
    AVFormatContext* format_context_;

    /// @brief Callback for `avio_alloc_context`.
    /// @details This is a static function because it is a callback for a C API.
    /// A function for refilling the buffer, may be NULL. For stream protocols,
    /// must never return 0 but rather a proper AVERROR code.
    /// @param opaque aka `this` (the `in_memory_video_input_t` instance)
    /// @param buf
    /// @param buf_size
    /// @return bytes read or AVERROR_EOF
    static int read_packet(void *opaque, std::uint8_t *buf, int buf_size);

    /// @brief Callback for `avio_alloc_context`.
    /// @details A function for seeking to specified byte position, may be NULL.
    /// @param opaque aka `this` (the `in_memory_video_input_t` instance)
    /// @param offset
    /// @param whence
    /// @return
    static int64_t seek(void *opaque, int64_t offset, int whence);
};

class file_video_input_t : public video_input_t {
  public:
    explicit file_video_input_t(const std::filesystem::path &video_path);
    ~file_video_input_t() noexcept override;
    auto format_context() const -> AVFormatContext * override;

  private:
    std::filesystem::path video_path_;
    libav_ptr_t<AVFormatContext, avformat_close_input> format_context_;
};

class decoder_t {
  public:
    void decode(std::vector<std::uint8_t> &dst,
                std::unique_ptr<video_input_t> src);
};

template <typename OutputIt>
    requires std::output_iterator<OutputIt, std::uint8_t>
void copy_img_buf(OutputIt dst, const AVFrame *frame);

} // namespace net_zelcon::plain_sight

#define _INCLUDE_NET_ZELCON_PLAIN_SIGHT_DECODER_H_
#endif // _INCLUDE_NET_ZELCON_PLAIN_SIGHT_DECODER_H_