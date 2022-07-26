#include "wpool.h"

#include "object.h"

#include "threading.h"
#include "atom.h"
#include "ifos.h"

#include "ncb.h"
#include "tcp.h"
#include "mxx.h"
#include "fifo.h"
#include "zmalloc.h"
#include "spinlock.h"
#include "refs.h"

struct wpool;
struct wptask {
    objhld_t hld;
    struct wpool *poolptr;
    struct list_head link;
};

struct wpool {
    refs_t ref;
    lwp_t thread;
    struct spin_lock sp;
    lwp_event_t signal;
    struct list_head tasks; /* struct wptask::link */
    int task_list_size;
    int actived;
};

struct wp_manager
{
    struct wpool *_wptcp;
    struct wpool *_wpudp;
    lwp_mutex_t mutex;
};
static struct wp_manager _wpmgr = {
    ._wptcp = NULL,
    ._wpudp = NULL,
    .mutex = { PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP },
     };

#define _wp_locate_protocol(p) (((IPPROTO_TCP == (p)) ? &_wpmgr._wptcp : ((IPPROTO_UDP == (p)) ? &_wpmgr._wpudp : NULL)))

static struct wpool *_wp_safe_retain(int protocol)
{
    struct wpool *poolptr, **locate;

    poolptr = NULL;

    lwp_mutex_lock(&_wpmgr.mutex);
    locate = _wp_locate_protocol(protocol);
    if (locate) {
        poolptr = *locate;
        if ( unlikely(ref_retain(&poolptr->ref)) <= 0) {
            poolptr = NULL;
        }
    }
    lwp_mutex_unlock(&_wpmgr.mutex);

    return poolptr;
}

static void _wp_safe_release(struct wpool *poolptr)
{
    lwp_mutex_lock(&_wpmgr.mutex);
    ref_release(&poolptr->ref);
    lwp_mutex_unlock(&_wpmgr.mutex);
}


static void _wp_add_task(struct wptask *task)
{
    struct wpool *poolptr;

    if (task) {
        poolptr = task->poolptr;
        INIT_LIST_HEAD(&task->link);

        /* very brief CPU time consumption, we can use spinlock instead of mutex */
        acquire_spinlock(&poolptr->sp);
        list_add_tail(&task->link, &poolptr->tasks);
        ++poolptr->task_list_size;
        release_spinlock(&poolptr->sp);
    }
}

static struct wptask *_wp_get_task(struct wpool *poolptr)
{
    struct wptask *task;

    task = NULL;

    /* very brief CPU time consumption, we can use spinlock instead of mutex */
    acquire_spinlock(&poolptr->sp);
    if (NULL != (task = list_first_entry_or_null(&poolptr->tasks, struct wptask, link))) {
         --poolptr->task_list_size;
        list_del(&task->link);
    }
    release_spinlock(&poolptr->sp);

    if (task) {
        INIT_LIST_HEAD(&task->link);
    }

    return task;
}

static nsp_status_t _wp_exec(struct wptask *task)
{
    nsp_status_t status;
    ncb_t *ncb;
    ncb_rw_t ncb_write;

    ncb = objrefr(task->hld);
    if (!ncb) {
        return posix__makeerror(ENOENT);
    }

    status = NSP_STATUS_FATAL;
    ncb_write = atom_get(&ncb->ncb_write);
    if (ncb_write) {
        /*
         * if the return value of @ncb_write equal to -1, that means system call maybe error, this link will be close
         *
         * if the return value of @ncb_write equal to -EAGAIN, set write IO blocked. this ncb object willbe switch to focus on EPOLLOUT | EPOLLIN
         * bacause the write operation object always takes place in the same thread context, there is no thread security problem.
         * for the data which has been reverted, write tasks will be obtained through event triggering of EPOLLOUT
         *
         * if the return value of @ncb_write equal to zero, it means the queue of pending data node is empty, not any send operations are need.
         * here can be consumed the task where allocated by kTaskType_TxOrder sucessful completed
         *
         * if the return value of @ncb_write greater than zero, it means the data segment have been written to system kernel
         * @retval is the total bytes that have been written
         */
        status = ncb_write(ncb);
        if (!NSP_SUCCESS(status)) {
            /* when EAGAIN occurred or no item in fifo now, wait for next EPOLLOUT event, just ok */
            if (NSP_FAILED_AND_ERROR_EQUAL(status, EAGAIN) || NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT)) {
                ;
            } else {
                objclos(ncb->hld); /* fatal error cause by syscall, close this link */
            }
        } else {
            /* on success, we need to append task to the tail of @fifo again, until all pending data have been sent
                in this case, @_wp_run should not free the memory of this task  */
            status = fifo_pop(ncb, NULL);
            if ( NSP_SUCCESS(status) ) {
                _wp_add_task(task);
            }
        }
    }

    objdefr(ncb->hld);
    return status;
}

