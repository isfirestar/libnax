#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include <windows.h>

#include "object.h"
#include "clist.h"
#include "avltree.h"
#include "zmalloc.h"

#define OBJ_HASHTABLE_SIZE   (256)

#define OBJSTAT_NORMAL      (0)
#define OBJSTAT_CLOSEWAIT   (1)

typedef CRITICAL_SECTION MUTEX_T;

#define LOCK    EnterCriticalSection
#define UNLOCK  LeaveCriticalSection

static void mutex_init(MUTEX_T *mutex)
{
	InitializeCriticalSection(mutex);
}

static void mutex_uninit(MUTEX_T *mutex)
{
	DeleteCriticalSection(mutex);
}

typedef struct _object_t
{
    struct avltree_node_t clash;    /* the hash conflict item container */
    objhld_t hld;
    int status;
    int refcnt;
    int size;
    objinitfn_t initializer;
    objuninitfn_t unloader;
    unsigned char body[0];
} object_t;

struct _object_manager
{
    struct avltree_node_t *roottab[OBJ_HASHTABLE_SIZE];
    objhld_t atoid;
    MUTEX_T mutex;
};

static int avl_compare_routine(const void *left, const void *right)
{
    const object_t *lobj, *robj;

    assert(left && right);

    lobj = containing_record(left, object_t, clash);
    robj = containing_record(right, object_t, clash);

    if (lobj->hld > robj->hld) {
        return 1;
    }

    if (lobj->hld < robj->hld) {
        return -1;
    }

    return 0;
}

static struct _object_manager g_objmgr;

/* table index = (hld % OBJ_HASHTABLE_SIZE) */
#define __hld2root(hld) ( likely(hld > 0) ?  &g_objmgr.roottab[ hld & (OBJ_HASHTABLE_SIZE - 1) ] : NULL )

static objhld_t __objtabinst(object_t *obj)
{
    object_t find;
    struct avltree_node_t **root, *target;

    root = NULL;

    LOCK(&g_objmgr.mutex);

    do {
        /* the creator acquire a knowned handle-id */
        if (INVALID_OBJHLD != obj->hld) {
            /* map root pointer from table */
            root = __hld2root(obj->hld);
            if (unlikely(!root)) {
                break;
            }

            find.hld = obj->hld;
            target = avlsearch(*root, &find.clash, &avl_compare_routine);
            if (target) {
                obj->hld = INVALID_OBJHLD;
                break;
            }
        } else {
            /* automatic increase handle number */
            obj->hld = ++g_objmgr.atoid;
            root = __hld2root(obj->hld);
            if (unlikely(!root)) {
                break;
            }
        }

        /* insert into hash list and using avl-binary-tree to handle the clash */
        *root = avlinsert(*root, &obj->clash, &avl_compare_routine);
    } while (0);

    UNLOCK(&g_objmgr.mutex);
    return obj->hld;
}

static int __objtabrmve(objhld_t hld, object_t **removed)
{
	object_t node;
    struct avltree_node_t **root, *rmnode;

    root = __hld2root(hld);
    if (unlikely(!root)) {
        return -1;
    }

    rmnode = NULL;

    node.hld = hld;
    *root = avlremove(*root, &node.clash, &rmnode, &avl_compare_routine);
    if (rmnode && removed) {
        *removed = containing_record(rmnode, object_t, clash);
    }

    return ((NULL == rmnode) ? (-1) : (0));
}

static object_t *__objtabsrch(const objhld_t hld)
{
    object_t node;
    struct avltree_node_t **root, *target;

    root = __hld2root(hld);
    if (unlikely(!root)) {
        return NULL;
    }

    node.hld = hld;
    target = avlsearch(*root, &node.clash, &avl_compare_routine);
    if (!target) {
        return NULL;
    }
    return containing_record(target, object_t, clash);
}

static void __objtagfree(object_t *target)
{
    /* release the object context and free target memory when object removed from table
        call the unload routine if not null */
    if ( target->unloader ) {
        target->unloader( target->hld, (void *)target->body );
    }
    zfree( target );
}

static void __objinit()
{
    memset(g_objmgr.roottab, 0, sizeof ( g_objmgr.roottab));
    g_objmgr.atoid = 0;
    mutex_init(&g_objmgr.mutex);
}

PORTABLEIMPL(void) objinit()
{
    static long inited = 0;
    if ( 1 == InterlockedIncrement(&inited)) {
        __objinit();
    } else{
        InterlockedDecrement(&inited);
    }
}

PORTABLEIMPL(void) objuninit()
{
    mutex_uninit(&g_objmgr.mutex);
}

