#include "ncb.h"
#include "mxx.h"
#include "fifo.h"
#include "io.h"
#include "zmalloc.h"

#include <pthread.h>
#include <stdio.h>

static LIST_HEAD(nl_head);
static pthread_mutex_t nl_head_locker = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static int nl_count = 0;

static void _ncb_post_preclose(const ncb_t *ncb);
static void _ncb_post_closed(const ncb_t *ncb);

/* ncb uninit proc will dereference all ncb object and try to going to close phase.
 */
void ncb_uninit(int protocol)
{
    ncb_t *ncb;
    struct list_head *root, *cursor, *n;
    int i;
    int nl_count_proto;
    objhld_t *hlds;

    root = &nl_head;
    hlds = NULL;
    nl_count_proto = 0;
    cursor = NULL;
    n = NULL;

    /* duplicate all pending objects, and than try to close it */
    pthread_mutex_lock(&nl_head_locker);
    do {
        if (nl_count <= 0 ) {
            break;
        }

        hlds = (objhld_t *)ztrymalloc(nl_count * sizeof(objhld_t));
        if (!hlds) {
            break;
        }

        list_for_each_safe(cursor, n, root) {
            ncb = containing_record(cursor, ncb_t, nl_entry);
            if (ncb->protocol == protocol) {
                list_del(&ncb->nl_entry);
                INIT_LIST_HEAD(&ncb->nl_entry);
                hlds[nl_count_proto] = ncb->hld;
                nl_count_proto++;
            }
        }
    } while(0);
    pthread_mutex_unlock(&nl_head_locker);

    if (hlds && nl_count_proto > 0) {
        for (i = 0 ; i < nl_count_proto; i++) {
            mxx_call_ecr("link:%lld close by ncb uninit", ncb->hld);
            objclos(hlds[i]);
        }
        zfree(hlds);
    }
}

int ncb_allocator(void *udata, const void *ctx, int ctxcb)
{
    ncb_t *ncb;

    ncb = (ncb_t *)udata;
    /* initialize to zero for security reason */
    memset(ncb, 0, sizeof (ncb_t));
    /* initialize the FIFO structure */
    fifo_init(ncb);
    /* insert this ncb node into gloabl nl_head */
    pthread_mutex_lock(&nl_head_locker);
    list_add_tail(&ncb->nl_entry, &nl_head);
    /* increase global item count */
    nl_count++;
    pthread_mutex_unlock(&nl_head_locker);
    return 0;
}

void ncb_deconstruct(objhld_t ignore, void *p)
{
    ncb_t *ncb;

    ILLEGAL_PARAMETER_STOP(!p);

    ncb = (ncb_t *) p;

    /* post pre close event to calling thread, and than,
        Invalidate the user context pointer, trust calling has been already handled and free @ncb->context  */
    _ncb_post_preclose(ncb);
    __atomic_store_n(&ncb->context, NULL, __ATOMIC_RELEASE);

    /* stop network service:
     * 1. cancel relation from epoll, if related
     * 2. shutdown socket, if file-descriptor effective
     * 3. close file descriptor, if effective
     *  */
    io_close(ncb);

    /* if this is a domain socket server, we need to unlink target from filesystem */
    if ( AF_UNIX == ncb->local_addr.sin_family  &&
        1 == ncb->local_addr.sin_port)
    {
        unlink(ncb->domain_addr.sun_path);
    }

    /* free packet cache */
    if (ncb->rx_buffer) {
        zfree(ncb->rx_buffer);
        ncb->rx_buffer = NULL;
    }

    if (ncb->u.tcp.rx_parse_buffer && IPPROTO_TCP == ncb->protocol ) {
        zfree(ncb->u.tcp.rx_parse_buffer);
        ncb->u.tcp.rx_parse_buffer = NULL;

        if (ncb_lb_marked(ncb)) {
            zfree(ncb->u.tcp.lbdata);
        }
        ncb->u.tcp.lbdata = NULL;
        ncb->u.tcp.lbsize = 0;
        ncb->u.tcp.lboffset = 0;
    }

    /* clear all packages pending in send queue */
    fifo_uninit(ncb);

    /* remove entry from global nl_head */
    pthread_mutex_lock(&nl_head_locker);
    list_del_init(&ncb->nl_entry);
    assert(nl_count > 0);
    nl_count--;
    pthread_mutex_unlock(&nl_head_locker);

    /* post close event to calling thread */
    _ncb_post_closed(ncb);

    /* set callback function to ineffectiveness */
    ncb->nis_callback = NULL;

    mxx_call_ecr("link:%lld finalization released",ncb->hld);
}

