#include "io.h"

#include <fcntl.h>

#include <sys/signal.h>
#include <signal.h>

#include "threading.h"
#include "atom.h"
#include "ifos.h"
#include "zmalloc.h"
#include "sharedptr.h"

#include "ncb.h"
#include "wpool.h"
#include "mxx.h"
#include "pipe.h"

/* 1024 is just a hint for the kernel */
#define EPOLL_SIZE    (1024)

struct epoll_object_block {
    int epfd;
    nsp_boolean_t actived;
    lwp_t lwp;
    lwp_event_t exit;
    int pipefdw;
    pid_t tid;
} ;

struct io_object_block {
    struct epoll_object_block *epoptr;
    int nprocs;
    int protocol;
};

struct io_manager {
    sharedptr_pt tcpio;
    sharedptr_pt udpio;
    lwp_mutex_t mutex;
    enum SharedPtrState tcp_available;
    enum SharedPtrState udp_available;
};
static struct io_manager _iomgr = {
    .tcpio = NULL,
    .udpio = NULL,
    .mutex = { PTHREAD_MUTEX_INITIALIZER },
    .tcp_available = SPS_CLOSED,
    .udp_available = SPS_CLOSED };

static void _io_rdhup(ncb_t *ncb)
{
    int rx_pending;

    /* log peer closed */
    mxx_call_ecr( "Lnk:%lld, EPOLLRDHUP", ncb->hld );

    /* detach socket from epoll manager before close(2) invoke */
    if (likely(ncb->epfd > 0)) {
        io_detach(ncb);
        ncb->epfd = -1;
    }

    /* shutdown(2) shall be invoked before this event triggered */
    if (likely(ncb->sockfd > 0)) {

        /* before socket close, we MUST clear data which residual and pending in kernel buffer.
         * consider of below client behavior:
         *
         * fd = socket();
         * if (connect(fd) == 0) {
         *      send(fd);
         *      close(fd);
         * }
         *
         * we can assume most situation, server peer have not enought time to push client sock into EPOLL queue before send and close
         * therefore, data send from client will pending in kernel buffer.RDHUP will arrived before EPOLLIN, this may cause data drop.
         * */
        if (0 == ioctl(ncb->sockfd, FIONREAD, &rx_pending)) {
            if (rx_pending > 0 && ncb->ncb_read) {
                ncb->ncb_read(ncb);
            }
        }

        /* close socket in system layer */
        shutdown(ncb->sockfd, SHUT_RDWR);
        close(ncb->sockfd);
        ncb->sockfd = -1;
    }

    /* acquire to destroy object item */
    objclos(ncb->hld);
}

/*
EPOLL enevts define in and copy form: /usr/include/x86_64-linux-gnu/sys/epoll.h
enum EPOLL_EVENTS
  {
    EPOLLIN = 0x001,
#define EPOLLIN EPOLLIN
    EPOLLPRI = 0x002,
#define EPOLLPRI EPOLLPRI
    EPOLLOUT = 0x004,
#define EPOLLOUT EPOLLOUT
    EPOLLRDNORM = 0x040,
#define EPOLLRDNORM EPOLLRDNORM
    EPOLLRDBAND = 0x080,
#define EPOLLRDBAND EPOLLRDBAND
    EPOLLWRNORM = 0x100,
#define EPOLLWRNORM EPOLLWRNORM
    EPOLLWRBAND = 0x200,
#define EPOLLWRBAND EPOLLWRBAND
    EPOLLMSG = 0x400,
#define EPOLLMSG EPOLLMSG
    EPOLLERR = 0x008,
#define EPOLLERR EPOLLERR
    EPOLLHUP = 0x010,
#define EPOLLHUP EPOLLHUP
    EPOLLRDHUP = 0x2000,
#define EPOLLRDHUP EPOLLRDHUP
    EPOLLEXCLUSIVE = 1u << 28,
#define EPOLLEXCLUSIVE EPOLLEXCLUSIVE
    EPOLLWAKEUP = 1u << 29,
#define EPOLLWAKEUP EPOLLWAKEUP
    EPOLLONESHOT = 1u << 30,
#define EPOLLONESHOT EPOLLONESHOT
    EPOLLET = 1u << 31
#define EPOLLET EPOLLET
  };
*/

