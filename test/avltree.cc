#include "avltree.h"

#include <pthread.h>
#include <time.h>

#include <gtest/gtest.h>

struct node
{
    int a;
    char unaligned[9];
    struct avltree_node_t  entry;
};

static constexpr int maxitem = 100000;

static int node_compare(const void *left, const void *right)
{
    struct node *lnode = container_of(left, struct node, entry);
    struct node *rnode = container_of(right, struct node, entry);

    if (lnode->a > rnode->a) {
        return 1;
    }

    if (lnode->a < rnode->a) {
        return -1;
    }

    return 0;
}

TEST(DoInsertTest, DoInsert)
{
    struct avltree_node_t *root = nullptr;

    struct node mynode = { 1, "abcd" };

    // error parameter shall return a nil-pointer
    EXPECT_EQ(avlinsert(root, nullptr, &node_compare), nullptr);
    EXPECT_EQ(avlinsert(root, &mynode.entry, nullptr), nullptr);

    // normal insert
    root = avlinsert(root, &mynode.entry, &node_compare);
    EXPECT_EQ(root, &mynode.entry);
}

class CAVLTreeTest : public testing::Test
{
public:
    CAVLTreeTest()
    {
        nodelist = new (std::nothrow) struct node[maxitem];
        srandom(time(0));
    }

    virtual ~CAVLTreeTest()
    {
        delete []nodelist;
    }

    virtual void SetUp() override final;
    virtual void TearDown() override final;

protected:
    struct avltree_node_t *maintest = nullptr;
    struct node *nodelist = nullptr;
};

void CAVLTreeTest::SetUp()
{
    // insert 100,000 items into tree container first
    for (int i = 0; i < maxitem; i++) {
        nodelist[i].a = i;
        maintest = avlinsert(maintest, &nodelist[i].entry, &node_compare);
        EXPECT_TRUE( (maintest != nullptr) );
    }
}

void CAVLTreeTest::TearDown()
{
    struct node search;
    struct avltree_node_t *rmentry;

    // remove entire list
    for (int i = 0; i < maxitem; i++) {
        search.a = i;
        maintest = avlremove(maintest, &search.entry, &rmentry, &node_compare);
        ASSERT_TRUE(rmentry != nullptr);

        // removed node address MUST equal to origin
        struct node *rmnode = container_of(rmentry, struct node, entry);
        EXPECT_EQ(rmnode, &nodelist[rmnode->a]);
    }
}

TEST_F(CAVLTreeTest, SearchItemTest)
{
    struct node search;

    // test all element shall be found in container
    for (int i = 0; i < maxitem; i++) {
        search.a = i;
        avltree_node_t *found = avlsearch(maintest, &search.entry, &node_compare);
        ASSERT_TRUE( (found != nullptr) );

        struct node *thenode = container_of(found, struct node, entry);
        EXPECT_EQ(thenode->a, i);
    }

    // test random element shall be found in container
    for (int i = 0; i < maxitem; i++) {
        search.a = random() % maxitem;
        avltree_node_t *found = avlsearch(maintest, &search.entry, &node_compare);
        EXPECT_TRUE( (found != nullptr) );

        struct node *thenode = container_of(found, struct node, entry);
        EXPECT_EQ(thenode->a, search.a );
    }

    struct avltree_node_t *minLimitItem = avlgetmin(maintest);
    struct avltree_node_t *maxLimitItem = avlgetmax(maintest);

    EXPECT_TRUE((minLimitItem != nullptr) && (maxLimitItem != nullptr));
    EXPECT_EQ( container_of(minLimitItem, struct node, entry), &nodelist[0] );
    EXPECT_EQ( container_of(maxLimitItem, struct node, entry), &nodelist[99999] );
}

