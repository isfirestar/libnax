#if !defined NSP_SPINLOCK_H
#define NSP_SPINLOCK_H

#include "compiler.h"

#if _WIN32
#include <Windows.h>
#else
#include <sched.h>
#endif

#define SPIN_LOCK_IDLE          (0)
#define SPIN_LOCK_BUSY          (1)
#define SPIN_LOCK_ROTATING      (2)

struct spin_lock
{
    volatile unsigned int slock;
#ifdef CONFIG_DEBUG_SPINLOCK
    unsigned int magic;
#endif /* !CONFIG_DEBUG_SPINLOCK */
#ifdef CONFIG_PREEMPT
    unsigned int break_lock;
#endif /* !CONFIG_PREEMPT */
};

#define SPIN_LOCK_INITIALIZER   { SPIN_LOCK_IDLE }

#if _WIN32

__forceinline void initial_spinlock(struct spin_lock* sp) /*__attribute__((always_inline))*/
{
    InterlockedExchange((volatile LONG *)&sp->slock, SPIN_LOCK_IDLE);
}

__forceinline void acquire_spinlock(struct spin_lock* sp)
{
    while (SPIN_LOCK_IDLE != InterlockedCompareExchange((volatile LONG *)&sp->slock, SPIN_LOCK_BUSY, SPIN_LOCK_IDLE)) {
        ;
    }
}

__forceinline void acquire_spinlock_yield(struct spin_lock* sp)
{
    while (SPIN_LOCK_IDLE != InterlockedCompareExchange((volatile LONG*)&sp->slock, SPIN_LOCK_BUSY, SPIN_LOCK_IDLE)) {
        SwitchToThread();
    }
}

__forceinline nsp_status_t acquire_spinlock_test(struct spin_lock* sp)
{
    if (SPIN_LOCK_IDLE == InterlockedCompareExchange((volatile LONG*)&sp->slock, SPIN_LOCK_BUSY, SPIN_LOCK_IDLE)) {
        return NSP_STATUS_SUCCESSFUL;
    }
    return NSP_STATUS_FATAL;
}

__forceinline void release_spinlock(struct spin_lock* sp)
{
    InterlockedExchange((volatile LONG*)&sp->slock, SPIN_LOCK_IDLE);
}

#else   /* blow are the POSIX specification */

static __inline__ void initial_spinlock(struct spin_lock *sp) /*__attribute__((always_inline))*/
{
    __atomic_exchange_n( &sp->slock, SPIN_LOCK_IDLE, __ATOMIC_SEQ_CST);
}

static __inline__ void acquire_spinlock(struct spin_lock *sp)
{
    int expect;

    expect = SPIN_LOCK_IDLE;
    while (!__atomic_compare_exchange_n( &sp->slock, &expect, SPIN_LOCK_BUSY, 1, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        expect = SPIN_LOCK_IDLE;
    }
}

static __inline__ void acquire_spinlock_yield(struct spin_lock *sp)
{
    int expect;

    expect = SPIN_LOCK_IDLE;
    while (!__atomic_compare_exchange_n( &sp->slock, &expect, SPIN_LOCK_BUSY, 1, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        expect = SPIN_LOCK_IDLE;

        /*  sched_yield(2)  causes  the  calling thread to relinquish the CPU.
            The thread is moved to the end of the queue for its static priority and a new thread gets to run */
        sched_yield();
    }
}

static __inline__ int acquire_spinlock_test(struct spin_lock *sp)
{
    int expect;

    expect = SPIN_LOCK_IDLE;
    if ( __atomic_compare_exchange_n( &sp->slock, &expect, SPIN_LOCK_BUSY, 1, __ATOMIC_RELEASE, __ATOMIC_ACQUIRE)) {
        return NSP_STATUS_SUCCESSFUL;
    }
    return NSP_STATUS_FATAL;
}

static __inline__ void release_spinlock(struct spin_lock *sp)
{
    __atomic_exchange_n( &sp->slock, SPIN_LOCK_IDLE, __ATOMIC_SEQ_CST);
}

#endif

#endif /* NSP_SPINLOCK_H */
