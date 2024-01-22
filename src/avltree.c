#if !defined __PUBLIC_INTERNEL_DECLARE
#define __PUBLIC_INTERNEL_DECLARE
#endif

#include "avltree.h"

#if !defined (NULL)
#define NULL ((void *)0)
#endif /*!NULL*/

#if !defined MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif /*!MAX*/

#define HEIGHT(t) ((t) == NULL ? -1 : (t)->height)

static struct avltree_node_t *_avlsinglerotateleft(struct avltree_node_t *tree);
static struct avltree_node_t *_avlsinglerotateright(struct avltree_node_t *tree);
static struct avltree_node_t *_avldoublerotateleft(struct avltree_node_t *tree);
static struct avltree_node_t *_avldoublerotateright(struct avltree_node_t *tree);

PORTABLEIMPL(struct avltree_node_t * )
avlinsert(struct avltree_node_t *tree, struct avltree_node_t *node,
        int( *compare)(const void *, const void *))
{
    int ret;

    if (unlikely(!node || !compare)) {
        return NULL;
    }

    if (!tree) {
        node->lchild = NULL;
        node->rchild = NULL;
        node->height = 0;
        return node;
    }

    ret = compare(node, tree);
    if (ret < 0) {
        tree->lchild = avlinsert(tree->lchild, node, compare);
        if (HEIGHT(tree->lchild) - HEIGHT(tree->rchild) == 2) {
            ret = compare(node, tree->lchild);
            if (ret < 0) {
                tree = _avlsinglerotateleft(tree);
            } else if (ret > 0) {
                tree = _avldoublerotateleft(tree);
            }
        } else {
            tree->height = MAX(HEIGHT(tree->lchild), HEIGHT(tree->rchild)) + 1;
        }
    } else if (ret > 0) {
        tree->rchild = avlinsert(tree->rchild, node, compare);
        if (HEIGHT(tree->rchild) - HEIGHT(tree->lchild) == 2) {
            ret = compare(node, tree->rchild);
            if (ret < 0) {
                tree = _avldoublerotateright(tree);
            } else if (ret > 0) {
                tree = _avlsinglerotateright(tree);
            }
        } else {
            tree->height = MAX(HEIGHT(tree->lchild), HEIGHT(tree->rchild)) + 1;
        }
    }
    return tree;
}

PORTABLEIMPL(struct avltree_node_t * )
avlremove(struct avltree_node_t *tree, struct avltree_node_t *node, struct avltree_node_t **rmnode,
        int( *compare)(const void *, const void *))
{
    int ret;

    if (unlikely(!tree || !node || !compare)) {
        return NULL;
    }

    if (rmnode) {
        *rmnode = NULL;
    }

    ret = compare(node, tree);
    if (ret == 0) {
        struct avltree_node_t *tmpnode;
        if (tree->lchild != NULL && tree->rchild != NULL) {
            /* 2 child */
            tmpnode = avlgetmin(tree->rchild);
            tree->rchild = avlremove(tree->rchild, tmpnode, rmnode, compare);
            tmpnode->lchild = tree->lchild;
            tmpnode->rchild = tree->rchild;
            if (rmnode) {
                *rmnode = tree;
            }
            tree = tmpnode;
        } else {
            /* 1 or 0 child */
            if (tree->lchild) {
                tmpnode = tree->lchild;
            } else if (tree->rchild) {
                tmpnode = tree->rchild;
            } else {
                tmpnode = NULL;
            }
            if (rmnode) {
                *rmnode = tree;
            }
            return tmpnode;
        }
    } else if (ret < 0) {
        if (tree->lchild != NULL){
            tree->lchild = avlremove(tree->lchild, node, rmnode, compare);
        }
    } else {
        /* ret > 0 */
        if (tree->rchild != NULL) {
            tree->rchild = avlremove(tree->rchild, node, rmnode, compare);
        }
    }

    if (HEIGHT(tree->lchild) - HEIGHT(tree->rchild) == 2) {
        if (HEIGHT(tree->lchild->lchild) - HEIGHT(tree->rchild) == 1
                && HEIGHT(tree->lchild->rchild) - HEIGHT(tree->rchild) == 1) {
            /*
                    tree --> o
                    /
                   o
                  / \
                 o   o
             */
            tree = _avlsinglerotateleft(tree);
        } else if (HEIGHT(tree->lchild->lchild) - HEIGHT(tree->rchild) == 1) {
            /*
                    tree --> o
                    /
                   o
                  /
                 o
             */
            tree = _avlsinglerotateleft(tree);
        } else if (HEIGHT(tree->lchild->rchild) - HEIGHT(tree->rchild) == 1) {
            /*
                    tree --> o
                    /
                   o
                    \
                     o
             */
            tree = _avldoublerotateleft(tree);
        }
    } else if (HEIGHT(tree->rchild) - HEIGHT(tree->lchild) == 2) {
        if (HEIGHT(tree->rchild->rchild) - HEIGHT(tree->lchild) == 1
                && HEIGHT(tree->rchild->lchild) - HEIGHT(tree->lchild) == 1) {
            /*
                    tree --> o
                     \
                      o
                     / \
                    o   o
             */
            tree = _avlsinglerotateright(tree);
        } else if (HEIGHT(tree->rchild->rchild) - HEIGHT(tree->lchild) == 1) {
            /*
                    tree --> o
                     \
                      o
                       \
                        o
             */
            tree = _avlsinglerotateright(tree);
        } else if (HEIGHT(tree->rchild->lchild) - HEIGHT(tree->lchild) == 1) {
            /*
                    tree --> o
                      \
                       o
                      /
                     o
             */
            tree = _avldoublerotateright(tree);
        }
    }
    tree->height = MAX(HEIGHT(tree->lchild), HEIGHT(tree->rchild)) + 1;
    return tree;
}

