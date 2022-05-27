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

struct wpool {
    lwp_t thread;
    struct spin_lock sp;
    lwp_event_t signal;
    struct list_head tasks; /* struct wptask::link */
    int task_list_size;
    int actived;
};

struct wptask {
    objhld_t hld;
    struct wpool *thread;
    struct list_head link;
};

static objhld_t __tcphld = -1;
static objhld_t __udphld = -1;

static void __wp_add_task(struct wptask *task)
{
    struct wpool *wpptr;

    if (task) {
        wpptr = task->thread;
        INIT_LIST_HEAD(&task->link);

        /* very brief CPU time consumption, we can use spinlock instead of mutex */
        acquire_spinlock(&wpptr->sp);
        list_add_tail(&task->link, &wpptr->tasks);
        ++wpptr->task_list_size;
        release_spinlock(&wpptr->sp);
    }
}

static struct wptask *__wp_get_task(struct wpool *wpptr)
{
    struct wptask *task;

    task = NULL;

    /* very brief CPU time consumption, we can use spinlock instead of mutex */
    acquire_spinlock(&wpptr->sp);
    if (NULL != (task = list_first_entry_or_null(&wpptr->tasks, struct wptask, link))) {
         --wpptr->task_list_size;
        list_del(&task->link);
    }
    release_spinlock(&wpptr->sp);

    if (task) {
        INIT_LIST_HEAD(&task->link);
    }

    return task;
}

static nsp_status_t __wp_exec(struct wptask *task)
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
                in this case, @__wp_run should not free the memory of this task  */
            status = fifo_pop(ncb, NULL);
            if ( NSP_SUCCESS(status) ) {
                __wp_add_task(task);
            }
        }
    }

    objdefr(ncb->hld);
    return status;
}

static void *__wp_run(void *p)
{
    struct wptask *task;
    struct wpool *wpptr;
    nsp_status_t status;

    wpptr = (struct wpool *)p;
    //mxx_call_ecr("Wp worker:%lld standby.", wpptr->hld);

    while (wpptr->actived) {
        status = lwp_event_wait(&wpptr->signal, 10);
        if (!NSP_SUCCESS_OR_ERROR_EQUAL(status, ETIMEDOUT)) {
            break;
        }
        lwp_event_block(&wpptr->signal);

        /* complete all write task when once signal arrived,
            no matter which thread wake up this wait object */
        while ((NULL != (task = __wp_get_task(wpptr)) ) && wpptr->actived) {
            status = __wp_exec(task);
            if (!NSP_SUCCESS(status)) {
                zfree(task);
            }
        }
    }

    //mxx_call_ecr("Wp worker:%lld terminated.", wpptr->hld);
    pthread_exit((void *) 0);
    return NULL;
}

static nsp_status_t __wp_init(struct wpool *wpptr)
{
    INIT_LIST_HEAD(&wpptr->tasks);
    lwp_event_init(&wpptr->signal, LWPEC_NOTIFY);
    initial_spinlock(&wpptr->sp);
    wpptr->task_list_size = 0;
    wpptr->actived = 1;
    if (lwp_create(&wpptr->thread, 0, &__wp_run, (void *)wpptr) < 0 ) {
        mxx_call_ecr("fatal error occurred syscall pthread_create(3), error:%d", errno);
        return NSP_STATUS_FATAL;
    }

    return NSP_STATUS_SUCCESSFUL;
}

static void __wp_uninit(objhld_t hld, void *udata)
{
    struct wpool *wpptr;
    struct wptask *task;

    wpptr = (struct wpool *)udata;

    /* This is an important judgment condition.
        when @__sync_bool_compare_and_swap failed in @wp_init, the mutex/condition_variable will notbe initialed,
        in this case, wait function block the calling thread and @wp_uninit progress cannot continue */
    if (wpptr->actived) {
        wpptr->actived = 0;
        lwp_event_awaken(&wpptr->signal);
        lwp_join(&wpptr->thread, NULL);

        /* clear the tasks which too late to deal with */
        while (NULL != (task = __wp_get_task(wpptr))) {
            zfree(task);
        }

        INIT_LIST_HEAD(&wpptr->tasks);
        lwp_event_uninit(&wpptr->signal);
    }

    if (!atom_compare_exchange_strong( &__tcphld, &hld, -1)) {
        atom_compare_exchange_strong( &__udphld, &hld, -1);
    }
}

void wp_uninit(int protocol)
{
    objhld_t *hldptr;

    hldptr = ((IPPROTO_TCP ==protocol ) ? &__tcphld : ((IPPROTO_UDP == protocol || ETH_P_ARP == protocol) ? &__udphld : NULL));
    if (hldptr) {
        if (*hldptr >= 0) {
            objclos(*hldptr);
        }
    }
}

nsp_status_t wp_init(int protocol)
{
    int retval;
    struct wpool *wpptr;
    objhld_t hld, expect, *hldptr;
    struct objcreator creator;

    hldptr = ((IPPROTO_TCP ==protocol ) ? &__tcphld :
                ((IPPROTO_UDP == protocol || ETH_P_ARP == protocol) ? &__udphld : NULL));
    if (!hldptr) {
        return posix__makeerror(EPROTOTYPE);
    }

    /* judgment thread-unsafe first, handle the most case of interface rep-calls,
        this case NOT mean a error */
    if (*hldptr >= 0) {
        return EALREADY;
    }

    creator.known = INVALID_OBJHLD;
    creator.size = sizeof(struct wpool);
	creator.initializer = NULL;
	creator.unloader = &__wp_uninit;
	creator.context = NULL;
	creator.ctxsize = 0;
    hld = objallo3(&creator);
    if (hld < 0) {
        return NSP_STATUS_FATAL;
    }

    expect = -1;
    if (!atom_compare_exchange_strong( hldptr, &expect, hld)) {
        objclos(hld);
        return EALREADY;
    }

    wpptr = objrefr(*hldptr);
    if (!wpptr) {
        return posix__makeerror(ENOENT);
    }

    retval = __wp_init(wpptr);
    objdefr(*hldptr);
    return retval;
}

nsp_status_t wp_queued(void *ncbptr)
{
    struct wptask *task;
    struct wpool *wpptr;
    ncb_t *ncb;
    objhld_t hld;
    nsp_status_t status;
    int protocol;

    ncb = (ncb_t *)ncbptr;
    protocol = ncb->protocol;
    hld = ((IPPROTO_TCP == protocol ) ? __tcphld :
                ((IPPROTO_UDP == protocol || ETH_P_ARP == protocol) ? __udphld : -1));
    if ( unlikely(hld < 0)) {
        return posix__makeerror(ENOENT);
    }

    wpptr = objrefr(hld);
    if (!wpptr) {
        return posix__makeerror(ENOENT);
    }

    do {
        if (NULL == (task = (struct wptask *)ztrymalloc(sizeof(struct wptask)))) {
            status = posix__makeerror(ENOMEM);
            break;
        }

        task->hld = ncb->hld;
        task->thread = wpptr;
        __wp_add_task(task);

        /* use local variable to save the thread object, because @task maybe already freed by handler now */
        lwp_event_awaken(&wpptr->signal);
        status = NSP_STATUS_SUCCESSFUL;
    } while (0);

    objdefr(hld);
    return status;
}
