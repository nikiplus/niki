#include <compare>
#include <gtest/gtest.h>


TEST(EnvTest, CheckCpp20) {
    auto res = (1 <=> 2);
    EXPECT_TRUE(res < 0);
}