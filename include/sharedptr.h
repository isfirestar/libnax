#if !defined SHARED_PTR_H
#define SHARED_PTR_H

#include "compiler.h"

/*  IMPORTANT:
 *      shared object just only a reference-count maintainer, it doesn't have multi-thread safe guarantee.
 *      everyone shall use mutex -- such as lwp_mutex_t init with PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP -- to work with shared object
 **/

struct shared_ptr;
typedef void (*ref_on_closed_t)(struct shared_ptr *ref);
struct shared_ptr
{
    int count;
    int status;
    ref_on_closed_t on_closed;
};

PORTABLEAPI(void) ref_init(struct shared_ptr *ref, const ref_on_closed_t on_closed);
/* retain the shared object, increase reference-count meanwhile, return value is the reference-count of this object on success, otherwise, return zero or negative number */
PORTABLEAPI(int) ref_retain(struct shared_ptr *ref);
/* release the shared object, decrease reference-count meanwhile,
 *  return value is the reference-count of this object on success, otherwise, return negative number
 *  zero return indicate a succesful release operation */
PORTABLEAPI(int) ref_release(struct shared_ptr *ref);
/* close the share object, this object are certain unusable after @ref_close call, but it's owned memory may not be freed immediately
 *  the real free operation could postpone to last reference released.(ref_release called) */
PORTABLEAPI(void) ref_close(struct shared_ptr *ref);

#endif