PORTABLEIMPL(objhld_t) objallo3(const struct objcreator *creator)
{
    object_t *obj;

    if (creator->size <= 0 || (creator->context && !creator->ctxsize) || (!creator->context && creator->ctxsize) ) {
        return INVALID_OBJHLD;
    }

    objinit();

    obj = (object_t *)zmalloc(creator->size + sizeof(object_t));
    if (!obj) {
        return INVALID_OBJHLD;
    }

    obj->hld = creator->known;
    if (0 == obj->hld) {
        obj->hld = INVALID_OBJHLD;  /* treat zero specify to be a invalidate value */
    }
    obj->status = OBJSTAT_NORMAL;
    obj->refcnt = 0;
    obj->size = creator->size;
    memset(&obj->clash, 0, sizeof(obj->clash));
    obj->initializer = creator->initializer;
    obj->unloader = creator->unloader;
    memset(obj->body, 0, obj->size);

    if (obj->initializer) {
        if (obj->initializer((void *)obj->body, creator->context, creator->ctxsize) < 0) {
            obj->unloader(-1, (void *)obj->body);
            zfree(obj);
            return -1;
        }
    }

    if (unlikely(INVALID_OBJHLD == __objtabinst(obj))) {
        zfree(obj);
        return INVALID_OBJHLD;
    }

    return obj->hld;
}

PORTABLEIMPL(objhld_t) objallo(int size, objinitfn_t initializer, objuninitfn_t unloader, const void *context, unsigned int ctxsize)
{
    struct objcreator creator;

    creator.known = INVALID_OBJHLD;
    creator.size = size;
    creator.initializer = initializer;
    creator.unloader = unloader;
    creator.context = context;
    creator.ctxsize = ctxsize;
    return objallo3(&creator);
}

PORTABLEIMPL(objhld_t) objallo2(int size)
{
    return objallo(size, NULL, NULL, NULL, 0);
}

PORTABLEIMPL(void *) objrefr(objhld_t hld)
{
    object_t *obj;
    unsigned char *context;

    obj = NULL;
    context = NULL;

    LOCK(&g_objmgr.mutex);
    obj = __objtabsrch(hld);
    if (obj) {
		/* object status CLOSE_WAIT will be ignore for @objrefr operation */
        if (OBJSTAT_NORMAL == obj->status) {
            ++obj->refcnt;
            context = obj->body;
        }
    }
    UNLOCK(&g_objmgr.mutex);

    return (void *)context;
}

PORTABLEAPI(int) objrefr2(objhld_t hld, void **output)
{
    object_t *obj;
    int size;

    if (!output) {
        return -1;
    }

    obj = NULL;
    *output = NULL;
    size = -1;

    LOCK(&g_objmgr.mutex);
    obj = __objtabsrch(hld);
    if (obj) {
        /* object status CLOSE_WAIT will be ignore for @objrefr operation */
        if (OBJSTAT_NORMAL == obj->status) {
            ++obj->refcnt;
            *output = obj->body;
            size = obj->size;
        }
    }
    UNLOCK(&g_objmgr.mutex);

    return size;
}

PORTABLEIMPL(void *) objreff(objhld_t hld)
{
    object_t *obj;
    unsigned char *context;

    obj = NULL;
    context = NULL;

    LOCK(&g_objmgr.mutex);
    obj = __objtabsrch(hld);
    if (obj) {
        /* object status CLOSE_WAIT will be ignore for @objrefr operation */
        if (OBJSTAT_NORMAL == obj->status) {
            ++obj->refcnt;
            context = obj->body;

            /* change the object states to CLOSEWAIT immediately,
                so, other reference request will fail, object will be close when ref-count decrease equal to zero. */
            obj->status = OBJSTAT_CLOSEWAIT;
        }
    }
    UNLOCK(&g_objmgr.mutex);

    return (void *)context;
}

PORTABLEIMPL(void) objdefr(objhld_t hld)
{
    object_t *obj, *removed;

    obj = NULL;
    removed = NULL;

    LOCK(&g_objmgr.mutex);
    obj = __objtabsrch(hld);
    if (obj) {
		/* in normal, ref-count must be greater than zero. otherwise, we will throw a assert fail*/
		assert( obj->refcnt > 0 );
		if (obj->refcnt > 0 ) {

            /* decrease the ref-count */
            --obj->refcnt;

           /* if this object is waitting for close and ref-count decrease equal to zero,
				close it */
			if ( ( 0 == obj->refcnt ) && ( OBJSTAT_CLOSEWAIT == obj->status ) ) {
                __objtabrmve(obj->hld, &removed);
			}
        }
    }
    UNLOCK(&g_objmgr.mutex);

    if (removed) {
        __objtagfree(removed);
    }
}

PORTABLEIMPL(void) objclos(objhld_t hld)
{
    object_t *removed, *obj;

	removed = NULL;

    LOCK(&g_objmgr.mutex);
	obj = __objtabsrch(hld);
    if (obj) {
        /* if this object is already in CLOSE_WAIT status, maybe trying an "double close" operation, do nothing.
           if ref-count large than zero, do nothing during this close operation, actual close will take place when the last count dereference.
           if ref-count equal to zero, close canbe finish immediately */
        if ((0 == obj->refcnt) && (OBJSTAT_NORMAL == obj->status)){
            __objtabrmve(obj->hld, &removed);
        } else {
            obj->status = OBJSTAT_CLOSEWAIT;
        }
    }
    UNLOCK(&g_objmgr.mutex);

    if (removed) {
        __objtagfree(removed);
    }
}
