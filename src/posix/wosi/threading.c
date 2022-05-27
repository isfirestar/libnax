#include "threading.h"

#include <sched.h>

/*********************************************************************************************************
*****************************************        thread            ***************************************
**********************************************************************************************************/

nsp_status_t lwp_create(lwp_t *lwp, int priority, void*(*start_rtn)(void*), void *arg)
{
    int retval;
    pthread_attr_t attr;
    struct sched_param param;

    if ( unlikely((!lwp || !start_rtn)) ) {
        return posix__makeerror(EINVAL);
    }

    pthread_attr_init(&attr);
    lwp->detached_ = NO;

    if (priority > 0) {
        pthread_attr_setschedpolicy(&attr, SCHED_RR);
        param.sched_priority = priority;
        pthread_attr_setschedparam(&attr,&param);
        pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    }

    retval = pthread_create(&lwp->pid_, &attr, start_rtn, arg);
    pthread_attr_destroy(&attr);
    return posix__makeerror(retval);
}

lwp_handle_t lwp_self(lwp_t *lwp)
{
    if (unlikely(!lwp)) {
        return posix__makeerror(EINVAL);
    }

    lwp->pid_ = pthread_self();
    return lwp->pid_;
}

nsp_status_t lwp_yield(lwp_t *tidp)
{
    if ( likely(0 == sched_yield()) ) {
        return NSP_STATUS_SUCCESSFUL;
    }

    return posix__makeerror(errno);
}

nsp_status_t lwp_setaffinity(const lwp_t *lwp, int cpumask)
{
    int i;
    cpu_set_t cpuset;
    int retval;

    if ( unlikely((!lwp || 0 == cpumask)) ) {
        return posix__makeerror(EINVAL);
    }

    CPU_ZERO(&cpuset);

    for (i = 0; i < 32; i++) {
        if (cpumask & (1 << i)) {
            CPU_SET(i, &cpuset);
        }
    }

    retval = pthread_setaffinity_np(lwp->pid_, sizeof(cpu_set_t), &cpuset);
    if (0 != retval) {
        return posix__makeerror(retval);
    }

    /* verify */
    for (i = 0; i < 32; i++) {
        if (cpumask & (1 << i)) {
            if (!CPU_ISSET(i, &cpuset)) {
                return NSP_STATUS_FATAL;
            }
        }
    }

    return NSP_STATUS_SUCCESSFUL;
}

#if PERMISS_DEPRECATE
int lwp_getaffinity(const lwp_t *lwp)
{
    int i;
    cpu_set_t cpuset;
    int mask;
    int retval;

    if ( unlikely(!lwp) ) {
        return posix__makeerror(EINVAL);
    }

    mask = 0;
    CPU_ZERO(&cpuset);
    retval = pthread_getaffinity_np(lwp->pid_, sizeof(cpu_set_t), &cpuset);
    if ( 0 != retval ) {
        return posix__makeerror(retval);
    }

    for (i = 0; i < 32; i++) {
        if(CPU_ISSET(i, &cpuset)) {
            mask |= (1 << i);
        }
    }

    return mask;
}
#endif

