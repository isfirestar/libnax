// g++ threading.cc -I ../include -lgtest -L ./ -lnax -pthread -othreading -std=c++11 -fpermissive -g3 -fstack-protector-all -fstack-check=specific
#include <gtest/gtest.h>

#include "threading.h"
#include "hash.h"



int main(int argc, char *argv[])
{
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
