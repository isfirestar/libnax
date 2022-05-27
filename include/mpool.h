#if !defined MPOOL_H
#define MPOOL_H

#include "compiler.h"

#if _WIN32
    #error "memroy ring-pool pattern does not support windows platform"
#endif

PORTABLEAPI(long)   mrp_create(long size);
PORTABLEAPI(void)   mrp_destory(long mrp);
PORTABLEAPI(void *) mrp_fetch(long mrp);
PORTABLEAPI(nsp_status_t)    mrp_recycle(long mrp, volatile const void *ptr);
PORTABLEAPI(long)   mrp_remain(long mrp);

#endif /* MPOOL_H */