nsp_status_t lwp_getaffinity(const lwp_t *lwp, int *cpumask)
{
    int i;
    cpu_set_t cpuset;
    int mask;
    int retval;

    if ( unlikely(!lwp) ) {
        return posix__makeerror(EINVAL);
    }

    mask = 0;
    CPU_ZERO(&cpuset);
    retval = pthread_getaffinity_np(lwp->pid_, sizeof(cpu_set_t), &cpuset);
    if ( 0 != retval ) {
        return posix__makeerror(retval);
    }

    for (i = 0; i < 32; i++) {
        if(CPU_ISSET(i, &cpuset)) {
            mask |= (1 << i);
        }
    }

    if (likely(cpumask)) {
        *cpumask = mask;
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t lwp_detach(lwp_t *lwp)
{
    int fr;

    if ( unlikely(!lwp) ) {
        return posix__makeerror(EINVAL);
    }

    if (!lwp->detached_ && lwp->pid_) {
        fr = pthread_detach(lwp->pid_);
        if (0 == fr) {
            lwp->detached_ = 1;
            lwp->pid_ = 0;
        }
        return posix__makeerror(fr);
    }

    return NSP_STATUS_FATAL;
}

nsp_status_t lwp_join(lwp_t *lwp, void **retval)
{
    int fr;

    if ( unlikely(!lwp) ) {
        return posix__makeerror(EINVAL);
    }

    if ( lwp_joinable(lwp) ) {
        fr = pthread_join(lwp->pid_, retval);
        if (0 == fr) {
            lwp->detached_ = 1;
            lwp->pid_ = 0;
        }
        return posix__makeerror(fr);
    }

    return NSP_STATUS_FATAL;
}

nsp_status_t lwp_setkey(lwp_t *lwp, void *key)
{
    int retval;

    if ( unlikely(!lwp || !key) ) {
        return posix__makeerror(EINVAL);
    }

    retval = pthread_key_create(&lwp->key_, NULL);
    if (0 != retval) {
        return posix__makeerror(retval);
    }

    retval = pthread_setspecific(lwp->key_, key);
    return posix__makeerror(retval);
}

void *lwp_getkey(lwp_t *lwp)
{
    return ( (likely(lwp)) ? pthread_getspecific(lwp->key_) : NULL );
}

/*********************************************************************************************************
*****************************************        mutex            ***************************************
**********************************************************************************************************/

nsp_status_t lwp_mutex_init(lwp_mutex_t *mutex, nsp_boolean_t recursive)
{
    pthread_mutexattr_t mutexattr;
    int retval;

    if (unlikely(!mutex)) {
        return posix__makeerror(EINVAL);
    }

    retval = pthread_mutexattr_init(&mutexattr);
    if ( unlikely(0 != retval) ) {
        return posix__makeerror(retval);
    }

    if ( recursive ) {
        pthread_mutexattr_settype(&mutexattr, PTHREAD_MUTEX_RECURSIVE_NP);
    }

    retval = pthread_mutex_init(&mutex->handle_, &mutexattr);
    pthread_mutexattr_destroy(&mutexattr);
    return posix__makeerror(retval);
}

void lwp_mutex_uninit(lwp_mutex_t *mutex)
{
    if ( unlikely(!mutex) ) {
        return;
    }

    pthread_mutex_destroy(&mutex->handle_);
}

void lwp_mutex_lock(lwp_mutex_t *mutex)
{
    if ( unlikely(!mutex) ) {
        return;
    }

    pthread_mutex_lock(&mutex->handle_);
}

nsp_status_t lwp_mutex_trylock(lwp_mutex_t *mutex)
{
    if ( unlikely(!mutex) ) {
        return posix__makeerror(EINVAL);
    }

    return posix__makeerror( pthread_mutex_trylock(&mutex->handle_) );
}

nsp_status_t lwp_mutex_timedlock(lwp_mutex_t *mutex, uint32_t expires)
{
    struct timespec abstime;
    int retval;
    uint64_t nsec;

    if (unlikely(!mutex)) {
        return posix__makeerror(EINVAL);
    }

    if (0 == clock_gettime(CLOCK_REALTIME, &abstime)) {
        nsec = abstime.tv_nsec;
        nsec += ((uint64_t) expires * 1000000); /* convert milliseconds to nanoseconds */
        abstime.tv_sec += (nsec / 1000000000);
        abstime.tv_nsec = (nsec % 1000000000);
        retval = pthread_mutex_timedlock(&mutex->handle_, &abstime);
        return posix__makeerror(retval);
    }

    return posix__makeerror(errno);
}

void lwp_mutex_unlock(lwp_mutex_t *mutex)
{
    if ( unlikely(!mutex) ) {
        return;
    }

    pthread_mutex_unlock(&mutex->handle_);
}

/*********************************************************************************************************
*****************************************           event          ***************************************
**********************************************************************************************************/
static nsp_status_t _lwp_event_init(lwp_event_t *evo)
{
    nsp_status_t status;
    pthread_condattr_t condattr;
    int retval;

    /* waitable handle MUST locked by a internal mutex object */
    status = lwp_mutex_init(&evo->mutex_, NO);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = NSP_STATUS_SUCCESSFUL;
    pthread_condattr_init(&condattr);
    do {
        /* using CLOCK_MONOTONIC time check method */
        retval = pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
        if (0 != retval) {
            status = posix__makeerror(retval);
            break;
        }

        /* OK, initial the condition variable now */
        retval = pthread_cond_init(&evo->cond_, &condattr);
        if (0 != retval) {
            status = posix__makeerror(retval);
            break;
        }

        /* initialize the pass condition */
        evo->pass_ = 0;
        evo->effective = 1;
        pthread_condattr_destroy(&condattr);
        return status;
    } while (0);

    lwp_mutex_uninit(&evo->mutex_);
    pthread_condattr_destroy(&condattr);
    return status;
}

nsp_status_t lwp_event_init(lwp_event_t *evo, enum lwp_event_category pattern)
{
    if ( unlikely(!evo) ) {
        return posix__makeerror(EINVAL);
    }

    evo->sync_ = (int)pattern;
    return _lwp_event_init(evo);
}

void lwp_event_uninit(lwp_event_t *evo)
{
    if ( unlikely(!evo) ) {
        return;
    }

    /* It shall be safe to destroy an initialized condition variable upon which no threads are currently blocked.
       Attempting to destroy a condition variable upon which other threads are currently blocked results in undefined behavior.
    */
    lwp_mutex_lock(&evo->mutex_);
    evo->pass_ = 1;
    evo->effective = 0;
    /* I'm going to awaken all threads who keep the condition variable object, no matter synchronous model or notification model. */
    pthread_cond_broadcast(&evo->cond_);
    pthread_cond_destroy(&evo->cond_);
    lwp_mutex_unlock(&evo->mutex_);
    lwp_mutex_uninit(&evo->mutex_);
}

static nsp_status_t _lwp_event_wait_inifinte(lwp_event_t *evo)
{
    int retval;

    retval = 0;
    lwp_mutex_lock(&evo->mutex_);
    do {
        if (!evo->effective) {
            retval = -1;
            break;
        }

        if (evo->sync_) {
            while (!evo->pass_) {
                if (0 != (retval = pthread_cond_wait(&evo->cond_, &evo->mutex_.handle_))) {
                    break;
                }
            }

            /* reset @pass_ flag to zero immediately after wait syscall,
                to maintain semantic consistency with ms-windows-API WaitForSingleObject*/
            if (evo->effective) {
                evo->pass_ = 0;
            } else {
                retval = -1;  /* condition variable object order to exit, maybe destroy request are pending on other thread */
            }
        } else {
            /* for notification waitable handle,
                all thread blocked on wait method will be awaken by pthread_cond_broadcast(3P)(@posix__sig_waitable_handle)
                the object is always in a state of signal before method @posix__reset_waitable_handle called.
                */
            if (!evo->pass_) {
                retval = pthread_cond_wait(&evo->cond_, &evo->mutex_.handle_);
            }

            if (!evo->effective) {
                retval = -1;
            }
        }
    } while (0);
    lwp_mutex_unlock(&evo->mutex_);
    return posix__makeerror(retval);
}

nsp_status_t lwp_event_wait(lwp_event_t *evo, int expire/*ms*/)
{
    int retval;
    struct timespec abstime; /* -D_POSIX_C_SOURCE >= 199703L */
    uint64_t nsec;

    if (unlikely(!evo)) {
        return posix__makeerror(EINVAL);
    }

    /* the evo using infinite wait model */
    if (expire <= 0) {
        return _lwp_event_wait_inifinte(evo);
    }

    /* wait with timeout */
    if (0 != clock_gettime(CLOCK_MONOTONIC, &abstime)) {
         return posix__makeerror(errno);
    }

    /* Calculation delay from current time，if tv_nsec >= 1000000000 will cause pthread_cond_timedwait EINVAL, 64 bit overflow */
    nsec = abstime.tv_nsec;
    nsec += ((uint64_t) expire * 1000000); /* convert milliseconds to nanoseconds */
    abstime.tv_sec += (nsec / 1000000000);
    abstime.tv_nsec = (nsec % 1000000000);

    retval = 0;
    lwp_mutex_lock(&evo->mutex_);
    do {
        if (!evo->effective) {
            retval = -1;
            break;
        }

        if (evo->sync_) {
            while (!evo->pass_) {
                retval = pthread_cond_timedwait(&evo->cond_, &evo->mutex_.handle_, &abstime);
                if (0 != retval) { /* timedout or fatal syscall cause the loop break */
                    break;
                }
            }

            /* reset @pass_ flag to zero immediately after wait syscall,
                to maintain semantic consistency with ms-windows-API WaitForSingleObject*/
            if (evo->effective) {
                evo->pass_ = 0;
            } else {
                retval = -1;  /* condition variable object order to exit, maybe destroy request are pending on other thread */
            }
        } else {
            /* for notification waitable handle,
                all thread blocked on wait method will be awaken by pthread_cond_broadcast(3P)(@posix__sig_waitable_handle)
                the object is always in a state of signal before method @posix__reset_waitable_handle called.
                */
            if (!evo->pass_) {
                retval = pthread_cond_timedwait(&evo->cond_, &evo->mutex_.handle_, &abstime);
            }

            if (!evo->effective) {
                retval = -1;
            }
        }
    } while(0);
    lwp_mutex_unlock(&evo->mutex_);

    return posix__makeerror(retval);
}

nsp_status_t lwp_event_awaken(lwp_event_t *evo)
{
    int retval;

    if (unlikely(!evo)) {
        return posix__makeerror(EINVAL);
    }

    lwp_mutex_lock(&evo->mutex_);
    evo->pass_ = 1;
    retval = (evo->sync_) ? pthread_cond_signal(&evo->cond_) : pthread_cond_broadcast(&evo->cond_);
    lwp_mutex_unlock(&evo->mutex_);

    return posix__makeerror(retval);
}

void lwp_event_block(lwp_event_t *evo)
{
    if (unlikely(!evo)) {
        return;
    }

    /* @reset operation effect only for notification wait object.  */
    if ( 0 == evo->sync_) {
        lwp_mutex_lock(&evo->mutex_);
        evo->pass_ = 0;
        lwp_mutex_unlock(&evo->mutex_);
    }
}

void lwp_hang()
{
    lwp_event_t lwp_event;
    nsp_status_t status;

    status = lwp_event_init(&lwp_event, LWPEC_SYNC);
    if ( unlikely(!NSP_SUCCESS(status)) ) {
        return;
    }

    lwp_event_wait( &lwp_event, -1 );
    lwp_event_uninit(&lwp_event);
}

nsp_status_t lwp_delay(uint64_t microseconds)
{
    int fdset;
    struct timeval tv;

    tv.tv_sec = microseconds / 1000000;
    tv.tv_usec = microseconds % 1000000;

    fdset = select(0, NULL, NULL, NULL, &tv);
    if (fdset < 0) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}
