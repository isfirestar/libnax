// g++ clock.cc -I ../include -lgtest -L ./ -lnax -pthread -oclock -std=c++11 -fpermissive -g3 -fstack-protector-all -fstack-check=specific
#include <gtest/gtest.h>

#include "clock.h"

TEST(SimpleTestClockFunction, DoClockTest)
{
    uint64_t now = clock_monotonic_raw();
    EXPECT_GT(now, 0);
    now = clock_monotonic();
    EXPECT_GT(now, 0);
    now = clock_epoch();
    EXPECT_GT(now, 0);
}
