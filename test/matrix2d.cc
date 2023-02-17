#include "matrix2d.h"

#include <gtest/gtest.h>

static void iter_fill_integer(matrix2d_ele_t *ele, unsigned int ele_index, void *args)
{
    int *p = (int *)args;
    *ele = p[ele_index];
}

TEST(DoTestMatrixAdd, TestMatrixAdd)
{
    matrix2d_pt m_left, m_right, m_sum, m_verify;
    static int d_left[2][3] = { 
        { 1, 2, 3 }, 
        { 4, 5, 6 }, 
    };
    static int d_right[2][3] = {
        { 7, 8, 9 }, 
        { 9, 8, 7 }, 
    };
    static int d_verify[2][3] = {
        { 8, 10,12 },
        { 13,13,13 },
    };

    m_left = matrix2d_alloc(2,3);
    ASSERT_TRUE(NULL != m_left);
    matrix2d_iterate_element(m_left, &iter_fill_integer, d_left);

    m_right = matrix2d_alloc(2,3);
    ASSERT_TRUE(NULL != m_right);
    matrix2d_iterate_element(m_right, &iter_fill_integer, d_right);

    m_verify = matrix2d_alloc(2,3);
    ASSERT_TRUE(NULL != m_verify);
    matrix2d_iterate_element(m_verify, &iter_fill_integer, d_verify);

    m_sum = matrix2d_add(m_left, m_right, NULL);
    ASSERT_TRUE(NULL != m_sum);

    EXPECT_TRUE(matrix2d_equal(m_sum, m_verify));
    
    matrix2d_free(m_sum);
    matrix2d_free(m_left);
    matrix2d_free(m_right);
    matrix2d_free(m_verify);
}

TEST(DoTestMatrixMul, TestMatrixMul)
{
    return;
    matrix2d_pt m_left, m_right, m_product, m_verify, m_identity, m_product_with_identity;
    static int d_left[6][3] = { 
        { 2, 3, 4 },
        { 5, 3, 5 },
        { 6, 2, 3 },
        { 7, 9, 9 },
        { 1, 4, 7 },
        { 2, 5, 9 },
    };
    static int d_right[3][4] = {
        { 1, 2, 7, 9 },
        { 3, 4, 6, 8 },
        { 5, 6, 3, 2 },
    };
    static int d_product[2][3] = {
        { 21, 27, 33 },
        { 57, 72, 87 },
    };

    m_left = matrix2d_alloc(6,3);
    ASSERT_TRUE(NULL != m_left);
    matrix2d_iterate_element(m_left, &iter_fill_integer, d_left);

    m_right = matrix2d_alloc(3,4);
    ASSERT_TRUE(NULL != m_right);
    matrix2d_iterate_element(m_right, &iter_fill_integer, d_right);

    m_verify = matrix2d_alloc(2,3);
    ASSERT_TRUE(NULL != m_verify);
    matrix2d_iterate_element(m_verify, &iter_fill_integer, d_product);

    m_product = matrix2d_mul(m_left, m_right, NULL, NULL);
    ASSERT_TRUE(NULL != m_product);
    EXPECT_TRUE(matrix2d_equal(m_product, m_verify));

    m_identity = matrix2d_make_identity(4);
    ASSERT_TRUE(NULL != m_identity);

    m_product_with_identity = matrix2d_mul(m_right, m_identity, NULL, NULL);
    ASSERT_TRUE(NULL != m_product_with_identity);
    EXPECT_TRUE(matrix2d_equal(m_product_with_identity, m_right));

    matrix2d_free(m_product);
    matrix2d_free(m_product_with_identity);
    matrix2d_free(m_left);
    matrix2d_free(m_right);
    matrix2d_free(m_identity);
    matrix2d_free(m_verify);
}

TEST(DoTestMatrixTransport, TestMatrixTransport)
{
    matrix2d_pt m, m_src, m_verify;
    static int d_src[6][3] = { 
        { 2, 3, 4 },
        { 5, 3, 5 },
        { 6, 2, 3 },
        { 7, 9, 9 },
        { 1, 4, 7 },
        { 2, 5, 9 },
    };
    static int d_verify[3][6] = {
        { 2, 5, 6, 7, 1, 2 },
        { 3, 3, 2, 9, 4, 5 },
        { 4, 5, 3, 9, 7, 9 },
    };
    
    m_src = matrix2d_alloc(6,3);
    ASSERT_TRUE(NULL != m_src);
    matrix2d_iterate_element(m_src, &iter_fill_integer, d_src);

    m_verify = matrix2d_alloc(3,6);
    ASSERT_TRUE(NULL != m_verify);
    matrix2d_iterate_element(m_verify, &iter_fill_integer, d_verify);

    m = matrix2d_transport(m_src);
    EXPECT_TRUE(m != NULL);
    EXPECT_TRUE(matrix2d_equal(m_verify, m));

    matrix2d_free(m_src);
    matrix2d_free(m);
    matrix2d_free(m_verify);
}