static void _iorun(const struct epoll_event *eventptr)
{
    ncb_t *ncb;
    objhld_t hld;
    nsp_status_t status;
    ncb_rw_t ncb_read;
    int error;

    do {
        hld = (objhld_t)eventptr->data.u64;
        ncb = (ncb_t *)objrefr(hld);
        if (unlikely(!ncb)) {
            return;
        }

        /* below errors could be happen:
         *  ECONNRESET(104) : Connection reset by peer.   client Ctrl+C terminate process
         *  EPIPE(32)       : broken pipe
         *
         *  Error  condition happened on the associated file descriptor.  This event is also reported for the write
         *     end of a pipe when the read end has been closed.  epoll_wait(2) will always report for this  event;  it
         *     is not necessary to set it in events.
         */
        if ( unlikely(eventptr->events & EPOLLERR) ) {
            if ( ncb_query_link_error(ncb, &error) >= 0 ) {
                mxx_call_ecr("Lnk:%lld, EPOLLERR:%d", hld, error);
            }
            objclos(hld);
            break;
        }

        /* EPOLLRDHUP indicate: (disconnect/error/reset socket states have been detect,)
         *  1. remote peer call close(2) or shutdown(2) with SHUT_WR
         *  2. local peer call shutdown(2) with SHUT_RD
         * EPOLLHUP indicate:
         *  1. local peer call shutdown(2) with SHUT_RDWR, !!!NOT CLOSE(2)
         *  2. local peer call shutdown(2) with SHUT_WR and remote peer call shutdown(2) with SHUT_WR
         *  3. remote peer give a RST signal */
        if (eventptr->events & EPOLLRDHUP) {
            _io_rdhup(ncb);
            break;
        }

        /* system width input cache change from empty to readable */
        if (eventptr->events & EPOLLIN) {
            ncb_read = atom_get(&ncb->ncb_read);
            if ( likely(ncb_read) ) {
                status = ncb_read(ncb);
                if (!NSP_SUCCESS_OR_ERROR_EQUAL(status, EAGAIN)) {
                    objclos(ncb->hld);
                }
            }
        }

        /* system width output cache change from full to writeable */
        if (eventptr->events & EPOLLOUT) {

            /* concern but not deal with EPOLLHUP
             * every connect request should trigger a EPOLLHUP event, no matter successful or failed
             * EPOLLHUP
             *  Hang up happened on the associated file descriptor.  epoll_wait(2) will always wait for this event;
             *  it is not necessary to set it in events.
             *
             * Notes: that when reading from a channel such as a pipe or a stream socket,
             *  this event merely indicates that the peer closed its end of the channel.
             * Subsequent reads from the channel will return 0 (end of file) only after all outstanding data in
             *   the channel has been consumed.
             *
             * EPOLLOUT and EPOLLHUP for asynchronous connect(2)
             * 1.When the connect function is not called locally, but the socket is attach to epoll for detection,
             *   epoll will generate an EPOLLOUT | EPOLLHUP, that is, an event with a value of 0x14
             * 2.When the local connect event occurs, but the connection fails to be established,
             *   epoll will generate EPOLLIN | EPOLLERR | EPOLLHUP, that is, an event with a value of 0x19
             * 3.When the connect function is also called and the connection is successfully established,
             *   epoll will generate EPOLLOUT once, with a value of 0x4, indicating that the socket is writable
             */
            if ( 0 == (eventptr->events & EPOLLHUP) ) {
                 wp_queued(ncb);
            } else {
                mxx_call_ecr("Lnk:%lld,Unkonwn epoll event:%d", hld, eventptr->events);
            }
        }
    } while (0);

    objdefr(hld);
}

static void *_epoll_proc(void *argv)
{
    static const int EP_TIMEDOUT = -1;//1000;
    struct epoll_event evts[EPOLL_SIZE];
    int sigcnt;
    struct epoll_object_block *epoptr;
    int i;

    epoptr = (struct epoll_object_block *)argv;
    assert(NULL != epoptr);

    mxx_call_ecr("Lwp for Ep:%d", epoptr->epfd);
    epoptr->tid = ifos_gettid();

    while (YES == epoptr->actived) {
        sigcnt = epoll_wait(epoptr->epfd, evts, EPOLL_SIZE, EP_TIMEDOUT);
        if (sigcnt < 0) {
    	    /* The call was interrupted by a signal handler before either :
    	     * (1) any of the requested events occurred or
    	     * (2) the timeout expired; */
            if (EINTR == errno) {
                continue;
            }

            mxx_call_ecr("Fatal syscall epoll_wait(2), epfd:%d, error:%d", epoptr->epfd, errno);
            break;
        }

        /* at least one signal is awakened,
            otherwise, timeout trigger. */
        for (i = 0; i < sigcnt; i++) {
            _iorun(&evts[i]);
        }
    }

    mxx_call_ecr("Lwp exit Ep:%d", epoptr->epfd);
    lwp_exit( (void *)0 );
    return NULL;
}

