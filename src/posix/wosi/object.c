#include "object.h"

#include "clist.h"
#include "avltree.h"
#include "spinlock.h"
#include "zmalloc.h"

#include <pthread.h>

#define OBJ_HASHTABLE_SIZE   (256)

#define OBJSTAT_NORMAL      (0)
#define OBJSTAT_CLOSEWAIT   (1)

typedef struct spin_lock MUTEX_T;

#define LOCK    acquire_spinlock
#define UNLOCK  release_spinlock

#define INCREMENT(n)    __sync_add_and_fetch(n, 1)
#define DECREMENT(n)    __sync_sub_and_fetch(n, 1)

static void _mutex_init(MUTEX_T *mutex)
{
    initial_spinlock(mutex);
}

static void _mutex_uninit(MUTEX_T *mutex)
{
    ;
}

typedef struct _object_t
{
    struct avltree_node_t clash;    /* the hash conflict item container */
    objhld_t hld;
    int status;
    int refcnt;
    unsigned int size;
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

static struct _object_manager g_objmgr = {
    .roottab = { NULL }, 0, SPIN_LOCK_INITIALIZER,
};

static int _avl_compare_routine(const void *left, const void *right)
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

/* table index = (hld % OBJ_HASHTABLE_SIZE) */
#define _hld2root(hld) ( likely((hld > 0)) ?  &g_objmgr.roottab[ hld & (OBJ_HASHTABLE_SIZE - 1) ] : NULL )

static nsp_status_t _objtabinst(object_t *obj)
{
    object_t find;
    struct avltree_node_t **root, *target;
    nsp_status_t status;

    root = NULL;
    status = NSP_STATUS_SUCCESSFUL;

    LOCK(&g_objmgr.mutex);

    do {
        /* the creator acquire a knowned handle-id */
        if (INVALID_OBJHLD != obj->hld || 0 == obj->hld) {
            /* map root pointer from table */
            root = _hld2root(obj->hld);
            if (unlikely(!root)) {
                status = ENODEV;
                break;
            }

            find.hld = obj->hld;
            target = avlsearch(*root, &find.clash, &_avl_compare_routine);
            if (target) {
                status = EEXIST;
                break;
            }
        } else {
            /* automatic increase handle number */
            obj->hld = ++g_objmgr.atoid;
            root = _hld2root(obj->hld);
            if (unlikely(!root)) {
                status = ENODEV;
                break;
            }
        }

        /* insert into hash list and using avl-binary-tree to handle the clash */
        *root = avlinsert(*root, &obj->clash, &_avl_compare_routine);
    } while (0);

    UNLOCK(&g_objmgr.mutex);
    return posix__makeerror(status);
}

static nsp_status_t _objtabrmve(objhld_t hld, object_t **removed)
{
	object_t node;
    struct avltree_node_t **root, *rmnode;

    root = _hld2root(hld);
    if (unlikely(!root)) {
        return posix__makeerror(ENODEV);
    }

    rmnode = NULL;

    node.hld = hld;
    *root = avlremove(*root, &node.clash, &rmnode, &_avl_compare_routine);
    if (rmnode && removed) {
        *removed = containing_record(rmnode, object_t, clash);
    }

    return ((NULL == rmnode) ? posix__makeerror(ENODEV) : NSP_STATUS_SUCCESSFUL);
}

static object_t *_objtabsrch(const objhld_t hld)
{
    object_t node;
    struct avltree_node_t **root, *target;

    root = _hld2root(hld);
    if (unlikely(!root)) {
        return NULL;
    }

    node.hld = hld;
    target = avlsearch(*root, &node.clash, &_avl_compare_routine);
    if (!target) {
        return NULL;
    }
    return containing_record(target, object_t, clash);
}

static void _objtagfree(object_t *target)
{
    /* release the object context and free target memory when object removed from table
        call the unload routine if not null */
    if ( target->unloader ) {
        target->unloader( target->hld, (void *)target->body );
    }
    zfree( target );
}

static void _objinit()
{
    memset(g_objmgr.roottab, 0, sizeof ( g_objmgr.roottab));
    g_objmgr.atoid = 0;
    _mutex_init(&g_objmgr.mutex);
}

/* In POSIX, call @objinit is option and NOT necessary */
PORTABLEIMPL(void) objinit()
{
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, &_objinit);
}

PORTABLEIMPL(void) objuninit()
{
    _mutex_uninit(&g_objmgr.mutex);
}

