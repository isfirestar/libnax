#if !defined SHARED_PTR_H
#define SHARED_PTR_H

#include "compiler.h"

/*  IMPORTANT:
 *      shared object just only a reference-count maintainer, it doesn't have multi-thread safe guarantee.
 *      everyone shall use mutex -- such as lwp_mutex_t init with PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP -- to work with shared object
 **/

struct refs;
typedef void (*refs_close_fp)(struct refs *ref);
typedef struct refs
{
    int count;
    int status;
    refs_close_fp on_closed;
} refs_t, *refs_pt;

#define NSP_REFS_INITIALIZER(on_close)  { 0, 0, (on_close) }

PORTABLEAPI(void) ref_init(struct refs *ref, const refs_close_fp on_closed);
/* retain the shared object, increase reference-count meanwhile, return value is the reference-count of this object on success, otherwise, return zero or negative number */
PORTABLEAPI(int) ref_retain(struct refs *ref);
/* release the shared object, decrease reference-count meanwhile,
 *  return value is the reference-count of this object on success, otherwise, return negative number
 *  zero return indicate a succesful release operation */
PORTABLEAPI(int) ref_release(struct refs *ref);
/* close the share object, this object are certain unusable after @ref_close call, but it's owned memory may not be freed immediately
 *  the real free operation could postpone to last reference released.(ref_release called) */
PORTABLEAPI(void) ref_close(struct refs *ref);

#endif