static enum SharedPtrState *_io_protocol_stat(int protocol)
{
    enum SharedPtrState *statpp;

    statpp = NULL;
    if (IPPROTO_TCP == protocol) {
        statpp = &_iomgr.tcp_available;
    } else if (IPPROTO_UDP == protocol) {
        statpp = &_iomgr.udp_available;
    } else {
        ;
    }

    return statpp;
}

static struct io_object_block *_io_retain_obp(int protocol, int available, sharedptr_pt *sptr)
{
    sharedptr_pt *targetpp;
    struct io_object_block *obptr;

    targetpp = NULL;
    if (IPPROTO_TCP == protocol) {
        targetpp = &_iomgr.tcpio;
    } else if (IPPROTO_UDP == protocol) {
        targetpp = &_iomgr.udpio;
    } else {
        return NULL;
    }

    obptr = NULL;
    lwp_mutex_lock(&_iomgr.mutex);
    do {
        if (available != atom_get( _io_protocol_stat(protocol)) ) {
            mxx_call_ecr("Unknown protocol:%d,available:%d", protocol, available);
            break;
        }

        *sptr = atom_get(targetpp);
        if (NULL == *sptr) {
            break;
        }

        obptr = (struct io_object_block *)ref_retain(*sptr);
    } while(0);
    lwp_mutex_unlock(&_iomgr.mutex);

    return obptr;
}

