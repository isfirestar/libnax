#if !defined MULTITHREADING_H
#define MULTITHREADING_H

#include "compiler.h"
#include "abuff.h"
#include "zmalloc.h"

#if _WIN32

#include <Windows.h>

typedef HANDLE lwp_handle_t;

struct _lwp_t
{
    HANDLE pid_;
};

#define LWP_TYPE_DECLARE(name)    \
            struct _lwp_t name = { .pid_ = NULL }
#define LWP_TYPE_INIT  \
            { .pid_ = NULL }

struct _mutex_t
{
    CRITICAL_SECTION handle_;
};

#define RWLOCK_MODE_FREE        (0)
#define RWLOCK_MODE_EXCLUSIVE   (1)
#define RWLOCK_MODE_SHARED      (2)

#include <synchapi.h>
struct _rwlock_t
{
    SRWLOCK handle_;
};

#else /* POSIX */

/* -lpthread */
#include <pthread.h>

typedef pthread_t lwp_handle_t;  /* typedef unsigned long int pthread_t; but
                                    typedef int __pid_t */

struct _lwp_t
{
    pthread_attr_t attr;
    pthread_t pid;
    pthread_key_t  key;
};

#define LWP_TYPE_INIT \
           { .pid_ = 0, .key = 0 }

#define LWP_TYPE_DECLARE(name)    \
            struct _lwp_t name = LWP_TYPE_INIT

struct _mutex_t
{
    pthread_mutex_t handle_;
};

struct _rwlock_t
{
    pthread_rwlock_t handle_;
};

#endif /* _WIN32 */

typedef struct _lwp_t           lwp_t;
typedef struct _mutex_t         lwp_mutex_t;
typedef struct _rwlock_t        lwp_rwlock_t;

/* create a new thread with specify priority @priority.
    if @priority less than or equal to zero, create behavior shall be default */
PORTABLEAPI(nsp_status_t) lwp_create(lwp_t *lwp, int priority, void*(*start_rtn)(void*), void *arg);
PORTABLEAPI(nsp_status_t) lwp_detach(lwp_t *lwp);
PORTABLEAPI(nsp_status_t) lwp_join(lwp_t *lwp, void **retval);
/* on success, return value shall be PTHREAD_CREATE_JOINABLE ro PTHREAD_CREATE_DETACHED.
    otherwise, negative integer value returned */
PORTABLEAPI(nsp_boolean_t) lwp_joinable(lwp_t *lwp);
//#define lwp_joinable(lwp)   (((lwp)->pid_ <= 0) ? 0 : (!(lwp)->detached_))

/* retain the thread itself */
PORTABLEAPI(lwp_handle_t) lwp_self();
PORTABLEAPI(lwp_handle_t) lwp_raw(lwp_t *tidp);

/* yield thread */
PORTABLEAPI(nsp_status_t) lwp_yield(lwp_t *tidp);

/* thread affinity management */
PORTABLEAPI(nsp_status_t) lwp_setaffinity(const lwp_t *lwp, int cpumask);
PORTABLEAPI(nsp_status_t) lwp_getaffinity(const lwp_t *lwp, int *cpumask);

/* thread name */
/* The  thread name is a meaningful C language string, whose length is restricted to 16 characters, including the terminating null byte ('\0').  */
typedef abuff_type(16)  abuff_pthread_name_t;
PORTABLEAPI(nsp_status_t) lwp_setname(const lwp_t *lwp, const abuff_pthread_name_t *name);
PORTABLEAPI(nsp_status_t) lwp_getname(const lwp_t *lwp, abuff_pthread_name_t *name);

#if _WIN32
#define lwp_exit(code)
#else
#define lwp_exit(code) pthread_exit((code))
#endif

#if _WIN32
typedef LONG lwp_once_t;
#define LWP_ONCE_INIT 0
inline void lwp_once(lwp_once_t* once, void(*proc)())
{
    if (LWP_ONCE_INIT == InterlockedCompareExchange((volatile LONG *)once, 1, 0)) {
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

/* for RW-locker */
PORTABLEAPI(nsp_status_t)  lwp_rwlock_init(lwp_rwlock_t *rwlock);
PORTABLEAPI(void)  lwp_rwlock_uninit(lwp_rwlock_t *rwlock);
PORTABLEAPI(nsp_status_t)  lwp_rwlock_rdlock(lwp_rwlock_t *rwlock);
PORTABLEAPI(nsp_status_t)  lwp_rwlock_wrlock(lwp_rwlock_t *rwlock);
PORTABLEAPI(void)  lwp_rwlock_rdunlock(lwp_rwlock_t *rwlock);
PORTABLEAPI(void)  lwp_rwlock_wrunlock(lwp_rwlock_t *rwlock);

enum lwp_event_category
{
    LWPEC_NOTIFY = 0,
    LWPEC_SYNC,
};

#if _WIN32
struct _lwp_event
{
    enum lwp_event_category sync_;
    HANDLE cond_;
};
#else
struct _lwp_event
{
    enum lwp_event_category sync_;
    pthread_cond_t cond_;
    int pass_;
    lwp_mutex_t mutex_;
    int effective;
};
#endif
typedef struct _lwp_event lwp_event_t;

/* synchronous or notifications event for thread */
PORTABLEAPI(nsp_status_t) lwp_event_init(lwp_event_t *evo, enum lwp_event_category pattern);
PORTABLEAPI(void) lwp_event_uninit(lwp_event_t *evo);

#define lwp_init_synchronous_event(evo) lwp_event_init((evo), LWPEC_SYNC)
#define lwp_init_notification_event(evo) lwp_event_init((evo), LWPEC_NOTIFY)

#define lwp_create_event(evoptr, pattern, status)       do { \
        (evoptr) = (lwp_event_t *)ztrymalloc(sizeof(*(evoptr)));    \
        (status) = (NULL == (evoptr)) ? posix__makeerror(ENOMEM) : lwp_event_init((evoptr), (pattern)); \
    } while(0)
#define lwp_release_event(evoptr)   do { \
        lwp_event_uninit((evoptr)); \
        zfree((evoptr));    \
    } while(0)
#define lwp_create_synchronous_event(evoptr, status)    lwp_create_event(evoptr, LWPEC_SYNC, status)
#define lwp_create_notification_event(evoptr, status)    lwp_create_event(evoptr, LWPEC_NOTIFY, status)

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
