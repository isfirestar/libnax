#include "refs.h"

#define REF_STATUS_NORMAL       (0)
#define REF_STATUS_CLOSEWAIT    (1)
#define REF_STATUS_CLOSED       (2)

PORTABLEIMPL(void) ref_init(refs_pt ref, const refs_close_fp on_closed)
{
    if (unlikely(!ref)) {
        return;
    }

    ref->count = 0;
    ref->status = REF_STATUS_NORMAL;
    ref->on_closed = on_closed;
}

PORTABLEIMPL(int) ref_retain(refs_pt ref)
{
    if (unlikely(!ref)) {
        return -1;
    }

    if (REF_STATUS_NORMAL != ref->status) {
        return -1;
    }

    ref->count++;
    return ref->count;
}

PORTABLEIMPL(int) ref_release(refs_pt ref)
{
    if (unlikely(!ref)) {
        return -1;
    }

    if (unlikely(REF_STATUS_CLOSED == ref->status)) {
        return -1;
    }

#if DEBUG
    assert(ref->count);
#endif
    if (unlikely(ref->count <= 0)) {
        return -1;
    }
    --ref->count;

    if (0 == ref->count && REF_STATUS_CLOSEWAIT == ref->status) {
        if (ref->on_closed) {
            ref->on_closed(ref);
        }
    }
    return ref->count;
}

PORTABLEIMPL(void) ref_close(refs_pt ref)
{
    if (unlikely(!ref)) {
        return;
    }

    if (REF_STATUS_NORMAL == ref->status) {
        if (0 == ref->count) {
            if (ref->on_closed) {
                ref->on_closed(ref);
            }
            ref->status = REF_STATUS_CLOSED;
        } else {
            ref->status = REF_STATUS_CLOSEWAIT;
        }
    }
}
