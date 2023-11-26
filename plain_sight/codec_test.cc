#include <gtest/gtest.h>

#include "plain_sight/codec.h"
#include "plain_sight/util.h"

#include <cstdint>
#include <filesystem>
#include <vector>

using namespace net_zelcon::plain_sight;

TEST(CodecEndToEndTest, Filesystem) {
    // load some file
    std::vector<std::uint8_t> some_file;
    read_file(some_file, std::filesystem::path{"/usr/include/errno.h"});
    ASSERT_GT(some_file.size(), 0);
    // encode it
    std::filesystem::path encoded_path{"/tmp/output.mp4"};
    encode_file(encoded_path, some_file);
    ASSERT_TRUE(std::filesystem::exists(encoded_path));
    ASSERT_GT(std::filesystem::file_size(encoded_path), 0);
    // decode it
    std::vector<std::uint8_t> decoded;
    decode_file(decoded, encoded_path);
    ASSERT_GT(decoded.size(), 0);
    // check that it's the same
    ASSERT_EQ(some_file.size(), decoded.size());
    ASSERT_EQ(some_file, decoded);
}

TEST(EncodingTest, InMemoryVsFileFidelity) {
    std::vector<std::uint8_t> some_file;
    read_file(some_file, std::filesystem::path{"/usr/include/errno.h"});
    std::filesystem::path encoded_path{"/tmp/output.mp4"};
    encode_file(encoded_path, some_file);
    std::vector<std::uint8_t> encoded_file_bytes;
    read_file(encoded_file_bytes, encoded_path);
    std::vector<std::uint8_t> encoded_in_memory;
    encode_raw_data(encoded_in_memory, some_file);
    ASSERT_EQ(std::filesystem::file_size(encoded_path),
              encoded_in_memory.size());
    ASSERT_EQ(encoded_file_bytes, encoded_in_memory);
}

TEST(CodecEndToEndTest, InMemory) {
    // load some file
    std::vector<std::uint8_t> some_file;
    read_file(some_file, std::filesystem::path{"/usr/include/errno.h"});
    ASSERT_GT(some_file.size(), 0);
    // encode it
    std::vector<std::uint8_t> encoded;
    encode_raw_data(encoded, some_file);
    ASSERT_GT(encoded.size(), 1);
    // decode it
    std::vector<std::uint8_t> decoded;
    decode_raw_data(decoded,
                    std::span<std::uint8_t>(encoded.data(), encoded.size()));
    ASSERT_GT(decoded.size(), 1);
    // check that it's the same
    ASSERT_EQ(some_file.size(), decoded.size());
    ASSERT_EQ(some_file, decoded);
}