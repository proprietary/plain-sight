#include <gtest/gtest.h>

#include "plain_sight/qr_codes.h"

TEST(QrCodeGenerator, SplitRightNumber) {
    std::vector<std::uint8_t> data(10'000, '1');
    auto qr_codes = net_zelcon::plain_sight::split_frames(data);
    ASSERT_EQ(qr_codes.size(), 100);
    data = std::vector<std::uint8_t>(10'001, '1');
    qr_codes = net_zelcon::plain_sight::split_frames(data);
    ASSERT_EQ(qr_codes.size(), 101);
}