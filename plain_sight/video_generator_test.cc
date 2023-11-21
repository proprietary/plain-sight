#include <gtest/gtest.h>

#include "plain_sight/decoder.h"
#include "plain_sight/qr_codes.h"
#include "plain_sight/util.h"
#include "plain_sight/video_generator.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
}

TEST(VideoGeneratorTest, SmokeTest) { ASSERT_EQ(true, true); }

TEST(VideoGeneratorTest, CheckFile) {
    std::filesystem::path path{"/usr/include/pthread.h"};
    ASSERT_TRUE(std::filesystem::exists(path));
}

TEST(VideoGeneratorTest, FromSomeFile) {
    // read file to bytes
    std::vector<std::uint8_t> bytes;
    std::filesystem::path path{"/usr/include/pthread.h"};
    ASSERT_TRUE(std::filesystem::exists(path));
    net_zelcon::plain_sight::read_file(bytes, path);
    // split bytes into frames
    std::vector<qrcodegen::QrCode> qr_codes =
        net_zelcon::plain_sight::split_frames(bytes);
    // output file
    std::filesystem::path output_path{"/tmp/output.mp4"};
    // generate video
    net_zelcon::plain_sight::write_qr_codes(qr_codes, output_path);
    // check file size
    ASSERT_TRUE(std::filesystem::exists(output_path));
    ASSERT_GT(std::filesystem::file_size(output_path), 0);
    // check if video has frames using libav
    AVFormatContext *format_context = nullptr;
    int result = avformat_open_input(&format_context, output_path.c_str(),
                                     nullptr, nullptr);
    ASSERT_EQ(result, 0);
    result = avformat_find_stream_info(format_context, nullptr);
    ASSERT_EQ(result, 0);
    int num_frames = format_context->streams[0]->nb_frames;
    ASSERT_GT(num_frames, 0);
    // teardown
    avformat_close_input(&format_context);
    std::filesystem::remove(output_path);
}

TEST(VideoGeneratorTest, EndToEnd) {
    std::vector<std::uint8_t> bytes;
    std::filesystem::path path{"/usr/include/pthread.h"};
    ASSERT_TRUE(std::filesystem::exists(path));
    net_zelcon::plain_sight::read_file(bytes, path);
    std::filesystem::path output_path{"/tmp/output.mp4"};
    auto qr_codes = net_zelcon::plain_sight::split_frames(bytes);
    net_zelcon::plain_sight::write_qr_codes(qr_codes, output_path);
    ASSERT_GT(std::filesystem::file_size(output_path), 0);
    std::vector<std::uint8_t> decoded_bytes;
    net_zelcon::plain_sight::decode(decoded_bytes, output_path);
    ASSERT_EQ(decoded_bytes.size(), bytes.size());
    ASSERT_EQ(decoded_bytes, bytes);
    // teardown
    std::filesystem::remove(output_path);
}