nsp_status_t ncb_set_rcvtimeo(const ncb_t *ncb, long long ms)
{
    struct timeval tv;

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    return SYSCALL_ZERO_SUCCESS_CHECK(setsockopt(ncb->sockfd, SOL_SOCKET, SO_RCVTIMEO, (const void *)&tv, sizeof(tv)));
}

nsp_status_t ncb_set_sndtimeo(const ncb_t *ncb, long long ms)
{
    struct timeval tv;

    tv.tv_sec = ms / 1000;
    tv.tv_usec = (ms % 1000) * 1000;

    return SYSCALL_ZERO_SUCCESS_CHECK(setsockopt(ncb->sockfd, SOL_SOCKET, SO_SNDTIMEO, (const void *)&tv, sizeof(tv)));
}

nsp_status_t ncb_get_rcvtimeo(const ncb_t *ncb)
{
    socklen_t optlen;

    optlen = sizeof(ncb->rcvtimeo);
    return SYSCALL_ZERO_SUCCESS_CHECK(getsockopt(ncb->sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *)&ncb->rcvtimeo, &optlen));
}

nsp_status_t ncb_get_sndtimeo(const ncb_t *ncb)
{
    socklen_t optlen;

    optlen = sizeof(ncb->sndtimeo);
    return SYSCALL_ZERO_SUCCESS_CHECK(getsockopt(ncb->sockfd, SOL_SOCKET, SO_SNDTIMEO, (void *)&ncb->sndtimeo, &optlen));
}

nsp_status_t ncb_set_iptos(const ncb_t *ncb, int tos)
{
    unsigned char type_of_service = (unsigned char )tos;

    ILLEGAL_PARAMETER_CHECK(0 == tos);

    return SYSCALL_ZERO_SUCCESS_CHECK(setsockopt(ncb->sockfd, SOL_IP, IP_TOS, (const void *)&type_of_service, sizeof(type_of_service)));
}

nsp_status_t ncb_get_iptos(const ncb_t *ncb)
{
    socklen_t optlen;

    optlen = sizeof(ncb->iptos);

    return SYSCALL_ZERO_SUCCESS_CHECK(getsockopt(ncb->sockfd, SOL_IP, IP_TOS, (void *__restrict)&ncb->iptos, &optlen));
}

nsp_status_t ncb_set_window_size(const ncb_t *ncb, int dir, int size)
{
    return SYSCALL_ZERO_SUCCESS_CHECK( setsockopt(ncb->sockfd, SOL_SOCKET, dir, (const void *)&size, sizeof(size)));
}

nsp_status_t ncb_get_window_size(const ncb_t *ncb, int dir, int *size)
{
    socklen_t optlen;

    optlen = sizeof(int);
    return SYSCALL_ZERO_SUCCESS_CHECK(getsockopt(ncb->sockfd, SOL_SOCKET, dir, (void *__restrict)size, &optlen));
}

nsp_status_t ncb_set_linger(const ncb_t *ncb)
{
    struct linger lgr;

    lgr.l_onoff = 1;
    lgr.l_linger = 0;

    return SYSCALL_ZERO_SUCCESS_CHECK(setsockopt(ncb->sockfd, SOL_SOCKET, SO_LINGER, (char *)&lgr, sizeof (struct linger)));
}

nsp_status_t ncb_get_linger(const ncb_t *ncb, int *onoff, int *lin)
{
    struct linger lgr;
    socklen_t optlen;
    nsp_status_t status;

    optlen = sizeof (lgr);

    status = SYSCALL_ZERO_SUCCESS_CHECK(getsockopt(ncb->sockfd, SOL_SOCKET, SO_LINGER, (void *__restrict) & lgr, &optlen));
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    if (onoff) {
        *onoff = lgr.l_onoff;
    }

    if (lin){
        *lin = lgr.l_linger;
    }

    return status;
}

