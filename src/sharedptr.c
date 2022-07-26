#include "sharedptr.h"

#define REF_STATUS_NORMAL       (0)
#define REF_STATUS_CLOSEWAIT    (1)
#define REF_STATUS_CLOSED       (2)

PORTABLEIMPL(void) ref_init(struct shared_ptr *ref, const ref_on_closed_t on_closed)
{
    if (!ref) {
        return;
    }

    ref->count = 0;
    ref->status = REF_STATUS_NORMAL;
    ref->on_closed = on_closed;
}

PORTABLEIMPL(int) ref_retain(struct shared_ptr *ref)
{
    if (!ref) {
        return -1;
    }

    if (ref->status != REF_STATUS_NORMAL) {
        return -1;
    }

    ref->count++;
    return ref->count;
}

PORTABLEIMPL(int) ref_release(struct shared_ptr *ref)
{
    if (!ref) {
        return -1;
    }

    if (ref->status == REF_STATUS_CLOSED) {
        return -1;
    }

#if DEBUG
    assert(ref->count);
#endif
    if (ref->count <= 0) {
        return -1;
    }
    --ref->count;

    if (ref->count == 0 && ref->status == REF_STATUS_CLOSEWAIT) {
        if (ref->on_closed) {
            ref->on_closed(ref);
        }
    }
    return ref->count;
}

PORTABLEIMPL(void) ref_close(struct shared_ptr *ref)
{
    if (!ref) {
        return;
    }

    if (ref->status == REF_STATUS_NORMAL) {
        if (ref->count == 0) {
            if (ref->on_closed) {
                ref->on_closed(ref);
            }
            ref->status = REF_STATUS_CLOSED;
        } else {
            ref->status = REF_STATUS_CLOSEWAIT;
        }
    }
}
