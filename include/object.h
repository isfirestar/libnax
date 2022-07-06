#if !defined BASEOBJECT_H
#define BASEOBJECT_H

#include "compiler.h"

typedef long objhld_t;
typedef int( STDCALL * objinitfn_t)(void *udata, const void *ctx, int ctxcb);
typedef void( STDCALL * objuninitfn_t)(objhld_t hld, void *udata);

#define INVALID_OBJHLD      (~((objhld_t)(0)))

struct objcreator
{
    /* opt, default to -1, set it to a effective value meat object will use the hanle-id to try insert into memory-pool and failure on existed */
    objhld_t known;
    /* MUST large than zero */
    unsigned int size;
    /* opt,can be null, if specified, this routine will be invoke before object insert into memory-pool */
    objinitfn_t initializer;
    /* opt,can be null, if specified, this routine wiil be invoke after object finilly remove from memory-pool */
    objuninitfn_t unloader;
    /* opt,can be null, when this field is non-null pointer, @ctxsize field MUST set to the size of bytes of pointer @context pointer to. */
    const void *context;
    unsigned int ctxsize;
};

PORTABLEAPI(void) objinit(); /* not necessary for POSIX */
PORTABLEAPI(void) objuninit();

/* obsolete implementation, normal object allocate function, new application should use @objallo3 instead of it */
PORTABLEAPI(objhld_t) objallo(unsigned int size, objinitfn_t initializer, objuninitfn_t unloader, const void *context, unsigned int ctxsize);
/* obsolete implementation, simple way to allocate a object, new application should use @objallo3 instead of it  */
PORTABLEAPI(objhld_t) objallo2(unsigned int size);
/* try to allocate a object with a knwon handle-identify specified by @hld,
    when success, the return value equivalent to input parameter @hld, otherwise INVALID_OBJHLD returned */
PORTABLEAPI(objhld_t) objallo3(const struct objcreator *creator);
PORTABLEAPI(nsp_status_t) objallo4(const struct objcreator *creator, objhld_t *out);

PORTABLEAPI(void *) objrefr(objhld_t hld);	/* reference object, NULL on failure */
PORTABLEAPI(unsigned int) objrefr2(objhld_t hld, void **output); /* reference object, return the size of object context on success, (uint32_t)-1 on fail */
PORTABLEAPI(void) objdefr(objhld_t hld);		/* deference object */
PORTABLEAPI(void *) objreff(objhld_t hld);	/* object finallize reference, after success call, object status is going to change to CLOSE_WAIT */
PORTABLEAPI(void) objclos(objhld_t hld);		/* object mark close */

#endif