static void *_wp_run(void *p)
{
    struct wptask *task;
    struct wpool *poolptr;
    nsp_status_t status;

    poolptr = (struct wpool *)p;
    //mxx_call_ecr("Wp worker:%lld standby.", poolptr->hld);

    while (poolptr->actived) {
        status = lwp_event_wait(&poolptr->signal, 10);
        if (!NSP_SUCCESS_OR_ERROR_EQUAL(status, ETIMEDOUT)) {
            break;
        }
        lwp_event_block(&poolptr->signal);

        /* complete all write task when once signal arrived,
            no matter which thread wake up this wait object */
        while ((NULL != (task = _wp_get_task(poolptr)) ) && poolptr->actived) {
            status = _wp_exec(task);
            if (!NSP_SUCCESS(status)) {
                zfree(task);
            }
        }
    }

    //mxx_call_ecr("Wp worker:%lld terminated.", poolptr->hld);
    pthread_exit((void *) 0);
    return NULL;
}

static nsp_status_t _wp_init(struct wpool *poolptr)
{
    INIT_LIST_HEAD(&poolptr->tasks);
    lwp_event_init(&poolptr->signal, LWPEC_NOTIFY);
    initial_spinlock(&poolptr->sp);
    poolptr->task_list_size = 0;
    poolptr->actived = 1;
    if (lwp_create(&poolptr->thread, 0, &_wp_run, (void *)poolptr) < 0 ) {
        mxx_call_ecr("fatal error occurred syscall pthread_create(3), error:%d", errno);
        return NSP_STATUS_FATAL;
    }

    return NSP_STATUS_SUCCESSFUL;
}

static void _wp_uninit(struct wpool *poolptr)
{
    struct wptask *task;

    /* This is an important judgment condition.
        when @__sync_bool_compare_and_swap failed in @wp_init, the mutex/condition_variable will notbe initialed,
        in this case, wait function block the calling thread and @wp_uninit progress cannot continue */
    if (poolptr->actived) {
        poolptr->actived = 0;
        lwp_event_awaken(&poolptr->signal);
        lwp_join(&poolptr->thread, NULL);

        /* clear the tasks which too late to deal with */
        while (NULL != (task = _wp_get_task(poolptr))) {
            zfree(task);
        }

        INIT_LIST_HEAD(&poolptr->tasks);
        lwp_event_uninit(&poolptr->signal);
    }
}

void wp_uninit(int protocol)
{
    struct wpool *poolptr, **locate;

    locate = _wp_locate_protocol(protocol);
    if (!locate) {
        return;
    }

    poolptr = *locate;
    if (!atom_compare_exchange_strong(locate, poolptr, NULL)) {
        return;
    }

    lwp_mutex_lock(&_wpmgr.mutex);
    ref_close(&poolptr->ref);
    lwp_mutex_unlock(&_wpmgr.mutex);
}

static void _wp_close_protocol(refs_t *ref)
{
    struct wpool *poolptr;

    poolptr = container_of(ref, struct wpool, ref);
    if (poolptr) {
        _wp_uninit(poolptr);
        zfree(poolptr);
    }
}

nsp_status_t wp_init(int protocol)
{
    nsp_status_t status;
    struct wpool *poolptr, *expect, **locate;

    locate = _wp_locate_protocol(protocol);
    if (unlikely(!locate)) {
        return posix__makeerror(EPROTOTYPE);
    }

    poolptr = (struct wpool *)ztrymalloc(sizeof(*poolptr));
    if (!poolptr) {
        return posix__makeerror(ENOMEM);
    }
    ref_init(&poolptr->ref, &_wp_close_protocol);

    expect = NULL;
    if (!atom_compare_exchange_strong( locate, &expect, poolptr)) {
        zfree(poolptr);
        return EALREADY;
    }

    lwp_mutex_lock(&_wpmgr.mutex);
    status = _wp_init(poolptr);
    if (unlikely(!NSP_SUCCESS(status))) {
        atom_set(locate, NULL);
        ref_close(&poolptr->ref);
    }
    lwp_mutex_unlock(&_wpmgr.mutex);
    return status;
}

nsp_status_t wp_queued(void *ncbptr)
{
    struct wptask *task;
    struct wpool *poolptr;
    ncb_t *ncb;
    nsp_status_t status;
    int protocol;

    ncb = (ncb_t *)ncbptr;
    protocol = ncb->protocol;

    poolptr = _wp_safe_retain(protocol);
    if (!poolptr) {
        return posix__makeerror(EPROTOTYPE);
    }

    do {
        if (NULL == (task = (struct wptask *)ztrymalloc(sizeof(struct wptask)))) {
            status = posix__makeerror(ENOMEM);
            break;
        }

        task->hld = ncb->hld;
        task->poolptr = poolptr;
        _wp_add_task(task);

        /* use local variable to save the thread object, because @task maybe already freed by handler now */
        lwp_event_awaken(&poolptr->signal);
        status = NSP_STATUS_SUCCESSFUL;
    } while (0);

    _wp_safe_release(poolptr);
    return status;
}