PORTABLEIMPL(objhld_t) objallo3(const struct objcreator *creator)
{
    object_t *obj;

    if ( unlikely(!creator) ) {
        return INVALID_OBJHLD;
    }

    if ( unlikely(0 == creator->size|| (creator->context && !creator->ctxsize) || (!creator->context && creator->ctxsize)) ) {
        return INVALID_OBJHLD;
    }

    obj = (object_t *)zmalloc(creator->size + sizeof(object_t));
    if ( unlikely(!obj) ) {
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
            return INVALID_OBJHLD;
        }
    }

    if (unlikely(!NSP_SUCCESS(_objtabinst(obj)))) {
        zfree(obj);
        return INVALID_OBJHLD;
    }

    return obj->hld;
}

PORTABLEIMPL(nsp_status_t) objallo4(const struct objcreator *creator, objhld_t *out)
{
    object_t *obj;
    nsp_status_t status;

    if ( unlikely(!creator || !out) ) {
        return posix__makeerror(EINVAL);
    }

    if ( unlikely(0 == creator->size|| (creator->context && !creator->ctxsize) || (!creator->context && creator->ctxsize)) ) {
        return posix__makeerror(EINVAL);
    }

    obj = (object_t *)ztrycalloc(creator->size + sizeof(object_t));
    if ( unlikely(!obj) ) {
        return posix__makeerror(ENOMEM);
    }

    obj->hld = creator->known;
    if (0 == obj->hld) {
        obj->hld = INVALID_OBJHLD;  /* treat zero specify to be a invalidate value */
    }
    obj->status = OBJSTAT_NORMAL;
    obj->refcnt = 0;
    obj->size = creator->size;
    obj->initializer = creator->initializer;
    obj->unloader = creator->unloader;

    if (obj->initializer) {
        if ( unlikely(obj->initializer((void *)obj->body, creator->context, creator->ctxsize) < 0)) {
            obj->unloader(-1, (void *)obj->body);
            zfree(obj);
            return NSP_STATUS_FATAL;
        }
    }

    status = _objtabinst(obj);
    if (!NSP_SUCCESS(status)) {
        zfree(obj);
        return status;
    }

    *out = obj->hld;
    return NSP_STATUS_SUCCESSFUL;
}

PORTABLEIMPL(objhld_t) objallo(unsigned int size, objinitfn_t initializer, objuninitfn_t unloader, const void *context, unsigned int ctxsize)
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

PORTABLEIMPL(objhld_t) objallo2(unsigned int size)
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
    obj = _objtabsrch(hld);
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

PORTABLEIMPL(unsigned int) objrefr2(objhld_t hld, void **out)
{
    object_t *obj;
    unsigned int size;

    if ( unlikely(!out) ) {
        return (unsigned int)-1;
    }

    obj = NULL;
    *out = NULL;
    size = (unsigned int)-1;

    LOCK(&g_objmgr.mutex);
    obj = _objtabsrch(hld);
    if (obj) {
        /* object status CLOSE_WAIT will be ignore for @objrefr operation */
        if (OBJSTAT_NORMAL == obj->status) {
            ++obj->refcnt;
            *out = obj->body;
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
    obj = _objtabsrch(hld);
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
    obj = _objtabsrch(hld);
    if (obj) {
		/* in normal, ref-count must be greater than zero. otherwise, we will throw a assert fail*/
		assert( obj->refcnt > 0 );
		if (obj->refcnt > 0 ) {

            /* decrease the ref-count */
            --obj->refcnt;

           /* if this object is waitting for close and ref-count decrease equal to zero,
				close it */
			if ( ( 0 == obj->refcnt ) && ( OBJSTAT_CLOSEWAIT == obj->status ) ) {
                _objtabrmve(obj->hld, &removed);
			}
        }
    }
    UNLOCK(&g_objmgr.mutex);

    if (removed) {
        _objtagfree(removed);
    }
}

PORTABLEIMPL(void) objclos(objhld_t hld)
{
    object_t *removed, *obj;

	removed = NULL;

    LOCK(&g_objmgr.mutex);
	obj = _objtabsrch(hld);
    if (obj) {
        /* if this object is already in CLOSE_WAIT status, maybe trying an "double close" operation, do nothing.
           if ref-count large than zero, do nothing during this close operation, actual close will take place when the last count dereference.
           if ref-count equal to zero, close canbe finish immediately */
        if ((0 == obj->refcnt) && (OBJSTAT_NORMAL == obj->status)){
            _objtabrmve(obj->hld, &removed);
        } else {
            obj->status = OBJSTAT_CLOSEWAIT;
        }
    }
    UNLOCK(&g_objmgr.mutex);

    if (removed) {
        _objtagfree(removed);
    }
}
