// g++ object.cc -I ../include -lgtest -L ./ -lnax -pthread -ohldtest -std=c++11 -fpermissive -g3 -fstack-protector-all -fstack-check=specific
#include <gtest/gtest.h>
#include <string>

#include "object.h"

struct context {
    int idx;
    char buffer[64];
};

void STDCALL ctxunloader(objhld_t hld, void *udata)
{

}

TEST(DoNormalTest, NormalTest)
{
    struct objcreator creator;
    creator.known = -1;
    creator.size = sizeof(struct context);
    creator.initializer = NULL;
    creator.unloader = &ctxunloader;
    creator.context = NULL;
    creator.ctxsize = 0;

    objhld_t hld;
    nsp_status_t status = objallo4(&creator, &hld);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    EXPECT_EQ(hld, (objhld_t)1);

    struct context *pctx = (struct context *)objrefr(hld);
    EXPECT_TRUE(pctx != NULL);
    pctx->idx = 101;
    strcpy(pctx->buffer, "hello world");

    struct context *pctx2;
    unsigned int size = objrefr2(hld, (void **)&pctx2);
    EXPECT_EQ(size, sizeof(context));
    EXPECT_EQ(pctx2, pctx);
    EXPECT_EQ(pctx2->idx, 101);
    EXPECT_EQ(0, strcmp(pctx2->buffer, "hello world"));

    objclos(hld);
    pctx = (struct context *)objrefr(hld);
    EXPECT_EQ(NULL, pctx);
    size = objrefr2(hld, (void **)&pctx2);
    EXPECT_EQ((unsigned int)-1, size);
    EXPECT_EQ(NULL, pctx2);

    objdefr(hld);
    objdefr(hld);
}

TEST(DoNormalSpecifyHld, NormalSpecifyHld)
{
    struct objcreator creator;
    creator.known = 5;
    creator.size = sizeof(struct context);
    creator.initializer = NULL;
    creator.unloader = &ctxunloader;
    creator.context = NULL;
    creator.ctxsize = 0;

    objhld_t hld;
    nsp_status_t status = objallo4(&creator, &hld);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    EXPECT_EQ(hld, (objhld_t)5);

    objhld_t hldtest;
    status = objallo4(&creator, &hldtest);
    EXPECT_EQ(status, -EEXIST);

    objclos(5);
}

TEST(DoRefNonExist, RefNonExist)
{
    struct context *pctx = (struct context *)objrefr(10);
    EXPECT_TRUE(pctx == NULL);

    unsigned int size = objrefr2(10, (void **)&pctx);
    EXPECT_EQ(size, (unsigned int)-1);
}

TEST(DoTestReff, TestReff)
{
    struct objcreator creator;
    creator.known = 0;
    creator.size = sizeof(struct context);
    creator.initializer = NULL;
    creator.unloader = &ctxunloader;
    creator.context = NULL;
    creator.ctxsize = 0;

    objhld_t hld;
    nsp_status_t status = objallo4(&creator, &hld);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    EXPECT_GT(hld, 0);

    struct context *pctx = (struct context *)objreff(hld);
    EXPECT_TRUE(pctx != NULL);

    struct context *pctx2 = (struct context *)objreff(hld);
    EXPECT_TRUE(pctx2 == NULL);

    objdefr(hld);
}

TEST(DoZeroContextSize, ZeroContextSize)
{
    struct objcreator creator;
    creator.known = 0;
    creator.size = 0;
    creator.initializer = NULL;
    creator.unloader = &ctxunloader;
    creator.context = NULL;
    creator.ctxsize = 0;

    objhld_t hld;
    nsp_status_t status = objallo4(&creator, &hld);
    EXPECT_EQ(status, -EINVAL);

#if 0
    creator.size = 0x7ffffffe;
    status = objallo4(&creator, &hld);
    EXPECT_EQ(status, -ENOMEM);
#endif
}

int STDCALL initializer(void *udata, const void *ctx, int ctxcb)
{
    struct context *objctx = (struct context *)udata;
    objctx->idx = *(int *)ctx;
    EXPECT_EQ(ctxcb, sizeof(int));

    strcpy(objctx->buffer, "abcd#1234");
    return 0;
}

TEST(DoTestInitializer, TestInitializer)
{
    int number = 10001;
    struct objcreator creator;
    creator.known = 0;
    creator.size = sizeof(struct context);
    creator.initializer = &initializer;
    creator.unloader = NULL;
    creator.context = &number;
    creator.ctxsize = sizeof(number);

    objhld_t hld;
    nsp_status_t status = objallo4(&creator, &hld);
    EXPECT_EQ(status, NSP_STATUS_SUCCESSFUL);
    EXPECT_GT(hld, 0);

    struct context *pctx = (struct context *)objrefr(hld);
    EXPECT_TRUE(pctx != NULL);
    EXPECT_EQ(pctx->idx, 10001);
    EXPECT_EQ(0, strcmp(pctx->buffer, "abcd#1234"));

    objdefr(hld);
    objclos(hld);
}
