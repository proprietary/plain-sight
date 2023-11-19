#include <gtest/gtest.h>

#include "plain_sight/qr_codes.h"
#include "plain_sight/util.h"
#include "plain_sight/video_generator.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>

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
  // generate video
  net_zelcon::plain_sight::write_qr_codes(qr_codes);
  // check output file
  std::filesystem::path output_path{"/tmp/output.mp4"};
  // check file size
  ASSERT_TRUE(std::filesystem::exists(output_path));
  ASSERT_GT(std::filesystem::file_size(output_path), 0);
}