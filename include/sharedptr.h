#if !defined SHARED_PTR_H
#define SHARED_PTR_H

/* It is very important to say that :
 * you can't obtain multi-threading completely safe guarantee by @sharedptr without any other mutex locker!!!
 * refer to nshost!io.c*/

typedef struct sharedptr *sharedptr_pt;

enum SharedPtrState
{
    SPS_INIT = 0,
    SPS_AVAILABLE,
    SPS_CLOSING,
    SPS_CLOSED,
};

extern sharedptr_pt ref_makeshared(long size);
extern sharedptr_pt ref_shared_from_this(void *data, long size);
extern void *ref_retain(sharedptr_pt sptr);
extern int ref_close(sharedptr_pt sptr);
extern int ref_release(sharedptr_pt sptr);

#endif
