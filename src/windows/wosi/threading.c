#include "threading.h"

#include <Windows.h>

#include "zmalloc.h"

struct WIN32_THPAR {
    void* (*start_rtn_)(void*);
    void* arg_;
};

uint32_t WINAPI ThProc(void* parameter)
{
    struct WIN32_THPAR* par = (struct WIN32_THPAR*)parameter;
    void* (*start_rtn)(void*) = par->start_rtn_;
    void* arg = par->arg_;

    zfree(par);
    if (start_rtn) {
        return (uint32_t)start_rtn(arg);
    }
    return 0;
}

int lwp_create(lwp_t *lwp, int priority, void*(*start_rtn)(void*), void *arg)
{
    HANDLE th;
    struct WIN32_THPAR* thpar;

    thpar = (struct WIN32_THPAR *)ztrymalloc(sizeof(struct WIN32_THPAR));
    if (!thpar) {
        return -ENOMEM;
    }

    thpar->arg_ = arg;
    thpar->start_rtn_ = start_rtn;
    th = CreateThread(NULL, 0, ThProc, thpar, 0, NULL);
    if (!th) {
        return posix__makeerror(GetLastError());
    }

    if (priority > 0) {
        SetThreadPriority(th, THREAD_PRIORITY_TIME_CRITICAL);
    }

    lwp->detached_ = NO;
    lwp->pid_ = th;
    return 0;
}

lwp_handle_t lwp_self()
{
    return GetCurrentThread();
}

lwp_handle_t lwp_raw(lwp_t *lwp)
{
    if (likely(lwp)) {
        return lwp->pid_;
    }

    return 0;
}

int lwp_yield(lwp_t *tidp)
{
    return SwitchToThread();
}

int lwp_setaffinity(const lwp_t *lwp, int cpumask)
{
    if (0 == cpumask) {
        return -1;
    }

    if (SetThreadAffinityMask(lwp->pid_, (DWORD_PTR)cpumask)) {
        return 0;
    }

    return posix__makeerror(GetLastError());
}

int lwp_getaffinity(const lwp_t *lwp)
{
    DWORD_PTR ProcessAffinityMask, SystemAffinityMask;

    if (!GetProcessAffinityMask(lwp->pid_, &ProcessAffinityMask, &SystemAffinityMask)) {
        return posix__makeerror(GetLastError());
    }

    return (int)ProcessAffinityMask;
}

nsp_status_t lwp_setname(const lwp_t *lwp, const abuff_pthread_name_t *name)
{
    HRESULT fr;

    if (!lwp || !name) {
        return posix__makeerror(EINVAL);
    }

    fr = SetThreadDescriptionA(lwp->pid_, name->cst);
    if (SUCCEEDED(fr)) {
        return NSP_STATUS_SUCCESSFUL;
    }
    return posix__makeerror(fr);
}

nsp_status_t lwp_getname(const lwp_t *lwp, abuff_pthread_name_t *name)
{
    HRESULT fr;
    PSTR pszThreaDescription;

    if (!lwp || !name) {
        return posix__makeerror(EINVAL);
    }

    fr = GetThreadDescriptionA(lwp->pid_, &pszThreaDescription);
    if (SUCCEEDED(fr)) {
        abuff_strcpy(name, pszThreaDescription);
        LocalFree(ppszThreaDescription);
        return NSP_STATUS_SUCCESSFUL;
    }

    return posix__makeerror(fr);
}

int lwp_detach(lwp_t *lwp)
{
    if (!lwp) {
        return posix__makeerror(EINVAL);
    }

    if (!lwp->detached_) {
        if (lwp->pid_) {
            CloseHandle(lwp->pid_);
        }

        lwp->pid_ = NULL;
        lwp->detached_ = YES;
        return 0;
    }

    return posix__makeerror(EINVAL);
}

int lwp_join(lwp_t *lwp, void **retval)
{
    if (!lwp) {
        return -EINVAL;
    }

    if (!lwp->detached_) {
        if (lwp->pid_) {
            CloseHandle(lwp->pid_);
        }

        lwp->pid_ = NULL;
        lwp->detached_ = YES;
        return 0;
    }

    return -1;
}

int lwp_setkey(lwp_t *lwp, void *key)
{
    return -1;
}

void *lwp_getkey(lwp_t *lwp)
{
    return NULL;
}

int lwp_mutex_init(lwp_mutex_t *mutex, nsp_boolean_t recursive)
{
    if (mutex) {
        InitializeCriticalSection( &mutex->handle_ );
        return 0;
    }
    return posix__makeerror(EINVAL);
}

void lwp_mutex_uninit(lwp_mutex_t *mutex)
{
    if (mutex) {
        DeleteCriticalSection( &mutex->handle_ );
    }
}

void lwp_mutex_lock(lwp_mutex_t *mutex)
{
    if (mutex) {
        EnterCriticalSection(&mutex->handle_);
    }
}

int lwp_mutex_trylock(lwp_mutex_t *mutex)
{
    if (!mutex) {
        return posix__makeerror(EINVAL);
    }

    if (TryEnterCriticalSection(&mutex->handle_)) {
        return 0;
    }

    return posix__makeerror(GetLastError());
}