PORTABLEIMPL(struct avltree_node_t * ) avlsearch(struct avltree_node_t *tree, struct avltree_node_t *node,
        int( *compare)(const void *, const void *))
{
    int ret;
    struct avltree_node_t *t;

    if ( unlikely(!node || !compare) ) {
        return NULL;
    }

    t = tree;
    while (t != NULL) {
        ret = compare(node, t);
        if (ret < 0) {
            t = t->lchild;
        } else if (ret > 0) {
            t = t->rchild;
        } else {
            return t;
        }
    }
    return NULL;
}

PORTABLEIMPL(struct avltree_node_t * ) avlgetmin(struct avltree_node_t *tree)
{
    struct avltree_node_t *t;

    if (!tree) {
        return NULL;
    }

    t = tree;
    while (t->lchild != NULL) {
        t = t->lchild;
    }
    return t;
}

PORTABLEIMPL(struct avltree_node_t * ) avlgetmax(struct avltree_node_t *tree)
{
    struct avltree_node_t *t;

    if (!tree) {
        return NULL;
    }

    t = tree;
    while (t->rchild != NULL) {
        t = t->rchild;
    }
    return t;
}

static struct avltree_node_t *_avlsinglerotateleft(struct avltree_node_t *tree)
{
    struct avltree_node_t *newtree;

    /*
            tree --> 1                  newtree --> 2
            /                              / \
           2        ---->                 3   1
          /
         3
     */
    /* 2 */
    newtree = tree->lchild;
    /* 1 */
    tree->lchild = newtree->rchild;
    tree->height = MAX(HEIGHT(tree->lchild), HEIGHT(tree->rchild)) + 1;
    /* 2 */
    newtree->rchild = tree;
    newtree->height = MAX(HEIGHT(newtree->lchild), HEIGHT(newtree->rchild)) + 1;
    return newtree;
}

static struct avltree_node_t *_avlsinglerotateright(struct avltree_node_t *tree)
{
    struct avltree_node_t *newtree;

    /*
            tree --> 1                  newtree --> 2
             \                            / \
              2    ---->                 1   3
               \
                3
     */
    /* 2 */
    newtree = tree->rchild;
    /* 1 */
    tree->rchild = newtree->lchild;
    tree->height = MAX(HEIGHT(tree->lchild), HEIGHT(tree->rchild)) + 1;
    /* 2 */
    newtree->lchild = tree;
    newtree->height = MAX(HEIGHT(newtree->lchild), HEIGHT(newtree->rchild)) + 1;
    return newtree;
}

static struct avltree_node_t *_avldoublerotateleft(struct avltree_node_t *tree)
{
    struct avltree_node_t *newtree;

    /*
            tree --> 1                  newtree --> 3
            /                              / \
           2        ---->                 2   1
            \
             3
     */
    /* 3 */
    newtree = tree->lchild->rchild;
    /* 2 */
    tree->lchild->rchild = newtree->lchild;
    tree->lchild->height = MAX(HEIGHT(tree->lchild->lchild), HEIGHT(tree->lchild->rchild)) + 1;
    /* 3 */
    newtree->lchild = tree->lchild;
    /* 1 */
    tree->lchild = newtree->rchild;
    tree->height = MAX(HEIGHT(tree->lchild), HEIGHT(tree->rchild)) + 1;
    /* 3 */
    newtree->rchild = tree;
    newtree->height = MAX(HEIGHT(newtree->lchild), HEIGHT(newtree->rchild)) + 1;
    return newtree;
}

static struct avltree_node_t *_avldoublerotateright(struct avltree_node_t *tree)
{
    struct avltree_node_t *newtree;

    /*
            tree --> 1                  newtree --> 3
            \                            / \
             2    ---->                 1   2
            /
           3
     */
    /* 3 */
    newtree = tree->rchild->lchild;
    /* 2 */
    tree->rchild->lchild = newtree->rchild;
    tree->rchild->height = MAX(HEIGHT(tree->rchild->lchild), HEIGHT(tree->rchild->rchild)) + 1;
    /* 3 */
    newtree->rchild = tree->rchild;
    /* 1 */
    tree->rchild = newtree->lchild;
    tree->height = MAX(HEIGHT(tree->lchild), HEIGHT(tree->rchild)) + 1;
    /* 3 */
    newtree->lchild = tree;
    newtree->height = MAX(HEIGHT(newtree->lchild), HEIGHT(newtree->rchild)) + 1;
    return newtree;
}

struct avltree_node_t * avllowerbound(avltree_node_t *tree, avltree_node_t *node, int( *compare)(const void *, const void *))
{
    int ret;
    struct avltree_node_t *cursor;

    if (!tree) {
        return tree;
    }

    ret = compare(node, tree);
    if (ret < 0) {
        cursor = avllowerbound(tree->lchild, node, compare);
        return cursor ? cursor : tree;
    } else if (ret > 0) {
        return avllowerbound(tree->rchild, node, compare);
    } else {
        return tree;
    }
}

struct avltree_node_t * avlupperbound(avltree_node_t *tree, avltree_node_t *node, int( *compare)(const void *, const void *))
{
    int ret;
    struct avltree_node_t *cursor;

    if (!tree) {
        return tree;
    }

    ret = compare(node, tree);
    if (ret < 0) {
        cursor = avlupperbound(tree->lchild, node, compare);
        return cursor ? cursor : tree;
    }

    return avlupperbound(tree->rchild, node, compare);
}
