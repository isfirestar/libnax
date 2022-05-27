#if !defined MULTITHREADING_H
#define MULTITHREADING_H

#include "compiler.h"

#if _WIN32

#include <Windows.h>

typedef HANDLE lwp_handle_t;

struct _lwp_t
{
    nsp_boolean_t detached_;
    HANDLE pid_;
};

#define LWP_TYPE_DECLARE(name)    \
            struct _lwp_t name = { .detached_ = NO, .pid_ = NULL }
#define LWP_TYPE_INIT  \
            { .detached_ = NO,, .pid_ = NULL }

struct _mutex_t
{
    CRITICAL_SECTION handle_;
};

#else /* POSIX */

/* -lpthread */
#include <pthread.h>

typedef pthread_t lwp_handle_t;  /* typedef unsigned long int pthread_t; */

struct _lwp_t
{
    nsp_boolean_t detached_;
    pthread_t pid_;
    pthread_key_t  key_;
};

#define LWP_TYPE_DECLARE(name)    \
            struct _lwp_t name = { .detached_ = NO, .pid_ = 0 }

#define LWP_TYPE_INIT \
            {.detached_ = NO, .pid_ = 0 }

struct _mutex_t
{
    pthread_mutex_t handle_;
};

#endif /* _WIN32 */

typedef struct _lwp_t           lwp_t;
typedef struct _mutex_t         lwp_mutex_t;

/* create a new thread with specify priority @priority.
    if @priority less than or equal to zero, create behavior shall be default */
PORTABLEAPI(nsp_status_t) lwp_create(lwp_t *lwp, int priority, void*(*start_rtn)(void*), void *arg);
PORTABLEAPI(nsp_status_t) lwp_detach(lwp_t *lwp);
PORTABLEAPI(nsp_status_t) lwp_join(lwp_t *lwp, void **retval);
#define lwp_joinable(lwp)   (((lwp)->pid_ <= 0) ? 0 : (!(lwp)->detached_))

/* retain the thread itself */
PORTABLEAPI(lwp_handle_t) lwp_self(lwp_t *tidp);

/* yield thread */
PORTABLEAPI(nsp_status_t) lwp_yield(lwp_t *tidp);

/* thread affinity management */
PORTABLEAPI(nsp_status_t) lwp_setaffinity(const lwp_t *lwp, int cpumask);

#if PERMISS_DEPRECATE
PORTABLEAPI(int) lwp_getaffinity(const lwp_t *lwp);
#endif
PORTABLEAPI(nsp_status_t) lwp_getaffinity(const lwp_t *lwp, int *cpumask);

#if _WIN32
#define lwp_exit(code)
#else
#define lwp_exit(code) pthread_exit((code))
#endif

#if _WIN32
typedef int lwp_once_t;
#define LWP_ONCE_INIT 0
inline void lwp_once(lwp_once_t* once, void(*proc)())
{
    if (LWP_ONCE_INIT == InterlockedCompareExchange((volatile LONG*)once, 1, 0)) {
        proc();
    }
}

#else
typedef pthread_once_t lwp_once_t;
#define LWP_ONCE_INIT   PTHREAD_ONCE_INIT
#define lwp_once(onceptr, proc)        pthread_once((onceptr), proc)
#endif

/* binding user context with thread */
PORTABLEAPI(nsp_status_t) lwp_setkey(lwp_t *lwp, void *key);
PORTABLEAPI(void *) lwp_getkey(lwp_t *lwp);

/* the utility mutex object for thread normal lock */
PORTABLEAPI(nsp_status_t)  lwp_mutex_init(lwp_mutex_t *mutex, nsp_boolean_t recursive);
PORTABLEAPI(void)  lwp_mutex_uninit(lwp_mutex_t *mutex);
PORTABLEAPI(void) lwp_mutex_lock(lwp_mutex_t *mutex);
PORTABLEAPI(nsp_status_t)  lwp_mutex_trylock(lwp_mutex_t *mutex);
PORTABLEAPI(nsp_status_t)  lwp_mutex_timedlock(lwp_mutex_t *mutex, uint32_t expires);
PORTABLEAPI(void) lwp_mutex_unlock(lwp_mutex_t *mutex);

enum lwp_event_category
{
    LWPEC_NOTIFY = 0,
    LWPEC_SYNC,
};

struct _lwp_event
{
    enum lwp_event_category sync_; /* as boolean check */
#if _WIN32
    HANDLE cond_;
#else
    pthread_cond_t cond_;
    int pass_;
    lwp_mutex_t mutex_;
    int effective;
#endif
};
typedef struct _lwp_event lwp_event_t;

/* synchronous or notifications event for thread */
PORTABLEAPI(nsp_status_t) lwp_event_init(lwp_event_t *evo, enum lwp_event_category pattern);
PORTABLEAPI(void) lwp_event_uninit(lwp_event_t *evo);

/* definition table of return value cause by @lwp_event_wait:
 *                    NSP_ERROR_STATUS_EQUAL(ETIMEDOUT)     : timeout specified by @expire expire
 *                               NSP_STATUS_SUCCESSFUL      : singal awakened
 * !NSP_SUCCESS() && !NSP_ERROR_STATUS_EQUAL(ETIMEDOUT)     : system level error trigger
 *
 * demo:
 *   lwp_event_t event;
 *
 *   lwp_event_init(&event, LWPEC_SYNC);
 *   nsp_status_t status = lwp_event_wait(&event, 1000);
 *   if (!NSP_SUCCESS(status)) {
 *       if (NSP_ERROR_STATUS_EQUAL(status, ETIMEDOUT)) {
 *           lwp_event_uninit(&event);
 *           return 1;
 *       }
 *       printf("system error %d triggered.\n", posix__status_to_errno(status));
 *       lwp_event_uninit(&event);
 *       return 1;
 *   }
 *   printf("signal akwaened.\n");
 *   lwp_event_uninit(&event);
 *   return 0;
 */
PORTABLEAPI(nsp_status_t) lwp_event_wait(lwp_event_t *evo, int expire/*ms*/);
PORTABLEAPI(nsp_status_t) lwp_event_awaken(lwp_event_t *evo);
PORTABLEAPI(void) lwp_event_block(lwp_event_t *evo);

/* derived of synchronous or notifications event */
PORTABLEAPI(void) lwp_hang();
PORTABLEAPI(nsp_status_t) lwp_delay(uint64_t microseconds);

#endif /* !MULTITHREADING_H */