void ncb_set_buffsize(const ncb_t *ncb)
{
    int size;
    static const int MINIMUM_RCVBUF = 65535;
    static const int MINIMUM_SNDBUF = 8192;

    /* define in:
     /proc/sys/net/ipv4/tcp_me
     /proc/sys/net/ipv4/tcp_wmem
     /proc/sys/net/ipv4/tcp_rmem */
    if ( NSP_SUCCESS(ncb_get_window_size(ncb, SO_RCVBUF, &size)) ) {
        mxx_call_ecr("link:%lld, current receive buffer size=%d", ncb->hld, size);
        if (size < MINIMUM_RCVBUF) {
            ncb_set_window_size(ncb, SO_RCVBUF, MINIMUM_RCVBUF);
        }
    }

    if ( NSP_SUCCESS(ncb_get_window_size(ncb, SO_SNDBUF, &size)) ) {
        mxx_call_ecr("link:%lld, current send buffer size=%d",ncb->hld, size);
        if (size < MINIMUM_SNDBUF) {
            ncb_set_window_size(ncb, SO_SNDBUF, MINIMUM_SNDBUF);
        }
    }
}

nsp_status_t ncb_set_reuseaddr(const ncb_t *ncb)
{
    int reuse;

    reuse = 1;
    return SYSCALL_ZERO_SUCCESS_CHECK(setsockopt(ncb->sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse, sizeof(reuse)));
}

nsp_status_t ncb_query_link_error(const ncb_t *ncb, int *err)
{
    socklen_t errlen;
    int error;

    error = 0;
    errlen = sizeof(error);
    if ( -1 == getsockopt(ncb->sockfd, SOL_SOCKET, SO_ERROR, &error, &errlen) ) {
        error = errno;
        mxx_call_ecr("error by syscall getsockopt(2), error:%d,link:%lld", error, ncb->hld);
    }

    if (likely(err)) {
        *err = error;
    }
    return posix__makeerror(error);
}

static void _ncb_post_preclose(const ncb_t *ncb)
{
    nis_event_t c_event;
    tcp_data_t c_data;

    ILLEGAL_PARAMETER_STOP(!ncb->nis_callback);

    c_event.Ln.Tcp.Link = ncb->hld;
    c_event.Event = EVT_PRE_CLOSE;
    c_data.e.PreClose.Context = ncb->context;
    ncb->nis_callback(&c_event, &c_data);
}

static void _ncb_post_closed(const ncb_t *ncb)
{
    nis_event_t c_event;

    ILLEGAL_PARAMETER_STOP(!ncb->nis_callback);

    c_event.Ln.Tcp.Link = ncb->hld;
    c_event.Event = EVT_CLOSED;
    ncb->nis_callback(&c_event, NULL);
}

void ncb_post_recvdata(const ncb_t *ncb,  int cb, const unsigned char *data)
{
    nis_event_t c_event;
    tcp_data_t c_data;

    ILLEGAL_PARAMETER_STOP(!ncb->nis_callback);

    c_event.Ln.Tcp.Link = (HTCPLINK) ncb->hld;
    c_event.Event = EVT_RECEIVEDATA;
    c_data.e.Packet.Size = cb;
    c_data.e.Packet.Data = data;
    ncb->nis_callback(&c_event, &c_data);
}

void ncb_post_pipedata(const ncb_t *ncb,  int cb, const unsigned char *data)
{
    nis_event_t c_event;
    tcp_data_t c_data;

    ILLEGAL_PARAMETER_STOP(!ncb->nis_callback);

    c_event.Ln.Tcp.Link = (HTCPLINK) ncb->hld;
    c_event.Event = EVT_PIPEDATA;
    c_data.e.Packet.Size = cb;
    c_data.e.Packet.Data = data;
    ncb->nis_callback(&c_event, &c_data);
}

void ncb_post_accepted(const ncb_t *ncb, HTCPLINK link)
{
    nis_event_t c_event;
    tcp_data_t c_data;

    ILLEGAL_PARAMETER_STOP(!ncb->nis_callback);

    c_event.Event = EVT_TCP_ACCEPTED;
    c_event.Ln.Tcp.Link = ncb->hld;
    c_data.e.Accept.AcceptLink = link;
    ncb->nis_callback(&c_event, &c_data);
}