static nsp_status_t _io_init(struct io_object_block *obptr)
{
    int i;
    struct epoll_object_block *epoptr;

    obptr->epoptr = (struct epoll_object_block *)ztrycalloc(sizeof(struct epoll_object_block) * obptr->nprocs);
    if (!obptr->epoptr) {
        return posix__makeerror(ENOMEM);
    }

    for (i = 0; i < obptr->nprocs; i++) {
        epoptr = &obptr->epoptr[i];
        epoptr->epfd = epoll_create(EPOLL_SIZE); /* kernel don't care about the parameter @size, but request it MUST be large than zero */
        if (epoptr->epfd < 0) {
            mxx_call_ecr("Fatal syscall epoll_create(2), error:%d", errno);
            continue;
        }

        /* @actived is the flag for io thread terminate */
        epoptr->actived = YES;
        if (lwp_create(&epoptr->lwp, 0, &_epoll_proc, epoptr) < 0) {
            mxx_call_ecr("Fatal syscall pthread_create(3), error:%d", errno);
            close(epoptr->epfd);
            epoptr->epfd = -1;
            epoptr->actived = NO;
            continue;
        }
    }

     /* function @io_attach will be invoke during @pipe_create called, so the epoll file-descriptor must create before it */
    for (i = 0; i < obptr->nprocs; i++) {
        epoptr = &obptr->epoptr[i];
        /* create a pipe object for this thread */
        pipe_create(obptr->protocol, &epoptr->pipefdw);
    }

    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t _io_exit_epo(struct epoll_object_block *epoptr)
{
    struct pipe_package_head pipemsg;
    ssize_t n;

    n = 0;
    if (YES == epoptr->actived) {
        /* mark exit */
        epoptr->actived = NO;
        /* awaken thread thougth a pipe message*/
        pipemsg.length = 0;
        pipemsg.link = -1;
        n = write(epoptr->pipefdw, &pipemsg, sizeof(pipemsg));
    }

    return n > 0 ? NSP_STATUS_SUCCESSFUL : NSP_STATUS_FATAL;
}

static void _io_uninit(struct io_object_block *obptr)
{
    int i;
    struct epoll_object_block *epoptr;

    for (i = 0; i < obptr->nprocs; i++) {
        _io_exit_epo(&obptr->epoptr[i]);
    }

    /* waitting for thread safe terminated */
    for (i = 0; i < obptr->nprocs; i++) {
        epoptr = &obptr->epoptr[i];
        lwp_join(&epoptr->lwp, NULL);

        if (epoptr->epfd > 0){
            close(epoptr->epfd);
            epoptr->epfd = -1;
        }
    }

    zfree(obptr->epoptr);
    obptr->epoptr = NULL;
}

nsp_status_t io_init(int protocol, int nprocs)
{
    nsp_status_t status;
    struct io_object_block *obptr;
    sharedptr_pt sptr, *targetpp;
    enum SharedPtrState expect;
    enum SharedPtrState *statpp;

    targetpp = NULL;
    if (IPPROTO_TCP == protocol) {
        targetpp = &_iomgr.tcpio;
    } else if (IPPROTO_UDP == protocol) {
        targetpp = &_iomgr.udpio;
    } else {
        return posix__makeerror(EINVAL);
    }
    statpp = _io_protocol_stat(protocol);

    expect = SPS_CLOSED;
    if (!atom_compare_exchange_strong(statpp, &expect, SPS_AVAILABLE)) {
        return EEXIST;
    }

    sptr = ref_makeshared(sizeof(struct io_object_block));
    if ( unlikely(!sptr)) {
        atom_set(statpp, SPS_CLOSED);
        return posix__makeerror(ENOMEM);
    }

    /* retain io-object entity body */
    obptr = (struct io_object_block *)ref_retain(sptr);
    *targetpp = sptr;

    /* determine how many threads are there IO module acquire */
    obptr->protocol = protocol;
    if (0 == nprocs) {
        obptr->nprocs = ifos_getnprocs();
        if (IPPROTO_TCP != protocol ) {
            obptr->nprocs >>= 1;
        }
    } else {
        obptr->nprocs = (nprocs < 0) ? 1 : nprocs;
    }
    status = _io_init(obptr);

    ref_release(sptr);

    if ( unlikely(!NSP_SUCCESS(status)) ) {
        atom_set(statpp, SPS_CLOSED);
        ref_close(sptr);
    }

    return posix__makeerror(status);
}

void io_uninit(int protocol)
{
    sharedptr_pt sptr;
    struct io_object_block *obptr;
    enum SharedPtrState expect;
    enum SharedPtrState *statpp;

    statpp = _io_protocol_stat(protocol);
    expect = SPS_AVAILABLE;
    if (!atom_compare_exchange_strong(statpp, &expect, SPS_CLOSING)) {
        return;
    }

    obptr = _io_retain_obp(protocol, SPS_CLOSING, &sptr);
    if (likely(obptr)) {
        _io_uninit(obptr);
        ref_release(sptr);
    }
    ref_close(sptr);
    atom_set(statpp, SPS_CLOSED);
}

nsp_status_t io_setfl(int fd, int test)
{
    int fr;
    int opt;

    opt = fcntl(fd, F_GETFL);
    if (-1 == opt) {
        mxx_call_ecr("Fatal syscall fcntl(2),error:%d", errno);
        return posix__makeerror(errno);
    }

    if ( 0 == (opt & test) ) {
        fr = fcntl(fd, F_SETFL, opt | test);
        if (-1 == fr) {
            mxx_call_ecr("Fatal syscall fcntl(2),error:%d", errno);
            return posix__makeerror(errno);
        }
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t io_fnbio(int fd)
{
    int opt;
    nsp_status_t status;

    status = io_setfl(fd, O_NONBLOCK);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    opt = fcntl(fd, F_GETFD);
    if (opt < 0) {
        mxx_call_ecr("Fatal syscall fcntl(2),error:%d", errno);
        return posix__makeerror(errno);
    }

    /* to disable the port inherit when fork/exec */
    if (0 == (opt & FD_CLOEXEC)) {
        if (fcntl(fd, F_SETFD, opt | FD_CLOEXEC) < 0) {
            mxx_call_ecr("Fatal syscall fcntl(2),error:%d", errno);
            return posix__makeerror(errno);
        }
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t io_attach(void *ncbptr, int mask)
{
    struct epoll_event epevt;
    ncb_t *ncb;
    nsp_status_t status;
    sharedptr_pt sptr;
    struct io_object_block *obptr;
    struct epoll_object_block *epoptr;

    ncb = (ncb_t *)ncbptr;

    obptr = _io_retain_obp(ncb->protocol, SPS_AVAILABLE, &sptr);
    if (unlikely(!obptr)) {
        return posix__makeerror(ENOENT);
    }

    do {
        status = io_fnbio(ncb->sockfd);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        memset(&epevt, 0, sizeof(epevt));
        epevt.data.u64 = (uint64_t)ncb->hld;
        epevt.events = (EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
    	epevt.events |= mask;

        epoptr = &obptr->epoptr[ncb->hld % obptr->nprocs];
    	ncb->epfd = epoptr->epfd;
        if ( epoll_ctl(ncb->epfd, EPOLL_CTL_ADD, ncb->sockfd, &epevt) < 0 &&
                errno != EEXIST ) {
            mxx_call_ecr("Fatal syscall epoll_ctl(2),link:%lld,sockfd:%d,epfd:%d,mask:%d,error:%d",
                ncb->hld, ncb->sockfd, ncb->epfd, mask, errno);
            ncb->epfd = -1;
    	} else {
            ncb->rx_tid = epoptr->tid;
            mxx_call_ecr("Success associate link:%lld,sockfd:%d,epfd:%d", ncb->hld, ncb->sockfd, ncb->epfd);
        }
    } while(0);
    ref_release(sptr);
	return ((ncb->epfd < 0) ? NSP_STATUS_FATAL : NSP_STATUS_SUCCESSFUL);
}

nsp_status_t io_modify(void *ncbptr, int mask )
{
    struct epoll_event epevt;
    ncb_t *ncb;

    ncb = (ncb_t *)ncbptr;
    epevt.data.u64 = (uint64_t)ncb->hld;
    epevt.events = (EPOLLET | EPOLLRDHUP | EPOLLHUP | EPOLLERR);
	epevt.events |= mask;

    if (epoll_ctl(ncb->epfd, EPOLL_CTL_MOD, ncb->sockfd, &epevt) < 0 ) {
        mxx_call_ecr("Fatal syscall epoll_ctl(2) link:%lld,sockfd:%d,epfd:%d,mask:%d,error:%d",
            ncb->hld, ncb->sockfd, ncb->epfd, mask, errno);
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

void io_detach(void *ncbptr)
{
    struct epoll_event evt;
    ncb_t *ncb;

    ncb = (ncb_t *)ncbptr;
    if (likely(ncb)) {
        if (epoll_ctl(ncb->epfd, EPOLL_CTL_DEL, ncb->sockfd, &evt) < 0) {
            mxx_call_ecr("Fatal syscall epoll_ctl(2) link:%lld,sockfd:%d,epfd:%d,mask:%d,error:%d",
                ncb->hld, ncb->sockfd, ncb->epfd, errno);
        }
        ncb->epfd = -1;
        ncb->rx_tid = 0;
    }
}

void io_close(void *ncbptr)
{
    ncb_t *ncb;

    ncb = (ncb_t *)ncbptr;
    if (unlikely(!ncb)){
        return;
    }

    if (likely(ncb->sockfd > 0)) {

        /* It is necessary to ensure that the SOCKET descriptor is removed from the EPOLL before closing the SOCKET,
           otherwise the epoll_wait function has a thread security problem and the behavior is not defined.

           While one thread is blocked in a call to epoll_pwait(2),
           it is possible for another thread to add a file descriptor to the waited-upon epoll instance.
           If the new file descriptor becomes ready, it will cause the epoll_wait(2) call to unblock.
            For a discussion of what may happen if a file descriptor in an epoll instance being monitored by epoll_wait(2) is closed in another thread,
            see select(2)

            If a file descriptor being monitored by select(2) is closed in another thread,
            the result is unspecified. On some UNIX systems, select(2) unblocks and returns,
            with an indication that the file descriptor is ready (a subsequent I/O operation will likely fail with an error,
            unless another the file descriptor reopened between the time select(2) returned and the I/O operations was performed).
            On Linux (and some other systems), closing the file descriptor in another thread has no effect on select(2).
            In summary, any application that relies on a particular behavior in this scenario must be considered buggy
        */
        if (ncb->epfd > 0){
            io_detach(ncb);
            ncb->epfd = -1;
        }

        shutdown(ncb->sockfd, SHUT_RDWR);
        close(ncb->sockfd);
        ncb->sockfd = -1;
    }
}

nsp_status_t io_pipefd(void *ncbptr, int *pipefd)
{
    ncb_t *ncb;
    sharedptr_pt sptr;
    struct io_object_block *obptr;
    nsp_status_t status;

    ncb = (ncb_t *)ncbptr;
    obptr = _io_retain_obp(ncb->protocol, SPS_AVAILABLE, &sptr);
    if (unlikely(!obptr)) {
        return posix__makeerror(ENOENT);
    }

    *pipefd = obptr->epoptr[ncb->hld % obptr->nprocs].pipefdw;
    status = *pipefd > 0 ? NSP_STATUS_SUCCESSFUL : posix__makeerror(EBADFD);

    ref_release(sptr);
    return status;
}

nsp_status_t io_shutdown(void *ncbptr, int how)
{
    ncb_t *ncb;

    ncb = (ncb_t *)ncbptr;
    if (-1 == shutdown(ncb->sockfd, how)) {
        mxx_call_ecr("Fatal syscall shutdown(2),link:%lld,sockfd:%d,error:%d", ncb->hld, ncb->sockfd, errno);
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}
