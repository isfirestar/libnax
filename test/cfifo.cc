// g++ cfifo.cc -I ../include -lgtest -L ./ -lnax -pthread -ocfifo -std=c++11 -fpermissive -g3 -fstack-protector-all -fstack-check=specific
#include <gtest/gtest.h>

#include "cfifo.h"

TEST(DoPutOverflow, PutOverfolwTest)
{
    unsigned char buffer[64] = { 0 };
    int numbers[32];

    struct ckfifo *fifo = ckfifo_init(buffer, sizeof(buffer));
    ASSERT_NE(fifo, NULL);

    for (int i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
        numbers[i] = 100 + i;
    }

    uint32_t n;
    for (int i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
        n = ckfifo_put(fifo, &numbers[i], sizeof(numbers[i]));
        EXPECT_EQ(n, i < 16 ? sizeof(int) : 0);
    }

    ckfifo_uninit(fifo);
}

TEST(DoCirclePut, CirclePutTest)
{
    unsigned char buffer[64] = { 0 };
    int numbers[32];

    struct ckfifo *fifo = ckfifo_init(buffer, sizeof(buffer));
    ASSERT_NE(fifo, NULL);

    for (int i = 0; i < sizeof(numbers) / sizeof(numbers[0]); i++) {
        numbers[i] = 100 + i;
    }

    uint32_t n;
    uint32_t len;
    for (int i = 0; i < ((sizeof(numbers) / sizeof(numbers[0])) >> 1); i++) {
        n = ckfifo_put(fifo, &numbers[i], sizeof(numbers[i]));
        EXPECT_EQ(n, sizeof(int));
        len = ckfifo_len(fifo);
        EXPECT_EQ(len, (i + 1) * sizeof(int));
    }

    for (int i = 0; i < ((sizeof(numbers) / sizeof(numbers[0])) >> 1); i++) {
        int value;
        n = ckfifo_get(fifo, &value, sizeof(value));
        EXPECT_EQ(n, sizeof(int));
        EXPECT_EQ(value, numbers[i]);
    }

    ckfifo_uninit(fifo);
}

int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