void ncb_post_connected(const ncb_t *ncb)
{
    nis_event_t c_event;

    ILLEGAL_PARAMETER_STOP(!ncb->nis_callback);

    c_event.Event = EVT_TCP_CONNECTED;
    c_event.Ln.Tcp.Link = ncb->hld;
    ncb->nis_callback(&c_event, NULL);
}

int ncb_recvdata(ncb_t *ncb, void *data, size_t datalen, struct sockaddr *addr, socklen_t addrlen)
{
    int cb;
    struct msghdr msg;
    struct iovec iov[1];

    iov[0].iov_base = data ? data : ncb->rx_buffer;
    iov[0].iov_len = (data && datalen > 0) ? datalen : ncb->rx_buffer_size;

    ILLEGAL_PARAMETER_CHECK(!iov[0].iov_base || iov[0].iov_len <= 0);

    msg.msg_name = addr;
    msg.msg_namelen = addrlen;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    SYSCALL_WHILE_EINTR(cb, recvmsg(ncb->sockfd, &msg, (ncb->attr & LINKATTR_NONBLOCK) ? MSG_DONTWAIT : 0 ));

    return cb < 0 ? posix__makeerror(errno) : cb;
}

int ncb_senddata(ncb_t *ncb, const void *data, size_t datalen, const struct sockaddr *addr, socklen_t addrlen)
{
    int cb;
    struct msghdr msg;
    struct iovec iov[1];

    iov[0].iov_base = (void *)data;
    iov[0].iov_len = datalen;

    ILLEGAL_PARAMETER_CHECK(!iov[0].iov_base || iov[0].iov_len <= 0);

    msg.msg_name = (void *)addr;
    msg.msg_namelen = addrlen;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = NULL;
    msg.msg_controllen = 0;
    msg.msg_flags = 0;

    SYSCALL_WHILE_EINTR(cb, sendmsg(ncb->sockfd, &msg, MSG_NOSIGNAL | ((ncb->attr & LINKATTR_NONBLOCK) ? MSG_DONTWAIT : 0)));

    return cb < 0 ? posix__makeerror(errno) : cb;
}

nsp_status_t ncb_set_nonblock(ncb_t *ncb, int set)
{
    if (set) {
        if (ncb->nis_callback) {
            return io_set_nonblock(ncb->sockfd, 1);
        } else {
            return posix__makeerror(EINVAL);
        }
    } else {
        return io_set_nonblock(ncb->sockfd, 0);
    }
}

int ncb_setattr_r(ncb_t *ncb, int attr)
{
    int oldattr;
    nsp_status_t status;

    oldattr = __atomic_exchange_n(&ncb->attr, attr, __ATOMIC_ACQ_REL);

    if ((attr & LINKATTR_NONBLOCK) && !(oldattr & LINKATTR_NONBLOCK)) {
        status = ncb_set_nonblock(ncb, 1);
        if (NSP_SUCCESS(status)) {
            __atomic_or_fetch(&ncb->attr, LINKATTR_NONBLOCK, __ATOMIC_SEQ_CST);
        } else {
            __atomic_and_fetch(&ncb->attr, ~LINKATTR_NONBLOCK, __ATOMIC_SEQ_CST);
            return posix__makeerror(status);
        }
    }

    if (!(attr & LINKATTR_NONBLOCK) && (oldattr & LINKATTR_NONBLOCK)) {
        status = ncb_set_nonblock(ncb, 0);
        if (NSP_SUCCESS(status)) {
            __atomic_and_fetch(&ncb->attr, ~LINKATTR_NONBLOCK, __ATOMIC_SEQ_CST);
        } else {
            __atomic_or_fetch(&ncb->attr, LINKATTR_NONBLOCK, __ATOMIC_SEQ_CST);
            return posix__makeerror(status);
        }
    }

    return oldattr;
}

int ncb_getattr_r(ncb_t *ncb)
{
    return __atomic_load_n(&ncb->attr, __ATOMIC_ACQUIRE);
}
