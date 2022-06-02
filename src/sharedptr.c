#include "sharedptr.h"
#include "zmalloc.h"

#include <errno.h>

#include "compiler.h"
#include "atom.h"

struct sharedptr
{
    long refcnt;
    long size;
    int type;
    int state;
    union {
        void *data;
        char section[1];
    };
};

enum SharedPtrType
{
    SPT_CONTIGUOUS = 0,
    SPT_DISCRETE,
};

sharedptr_pt ref_makeshared(long size)
{
    sharedptr_pt ptr;

    if ( unlikely(size < 0)) {
        return NULL;
    }

    ptr = (sharedptr_pt)ztrymalloc(size + sizeof(*ptr));
    if (unlikely(!ptr)) {
        return NULL;
    }
    ptr->refcnt = 0;
    ptr->size = size;
    ptr->type = SPT_CONTIGUOUS;
    ptr->state = SPS_AVAILABLE;
    return ptr;
}

sharedptr_pt ref_shared_from_this(void *data, long size)
{
    sharedptr_pt ptr;

    if ( unlikely(!data || size <= 0 )) {
        return NULL;
    }

    ptr = (sharedptr_pt)ztrymalloc(size + sizeof(*ptr));
    if (unlikely(!ptr)) {
        return NULL;
    }
    ptr->refcnt = 0;
    ptr->size = size;
    ptr->type = SPT_DISCRETE;
    ptr->state = SPS_AVAILABLE;
    return ptr;
}

void *ref_retain(sharedptr_pt sptr)
{
    if (unlikely(!sptr)) {
        return NULL;
    }

    if (SPS_AVAILABLE != atom_get(&sptr->state) ) {
        return NULL;
    }

    atom_addone(&sptr->refcnt);

    if (sptr->type == SPT_CONTIGUOUS) {
        return &sptr->section[0];
    }

    return sptr->data;
}

static int _ref_decrease_check(sharedptr_pt sptr, int decrease)
{
    int expect;

    if (0 == atom_decrease(&sptr->refcnt, decrease)) {
        expect = SPS_CLOSING;
        if (atom_compare_exchange_strong(&sptr->state, &expect, SPS_CLOSED)) {
            zfree(sptr);
            return 1;
        }
    }
    return 0;
}

int ref_close(sharedptr_pt sptr)
{
    int expect;

    if (unlikely(!sptr)) {
        return -EINVAL;
    }

    expect = SPS_AVAILABLE;
    if (atom_compare_exchange_strong(&sptr->state, &expect, SPS_CLOSING)) {
        return _ref_decrease_check(sptr, 0);
    }

    return 0;
}

int ref_release(sharedptr_pt sptr)
{
    if (unlikely(!sptr)) {
        return -EINVAL;
    }

    return _ref_decrease_check(sptr, 1);
}