int lwp_mutex_timedlock(lwp_mutex_t *mutex, uint32_t expires)
{
    return -1;
}

void lwp_mutex_unlock(lwp_mutex_t *mutex)
{
    if (mutex) {
        LeaveCriticalSection(&mutex->handle_);
    }
}

nsp_status_t lwp_rwlock_init(lwp_rwlock_t *rwlock)
{
    if (unlikely(!rwlock)) {
        return posix__makeerror(EINVAL);
    }

    InitializeSRWLock(&rwlock->handle_);
    return NSP_STATUS_SUCCESSFUL;
}

void  lwp_rwlock_uninit(lwp_rwlock_t *rwlock)
{

}

nsp_status_t  lwp_rwlock_rdlock(lwp_rwlock_t *rwlock)
{
    if (unlikely(!rwlock)) {
        return posix__makeerror(EINVAL);
    }

    AcquireSRWLockShared(&rwlock->handle_);
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t  lwp_rwlock_wrlock(lwp_rwlock_t *rwlock)
{
    if (unlikely(!rwlock)) {
        return posix__makeerror(EINVAL);
    }

    AcquireSRWLockExclusive(&rwlock->handle_);
    return NSP_STATUS_SUCCESSFUL;
}

void  lwp_rwlock_rdunlock(lwp_rwlock_t *rwlock)
{
    if (likely(rwlock)) {
        ReleaseSRWLockShared(&rwlock->handle_);
    }
}

void  lwp_rwlock_wrunlock(lwp_rwlock_t *rwlock)
{
    if (likely(rwlock)) {
        ReleaseSRWLockExclusive(&rwlock->handle_);
    }
}

/***********************************           event          *********************************/

static int _lwp_event_init(lwp_event_t *evo)
{
    if (!evo) {
        return posix__makeerror(EINVAL);
    }

    evo->cond_ = CreateEvent(NULL, evo->sync_ ? FALSE : TRUE, FALSE, NULL);
    if (!evo->cond_) {
        return posix__makeerror(GetLastError());
    }

    return 0;
}

int lwp_event_init(lwp_event_t *evo, enum lwp_event_catagory pattern)
{
    if ( unlikely(!evo) ) {
        return posix__makeerror(EINVAL);
    }
    evo->sync_ = (int)pattern;
    return _lwp_event_init(evo);
}

void lwp_event_uninit(lwp_event_t *evo)
{
    if (evo) {
        if (evo->cond_) {
            CloseHandle(evo->cond_);
            evo->cond_ = NULL;
        }
    }
}

static int _lwp_event_wait_inifinte(lwp_event_t *evo)
{
    return lwp_event_wait(evo, -1);
}

int lwp_event_wait(lwp_event_t *evo, int interval/*ms*/)
{
    DWORD waitRes;

    if (!evo) {
        return posix__makeerror(EINVAL);
    }

    if (!evo->cond_) {
        return posix__makeerror(EBADF);
    }

    /* if t state of the specified object is signaled before wait function called, the return value willbe @WAIT_OBJECT_0
        either synchronous event or notification event.*/
    if (interval >= 0) {
        waitRes = WaitForSingleObject(evo->cond_, (DWORD)interval);
    }
    else {
        waitRes = WaitForSingleObject(evo->cond_, INFINITE);
    }

    if (WAIT_FAILED == waitRes) {
        return posix__makeerror(GetLastError());
    } else if (WAIT_TIMEOUT == waitRes) {
        return ETIMEDOUT;
    } else {
        return 0;
    }
}

PORTABLEAPI(int) lwp_event_awaken(lwp_event_t *evo)
{
    if (!evo) {
        return -EINVAL;
    }

    if (!evo->cond_) {
        return -EBADF;
    }

    return SetEvent(evo->cond_);
}

PORTABLEAPI(void) lwp_event_block(lwp_event_t *evo)
{
    if (evo) {
        if (evo->cond_ && evo->sync_ == 0) {
            ResetEvent(evo->cond_);
        }
    }
}

void lwp_hang()
{
    int retval;
    lwp_event_t lwp_event;

    retval = lwp_event_init(&lwp_event, LWPEC_SYNC);
    if ( unlikely(retval < 0) ) {
        return;
    }

    lwp_event_wait( &lwp_event, -1 );
    lwp_event_uninit(&lwp_event);
}

int lwp_delay(uint64_t microseconds)
{
    typedef NTSTATUS(WINAPI* DelayExecution)(BOOL bAlertable, PLARGE_INTEGER pTimeOut);
    static DelayExecution ZwDelayExecution = NULL;
    static HINSTANCE inst = NULL;

    if (!ZwDelayExecution) {
        if (!inst) {
            inst = LoadLibraryA("ntdll.dll");
            if (!inst) {
                return -1;
            }
        }
        ZwDelayExecution = (DelayExecution)GetProcAddress(inst, "NtDelayExecution");
    }

    if (ZwDelayExecution) {
        LARGE_INTEGER TimeOut;
        TimeOut.QuadPart = -1 * microseconds * 10;
        if (!NT_SUCCESS(ZwDelayExecution(FALSE, &TimeOut))) {
            return -1;
        }
        return 0;
    }

    return -1;
}
