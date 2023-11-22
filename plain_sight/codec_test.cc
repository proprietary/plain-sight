#include <gtest/gtest.h>

#include "plain_sight/codec.h"
#include "plain_sight/util.h"

#include <cstdint>
#include <filesystem>
#include <vector>

using namespace net_zelcon::plain_sight;

TEST(CodecTest, EndToEnd) {
    // load some file
    std::vector<std::uint8_t> some_file;
    read_file(some_file, std::filesystem::path{"/usr/share/dict/words"});
    ASSERT_GT(some_file.size(), 0);
    // encode it
    std::vector<std::uint8_t> encoded;
    encode_raw_data(encoded, some_file);
    ASSERT_GT(encoded.size(), 0);
    // decode it
    std::vector<std::uint8_t> decoded;
    decode_raw_data(decoded,
                    std::span<std::uint8_t>(encoded.data(), encoded.size()));
    ASSERT_GT(decoded.size(), 0);
    // check that it's the same
    ASSERT_EQ(some_file.size(), decoded.size());
    ASSERT_EQ(some_file, decoded);
}