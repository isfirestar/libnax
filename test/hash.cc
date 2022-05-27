// g++ hash.cc -I ../include -lgtest -L ./ -lnax -pthread -ohash -std=c++11 -fpermissive -g3 -fstack-protector-all -fstack-check=specific
#include <gtest/gtest.h>

#include "ifos.h"
#include "hash.h"

TEST(DoTestDESEncrypt, TestDESEncrypt)
{
    char origin[64], encrypt[64], decrypt[64];

    for (int i = 0; i < sizeof(origin); i++) {
        origin[i] = ifos_random(1, 254);
    }

    int fr = DES__encrypt(origin, sizeof(origin), NULL, encrypt);
    EXPECT_NE(0, memcmp(origin, encrypt, sizeof(origin)));
    EXPECT_EQ(sizeof(origin), fr);

    fr = DES__decrypt(encrypt, sizeof(encrypt), NULL, decrypt);
    EXPECT_NE(0, memcmp(encrypt, decrypt, sizeof(encrypt)));
    EXPECT_EQ(0, memcmp(decrypt, origin, sizeof(origin)));
}

TEST(DoTestDESEncryptWithKey, TestDESEncryptWithKey)
{
    char origin[64], encrypt[64], decrypt[64];
    char key[8];

    for (int i = 0; i < sizeof(origin); i++) {
        origin[i] = ifos_random(1, 254);
    }

    for (int i = 0; i < sizeof(key); i++) {
        key[i] = ifos_random(1, 127);
    }

    int fr = DES__encrypt(origin, sizeof(origin), key, encrypt);
    EXPECT_EQ(sizeof(origin), fr);
    EXPECT_NE(0, memcmp(origin, encrypt, sizeof(origin)));

    fr = DES__decrypt(encrypt, sizeof(encrypt), key, decrypt);
    EXPECT_EQ(sizeof(origin), fr);
    EXPECT_NE(0, memcmp(encrypt, decrypt, sizeof(encrypt)));
    EXPECT_EQ(0, memcmp(decrypt, origin, sizeof(origin)));
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
