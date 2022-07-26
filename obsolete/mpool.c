#include "mpool.h"

#include "zmalloc.h"

/* very important: result of '-1 % 16' is -1, but, result of MOD(-1, 16) is 15  */
#define BINMOD(n, k)        ( (n) & ((1 << (k)) -1) )
#define MOD(n, d)           ( (n) & ( (d) - 1) )

#if !defined NULL
#define NULL                ((void *)0)
#endif

/* specify the divisor in bits as coefficient use for @MOD operatior */
#define rp_calc_coefficient(n, coefficient) \
    do { ++(coefficient); } while ( 0 == (( (n) >>= 1) & 1) )

struct ringpool
{
    long fetch;
    long recycle;
    long size;
    long remain;
    long pool[0];
};

#if defined(__ATOMIC_RELAXED) && defined(__ATOMIC_SEQ_CST)

#define rp_sub_and_fetch(ptr, n)  \
            __atomic_sub_fetch( (ptr), (n), __ATOMIC_RELAXED)
#define rp_fetch_and_sub(ptr, n)  \
            __atomic_fetch_sub( (ptr), (n), __ATOMIC_RELAXED )

#define rp_add_and_fetch(ptr, n)    \
            __atomic_add_fetch( (ptr), (n), __ATOMIC_RELAXED)
#define rp_fetch_and_add(ptr, n)    \
            __atomic_fetch_add( (ptr), (n), __ATOMIC_RELAXED)

#define rp_exchange(ptr, value)   \
            __atomic_exchange_n( (ptr), (value), __ATOMIC_ACQ_REL)
#define rp_compare_exchange(ptr, expect, desired)   \
            __atomic_compare_exchange_n( (ptr), &(expect), desired, 1, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)

#define rp_thread_fence(order)  \
            __atomic_thread_fence(order)

#define rp_load(ptr)   \
            __atomic_load_n( (ptr), __ATOMIC_SEQ_CST)
#else
#define rp_sub_and_fetch(ptr, n)  \
            __sync_sub_and_fetch( (ptr), (n) )
#define rp_fetch_and_sub(ptr, n)  \
            __sync_fetch_and_sub( (ptr), (n) )

#define rp_add_and_fetch(ptr, n)    \
            __sync_add_and_fetch( (ptr), (n) )
#define rp_fetch_and_add(ptr, n)    \
            __sync_fetch_and_add( (ptr), (n) )

#define rp_exchange(ptr, value)   \
            __sync_lock_test_and_set( (ptr), (value), __sync_synchronize )
#define rp_compare_exchange(ptr, expect, desired)   \
            __sync_bool_compare_and_swap( (ptr), (expect), (desired), __sync_synchronize)

#define rp_thread_fence(order)

#define rp_load(ptr)   \
            __sync_sub_and_fetch( (ptr), 0)
#endif /* _MEMORY_FENCE */

PORTABLEIMPL(long) mrp_create(long size)
{
    struct ringpool *mrp;

    if ( unlikely(size <= 0) ) {
        return -1;
    }

    /* @size has to be an integer multiple of 2 */
    if ( unlikely((size & (size - 1)) != 0) ) {
        return -1;
    }

    mrp = (struct ringpool *)ztrycalloc(sizeof(struct ringpool) + sizeof(long) * size);
    if ( unlikely(!mrp) ) {
        return -1;
    }

    mrp->size = mrp->remain = size;
    mrp->fetch = 0;
    mrp->recycle = size - 1;
    return (long)mrp;
}

PORTABLEIMPL(void) mrp_destory(long mrp)
{
    if ( unlikely(!mrp) ) {
        return;
    }

    zfree( (void *)mrp );
}

PORTABLEIMPL(void *) mrp_fetch(long mrp)
{
    volatile long offset, nilptr;
    struct ringpool *poolptr;

    poolptr = (struct ringpool *)mrp;
    if ( unlikely(!poolptr) ) {
        return NULL;
    }

    /* befor move @fetch cursor, we MUST ensure memory protection that read instruction not ascend above xchg  */
    rp_thread_fence(__ATOMIC_ACQUIRE);

    /* algorithm guarantee that @cacheptr NOT be null,
       try to decrease the reference count @remain before any other instructions */
    while ( rp_sub_and_fetch(&poolptr->remain, 1) < 0 ) {
        if ( rp_add_and_fetch(&poolptr->remain, 1) <= 0 ) {
            return NULL;
        }
    }

    offset = rp_fetch_and_add(&poolptr->fetch, 1);
    offset = (offset & (poolptr->size - 1));

    /* after @fetch cursor moved, before exchange, memory protection change to ensure below instructions did not sink. */
    rp_thread_fence(__ATOMIC_RELEASE);

    nilptr = 0;
    return (void *)rp_exchange(&poolptr->pool[offset], nilptr);
}

PORTABLEIMPL(nsp_status_t) mrp_recycle(long mrp, volatile const void *ptr)
{
    volatile long offset, desired;
    long expect;
    struct ringpool *poolptr;

    poolptr = (struct ringpool *)mrp;
    if ( unlikely(!poolptr) ) {
        return posix__makeerror(EINVAL);
    }

    expect = 0;
    desired = (long)ptr;

    /* befor move @fetch recycle, we MUST ensure memory protection that read instruction not ascend above xchg  */
    rp_thread_fence(__ATOMIC_ACQUIRE);

    offset = rp_add_and_fetch(&poolptr->recycle, 1);
    offset = (offset & (poolptr->size - 1));

    /* after @fetch cursor moved, before exchange, memory protection change to ensure below instructions did not sink. */
    rp_thread_fence(__ATOMIC_RELEASE);

    /* algorithm guarantee that @poolptr->pool[offset] MUST be null, decrease the reference count of remain now */
    if ( rp_compare_exchange(&poolptr->pool[offset], expect, desired) ) {
        /* on success, increase the count of element remian */
        rp_add_and_fetch(&poolptr->remain, 1);
        return NSP_STATUS_SUCCESSFUL;
    }

    return NSP_STATUS_FATAL;
}

PORTABLEIMPL(long) mrp_remain(long mrp)
{
    if ( unlikely(!mrp)) {
        return -1L;
    }

    return rp_load( &((struct ringpool *)mrp)->remain );
}
