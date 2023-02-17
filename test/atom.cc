#include "atom.h"

#include <gtest/gtest.h>

TEST(DoBaseFunc, BaseFunc)
{
    long value = 0;

    EXPECT_EQ(atom_get(&value), 0);
    EXPECT_EQ(atom_get64(&value), 0);

    atom_set(&value, 1);
    EXPECT_EQ(atom_get(&value), 1);
    atom_set64(&value, 2);
    EXPECT_EQ(atom_get(&value), 2);

    atom_increase(&value, 1);
    EXPECT_EQ(atom_get(&value), 3);
    atom_increase64(&value, 1);
    EXPECT_EQ(atom_get(&value), 4);

    atom_decrease(&value, 1);
    EXPECT_EQ(atom_get(&value), 3);
    atom_decrease64(&value, 1);
    EXPECT_EQ(atom_get(&value), 2);

    EXPECT_EQ(atom_exchange(&value, 10), 2);
    EXPECT_EQ(atom_exchange64(&value, 20), 10);

    EXPECT_EQ(atom_compare_exchange(&value, 20, 10), 20);
    EXPECT_EQ(atom_compare_exchange64(&value, 20, 10), 10);
}

TEST(DoPointerFunc, PointerFunc)
{
    long value = 0;
    long *pvalue = &value;
    EXPECT_EQ(atom_exchange_pointer(&pvalue, nullptr), &value);
    EXPECT_EQ(atom_compare_exchange_pointer(&pvalue, nullptr, &value), nullptr);
    EXPECT_EQ(atom_compare_exchange_pointer(&pvalue, nullptr, &value), &value);
}

// g++ atom.cpp -g3 -oatom -lgtest -pthread -std=c++11
// int main(int argc,char *argv[])
// {
//     testing::InitGoogleTest(&argc, argv);
//     return RUN_ALL_TESTS();
// }
