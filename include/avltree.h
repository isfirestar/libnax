#if !defined (_AVLTREE_HEADER_ANDERSON_20120216)
#define _AVLTREE_HEADER_ANDERSON_20120216

#include "compiler.h"

/* avl search tree, by neo-anderson 2012-05-05 copyright(C) shunwang Co,.Ltd*/
struct avltree_node_t {
    struct avltree_node_t *lchild, *rchild; /* 分别指向左右子树 */
    int height; /* 树的高度 */
} __POSIX_TYPE_ALIGNED__;

/**
 *	结构类型重声明
 */
typedef struct avltree_node_t TREENODE_T, *PTREENODE, *TREEROOT;

/**
 *	节点数据对比例程
 *
 *	left 用于对比的左节点， 数据类型强制转化为struct avltree_node_t *后可用
 *	fight 用于对比的右节点， 数据类型强制转化为struct avltree_node_t *后可用
 *
 *	左节点大于右节点， 返回指定 1
 *	左节点小于右节点， 返回指定 -1
 *	左右节点相等，     返回指定 0
 */
typedef int( *compare_routine)(const void *left, const void *right);
typedef compare_routine avlcompare_t;

#define avl_simple_compare(left, right, field)   \
    ( ((left)->field > (right)->field) ? (1) : ( ((left)->field < (right)->field) ? (-1) : (0) ) )

#define avl_type_compare(type, leaf, field, left, right) \
    avl_simple_compare(container_of(left, type, leaf), container_of(right, type, leaf), field)

/**
 *  向根为tree的AVL树插入数据。
 *
 *  tree指向插入数据前AVL树的根。
 *  node指向包含待插入数据的avltree_node_t节点。
 *  compare指向节点之间的比较函数，由用户定义。
 *
 *  返回插入数据后AVL树的根。
 *
 *	数据结构过程使用参数传入指针直接钩链， 而没有进行深拷贝操作， 需要主调函数保证节点指针在调用 avlremove 之前的有效性
 */
PORTABLEAPI(struct avltree_node_t *) avlinsert(struct avltree_node_t *tree, struct avltree_node_t *node,
        int( *compare)(const void *, const void *));
/**
 *  从根为tree的AVL树删除数据。
 *
 *  tree指向删除数据前AVL树的根。
 *  node指向包含待匹配数据的avltree_node_t节点。
 *  rmnode为一个二次指针，删除成功时*rmnode存放的是被删除节点指针，失败则为NULL。
 *  compare指向节点之间的比较函数，由用户定义。
 *
 *  返回删除数据后AVL树的根。
 */
PORTABLEAPI(struct avltree_node_t *) avlremove(struct avltree_node_t *tree, struct avltree_node_t *node,
        struct avltree_node_t **rmnode,
        int( *compare)(const void *, const void *));
/**
 *  从根为tree的AVL树中搜索数据。
 *
 *  tree指向AVL树的根。
 *  node指向包含待匹配数据的avltree_node_t节点。
 *  compare指向节点之间的比较函数，由用户定义。
 *
 *  返回匹配数据节点，或者NULL。
 */
PORTABLEAPI(struct avltree_node_t *) avlsearch(struct avltree_node_t *tree, struct avltree_node_t *node,
        int( *compare)(const void *, const void *));
/**
 *  从根为tree的AVL树中搜索最小数据节点。
 *
 *  返回最小数据节点，或者NULL（空树）。
 */
PORTABLEAPI(struct avltree_node_t *) avlgetmin(struct avltree_node_t *tree);

/**
 *  从根为tree的AVL树中搜索最大数据节点。
 *
 *  返回最大数据节点，或者NULL（空树）。
 */
PORTABLEAPI(struct avltree_node_t *) avlgetmax(struct avltree_node_t *tree);


#endif /*_AVLTREE_HEADER_ANDERSON_20120216*/
