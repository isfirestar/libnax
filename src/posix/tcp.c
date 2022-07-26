#include "tcp.h"

#include "mxx.h"
#include "fifo.h"
#include "io.h"
#include "wpool.h"
#include "pipe.h"

#include "ifos.h"
#include "atom.h"
#include "zmalloc.h"

/*
 *  kernel status of tcpi_state
 *  defined in /usr/include/netinet/tcp.h
 *  enum
 *  {
 *    TCP_ESTABLISHED = 1,
 *    TCP_SYN_SENT,
 *    TCP_SYN_RECV,
 *    TCP_FIN_WAIT1,
 *    TCP_FIN_WAIT2,
 *    TCP_TIME_WAIT,
 *    TCP_CLOSE,
 *    TCP_CLOSE_WAIT,
 *    TCP_LAST_ACK,
 *    TCP_LISTEN,
 *    TCP_CLOSING
 *  };
 */
const char *TCP_KERNEL_STATE_NAME[TCP_KERNEL_STATE_LIST_SIZE] = {
    "TCP_UNDEFINED",
    "TCP_ESTABLISHED",
    "TCP_SYN_SENT",
    "TCP_SYN_RECV",
    "TCP_FIN_WAIT1",
    "TCP_FIN_WAIT2",
    "TCP_TIME_WAIT",
    "TCP_CLOSE",
    "TCP_CLOSE_WAIT",
    "TCP_LAST_ACK",
    "TCP_LISTEN",
    "TCP_CLOSING"
};

static nsp_status_t _tcprefr( objhld_t hld, ncb_t **ncb )
{
    if ( hld < 0 || !ncb) {
        return -EINVAL;
    }

    *ncb = objrefr( hld );
    if ( NULL != (*ncb) ) {
        if ( IPPROTO_TCP == (*ncb)->protocol ) {
            return NSP_STATUS_SUCCESSFUL;
        }

        objdefr( hld );
        *ncb = NULL;
        return posix__makeerror(EPROTOTYPE);
    }

    return posix__makeerror(ENOENT);
}

nsp_status_t tcp_allocate_rx_buffer(ncb_t *ncb)
{
    /* allocate package to save parse result */
    if (!ncb->packet) {
        if (NULL == (ncb->packet = (unsigned char *)ztrymalloc(TCP_BUFFER_SIZE))) {
            mxx_call_ecr("Fails allocate packet memory");
            return posix__makeerror(ENOMEM);
        }
    }

    /* allocate package to storage Rx kernel buffer, this buffer direct post to recv(2) */
    ncb->u.tcp.rx_parse_offset = 0;
    if (!ncb->u.tcp.rx_buffer) {
        if (NULL == (ncb->u.tcp.rx_buffer = (unsigned char *)ztrymalloc(TCP_BUFFER_SIZE))) {
            mxx_call_ecr("Fails allocate Rx buffer memory");
            zfree(ncb->packet);
            ncb->packet = NULL;
            return posix__makeerror(ENOMEM);
        }
    }

    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t _tcp_bind(const ncb_t *ncb)
{
    do {
        if (AF_UNIX == ncb->local_addr.sin_family ) {
            if (0 != bind(ncb->sockfd, (const struct sockaddr *)&ncb->domain_addr, sizeof(ncb->domain_addr))) {
                mxx_call_ecr("Fatal syscall bind(2) on domain,link:%lld,error:%d", ncb->hld, errno);
                return posix__makeerror(errno);
            }
            break;
        }

        /* binding on local address before listen */
        if ( 0 != bind(ncb->sockfd, (struct sockaddr *) &ncb->local_addr, sizeof(struct sockaddr)) ) {
            mxx_call_ecr("Fatal syscall bind(2),link:%lld，errno:%d", ncb->hld, errno);
            return posix__makeerror(errno);
        }
    } while(0);

    return NSP_STATUS_SUCCESSFUL;
}

#define _tcp_invoke(foo)  foo(IPPROTO_TCP)

/* tcp impls */
nsp_status_t tcp_init2(int nprocs)
{
    nsp_status_t status;

    status = io_init(IPPROTO_TCP, nprocs);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = _tcp_invoke(wp_init);
    if ( !NSP_SUCCESS(status) ) {
        _tcp_invoke(io_uninit);
    }

    return status;
}

nsp_status_t tcp_init()
{
    return tcp_init2(0);
}

void tcp_uninit()
{
    _tcp_invoke(ncb_uninit);
    _tcp_invoke(io_uninit);
    _tcp_invoke(wp_uninit);
}

static nsp_status_t _tcp_create_domain(ncb_t *ncb, const char* domain)
{
    int fd;
    int expect;

    fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        mxx_call_ecr("Fatal syscall socket(2) for domain,error:%d", errno);
        return posix__makeerror(errno);
    }

    /* prevent double creation */
    expect = 0;
    if (!atom_compare_exchange_strong(&ncb->sockfd, &expect, fd)) {
        close(fd);
        return posix__makeerror(EEXIST);
    }

    /* local address fill and delay use */
    ncb->local_addr.sin_addr.s_addr = 0;
    ncb->local_addr.sin_family = AF_UNIX;
    ncb->local_addr.sin_port = 0; /* IPC host mark as a client by default */

    /* local domain fill and delay use
     *  we allow use to specify the domain file postpone to @connect */
    memset(&ncb->domain_addr, 0, sizeof(ncb->domain_addr));
    ncb->domain_addr.sun_family = AF_UNIX;
    strncpy(ncb->domain_addr.sun_path, domain, sizeof(ncb->domain_addr.sun_path) - 1);

    mxx_call_ecr("Init domain link:%lld", ncb->hld);
    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t _tcp_create(ncb_t *ncb, const char* ipstr, uint16_t port)
{
    int fd;

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        mxx_call_ecr("Fatal syscall socket(2),error:%d", errno);
        return posix__makeerror(errno);
    }

    ncb->sockfd = fd;

    /* local address fill and delay use */
    ncb->local_addr.sin_addr.s_addr = ipstr ? inet_addr(ipstr) : INADDR_ANY;
    ncb->local_addr.sin_family = AF_INET;
    ncb->local_addr.sin_port = htons(port);

    /* every TCP socket do NOT need graceful close */
    ncb_set_linger(ncb);
    mxx_call_ecr("Success create socket link:%lld, sockfd:%d", ncb->hld, ncb->sockfd);
    return NSP_STATUS_SUCCESSFUL;
}

HTCPLINK tcp_create(tcp_io_fp callback, const char* ipstr, uint16_t port)
{
    ncb_t *ncb;
    objhld_t hld;
    struct objcreator creator;
    nsp_status_t status;

    creator.known = INVALID_OBJHLD;
    creator.size = sizeof(ncb_t);
	creator.initializer = &ncb_allocator;
	creator.unloader = &ncb_deconstruct;
	creator.context = NULL;
	creator.ctxsize = 0;
    hld = objallo3(&creator);
    if (hld < 0) {
        mxx_call_ecr("Insufficient resource for inner object.");
        return INVALID_HTCPLINK;
    }
    ncb = objrefr(hld);
    assert(ncb);

    ncb->hld = hld;
    ncb->protocol = IPPROTO_TCP;
    ncb->nis_callback = callback;

    do {
        if (ipstr) {
            if (0 == strncasecmp(ipstr, "IPC:", 4)) {
                status = _tcp_create_domain(ncb, &ipstr[4]);
                break;
            }
        }

        status = _tcp_create(ncb, ipstr, port);
    } while(0);

    objdefr(hld);
    if (!NSP_SUCCESS(status)) {
        objclos(hld);
        return INVALID_HTCPLINK;
    }

    return ncb->hld;
}

HTCPLINK tcp_create2(tcp_io_fp callback, const char* ipstr, uint16_t port, const tst_t *tst)
{
    HTCPLINK link;
    nsp_status_t status;

    link = tcp_create(callback, ipstr, port);
    if (INVALID_HTCPLINK == link) {
        return INVALID_HTCPLINK;
    }

    if (tst) {
        status = tcp_settst_r(link, tst);
        if ( unlikely(!NSP_SUCCESS(status)) ) {
            tcp_destroy(link);
            link = INVALID_HTCPLINK;
        }
    }

    return link;
}

nsp_status_t tcp_settst(HTCPLINK link, const tst_t *tst)
{
    ncb_t *ncb;
    nsp_status_t status;

    if ( unlikely(!tst) ) {
        return posix__makeerror(EINVAL);
    }

     /* size of tcp template must be less or equal to 32 bytes */
    if ( unlikely(tst->cb_ > TCP_MAXIMUM_TEMPLATE_SIZE) ) {
        mxx_call_ecr("Limit size of tst is 32 byte");
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( likely(NSP_SUCCESS(status)) ) {
        ncb->u.tcp.template.cb_ = tst->cb_;
        ncb->u.tcp.template.builder_ = tst->builder_;
        ncb->u.tcp.template.parser_ = tst->parser_;
        objdefr(link);
    }

    return status;
}

nsp_status_t tcp_settst_r(HTCPLINK link, const tst_t *tst)
{
    ncb_t *ncb;
    nsp_status_t status;

    if ( unlikely(!tst) ) {
        return posix__makeerror(EINVAL);
    }

     /* size of tcp template must be less or equal to 32 bytes */
    if ( unlikely(tst->cb_ > TCP_MAXIMUM_TEMPLATE_SIZE) ) {
        mxx_call_ecr("Limit size of tst is 32 byte");
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( likely(NSP_SUCCESS(status)) ) {
        ncb->u.tcp.prtemplate.cb_ = atom_exchange(&ncb->u.tcp.template.cb_, tst->cb_);
        ncb->u.tcp.prtemplate.builder_ = atom_exchange(&ncb->u.tcp.template.builder_, tst->builder_);
        ncb->u.tcp.prtemplate.parser_ = atom_exchange(&ncb->u.tcp.template.parser_, tst->parser_);
        objdefr(link);
    }
    return status;
}

nsp_status_t tcp_gettst(HTCPLINK link, tst_t *tst)
{
    ncb_t *ncb;
    nsp_status_t status;

    if ( unlikely(!tst) ) {
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( likely(NSP_SUCCESS(status)) ) {
        tst->cb_ = ncb->u.tcp.template.cb_;
        tst->builder_ = ncb->u.tcp.template.builder_;
        tst->parser_ = ncb->u.tcp.template.parser_;
        objdefr(link);
    }

    return status;
}

nsp_status_t tcp_gettst_r(HTCPLINK link, tst_t *tst, tst_t *previous)
{
    ncb_t *ncb;
    nsp_status_t status;
    tst_t local;

    if ( unlikely(!tst) ) {
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( likely(NSP_SUCCESS(status)) ) {
        local.cb_ = atom_exchange(&tst->cb_, ncb->u.tcp.template.cb_);
        local.builder_ = atom_exchange(&tst->builder_, ncb->u.tcp.template.builder_);
        local.parser_ = atom_exchange(&tst->parser_, ncb->u.tcp.template.parser_);
        objdefr(link);

        if (previous) {
            memcpy(previous, &local, sizeof(local));
        }
    }
    return status;
}

/*
 * Object destruction operations may be intended to interrupt some blocking operations. just like @tcp_connect
 * so,close the file descriptor directly, destroy the object by the smart pointer.
 */
void tcp_destroy(HTCPLINK link)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return;
    }

    mxx_call_ecr("Order destroy link:%lld", ncb->hld);
    io_shutdown(ncb, SHUT_RDWR);
    objdefr(link);
}

#if 0

/* <tcp_check_connection_bypoll> */
static int __tcp_check_connection_bypoll(int sockfd)
{
    struct pollfd pofd;
    socklen_t len;
    int error;

    pofd.fd = sockfd;
    pofd.events = POLLOUT;

    while(poll(&pofd, 1, -1) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }

    len = sizeof (error);
    if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        return -1;
    }

    return 0;
}

/* <tcp_check_connection_byselect> */
static int __tcp_check_connection(int sockfd)
{
    int retval;
    socklen_t len;
    struct timeval timeo;
    fd_set rset, wset;
    int error;
    int nfd;

    /* 3 seconds as maximum wait time long*/
    timeo.tv_sec = 3;
    timeo.tv_usec = 0;

    FD_ZERO(&rset);
    FD_SET(sockfd, &rset);
    wset = rset;

    retval = -1;
    len = sizeof (error);
    do {

        /* The nfds argument specifies the range of descriptors to be tested.
         * The first nfds descriptors shall be checked in each set;
         * that is, the descriptors from zero through nfds-1 in the descriptor sets shall be examined.
         */
        nfd = select(sockfd + 1, &rset, &wset, NULL, &timeo);
        if ( nfd <= 0) {
            break;
        }

        if (FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {
            retval = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *) & len);
            if ( retval < 0) {
                break;
            }
            retval = error;
        }
    } while (0);

    return retval;
}

#endif

static nsp_status_t _tcp_connect_domain(ncb_t *ncb, const char *domain)
{
    nsp_status_t status;
    int retval;

    do {
        if (0 == ncb->domain_addr.sun_path[0]) {
            if (!domain) {
                status = posix__makeerror(EINVAL);
                break;
            }

            if (0 == domain[0]) {
                status = posix__makeerror(EINVAL);
                break;
            }

            if (0 != strncasecmp(domain, "IPC:", 4)) {
                status = posix__makeerror(EINVAL);
                break;
            }

            strncpy(ncb->domain_addr.sun_path, &domain[4], sizeof(ncb->domain_addr.sun_path) - 1);
        }

        /* syscall @connect can be interrupted by other signal. */
        do {
            retval = connect(ncb->sockfd, (const struct sockaddr *) &ncb->domain_addr, sizeof(ncb->domain_addr));
        } while((errno == EINTR) && (retval < 0));

        if (retval < 0) {
            /* if this socket is already connected, or it is in listening states, sys-call failed with error EISCONN  */
            mxx_call_ecr("Fatal syscall connect(2) for link:%lld,domain:\"%s\",error:%u", ncb->hld, ncb->domain_addr.sun_path, errno);
            status = posix__makeerror(errno);
            break;
        }

        /* this link use to receive data from remote peer,
            so the packet and rx memory acquire to allocate now */
        status = tcp_allocate_rx_buffer(ncb);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        /* follow tcp rx/tx event */
        atom_set(&ncb->ncb_read, &tcp_rx);
        atom_set(&ncb->ncb_write, &tcp_tx);

        /* focus EPOLLIN only */
        status = io_attach(ncb, EPOLLIN);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        mxx_call_ecr("Link:%lld established domain:%s", ncb->hld,ncb->domain_addr.sun_path);
        ncb_post_connected(ncb);
    }while( 0 );

    return status;
}

static nsp_status_t _tcp_connect(ncb_t *ncb, const char* ipstr, uint16_t port)
{
    struct sockaddr_in addr_to;
    struct tcp_info ktcp;
    nsp_status_t status;
    int retval;

    do {
        if ( unlikely(!ipstr || 0 == port || 0xFFFF == port) ) {
            status = posix__makeerror(EINVAL);
            break;
        }
        if ( unlikely(0 == ipstr[0]) ) {
            status = posix__makeerror(EINVAL);
            break;
        }

        /* get the socket status of tcp_info to check the socket tcp statues */
        status = tcp_save_info(ncb, &ktcp);
        if (NSP_SUCCESS(status)) {
            if (ktcp.tcpi_state != TCP_CLOSE) {
                mxx_call_ecr("Link:%lld, kernel states error:%s.", link, tcp_state2name(ktcp.tcpi_state));
                status = posix__makeerror((TCP_ESTABLISHED == ktcp.tcpi_state) ? EISCONN : EBADFD );
                break;
            }
        }

        /* set time elapse for TCP sender timeout error trigger */
        tcp_set_user_timeout(ncb, 3000);
        /* try no more than 3 times of tcp::syn */
        tcp_set_syncnt(ncb, 3);
        /* On individual connections, the socket buffer size must be set prior to the listen(2) or connect(2) calls in order to have it take effect. */
        ncb_set_buffsize(ncb);
        /* mark normal attributes */
        tcp_set_nodelay(ncb, 1);

        /* bind on particular local address:port tuple when need. */
        status = _tcp_bind(ncb);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        addr_to.sin_family = PF_INET;
        addr_to.sin_port = htons(port);
        addr_to.sin_addr.s_addr = inet_addr(ipstr);

        /* syscall @connect can be interrupted by other signal. */
        do {
            retval = connect(ncb->sockfd, (const struct sockaddr *) &addr_to, sizeof (struct sockaddr));
        } while((errno == EINTR) && (retval < 0));

        if (retval < 0) {
            /* if this socket is already connected, or it is in listening states, sys-call failed with error EISCONN  */
            mxx_call_ecr("Fatal syscall connect(2) for link:%lld,endpoint:\"%s:%u\",error:%u", link, ipstr, port, errno);
            status = posix__makeerror(errno);
            break;
        }

        /* this link use to receive data from remote peer,
            so the packet and rx memory acquire to allocate now */
        status = tcp_allocate_rx_buffer(ncb);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        /* the low-level [TCP Keep-ALive] are usable. */
        tcp_set_keepalive(ncb, 9);

        /* get peer address information */
        tcp_relate_address(ncb);

        /* follow tcp rx/tx event */
        atom_set(&ncb->ncb_read, &tcp_rx);
        atom_set(&ncb->ncb_write, &tcp_tx);

        /* focus EPOLLIN only */
        status = io_attach(ncb, EPOLLIN);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        mxx_call_ecr("Link:%lld connection established to %s:%d",
            ncb->hld, inet_ntoa(ncb->local_addr.sin_addr), ntohs(ncb->local_addr.sin_port));
        ncb_post_connected(ncb);
    }while( 0 );

    return status;
}

nsp_status_t tcp_connect(HTCPLINK link, const char* ipstr, uint16_t port)
{
    ncb_t *ncb;
    nsp_status_t status;

    if (unlikely(link < 0)) {
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = (AF_UNIX == ncb->local_addr.sin_family) ? _tcp_connect_domain(ncb, ipstr) : _tcp_connect(ncb, ipstr, port);
    objdefr(link);
    return status;
}

static nsp_status_t _tcp_connect2_domain(ncb_t *ncb, const char *domain)
{
    nsp_status_t status;
    int retval;
    nsp_status_t (*expect)(struct _ncb *);

    do {
        if (0 == ncb->domain_addr.sun_path[0]) {
            if (!domain) {
                status = posix__makeerror(EINVAL);
                break;
            }

            if (0 == domain[0]) {
                status = posix__makeerror(EINVAL);
                break;
            }

            strncpy(ncb->domain_addr.sun_path, domain, sizeof(ncb->domain_addr.sun_path) - 1);
        }

        /* for asynchronous connect, set file-descriptor to non-blocked mode first */
        status = io_fnbio(ncb->sockfd);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        /* double check the tx_syn routine, decline multithread risk */
        expect = NULL;
        if (!atom_compare_exchange_strong( &ncb->ncb_write, &expect, &tcp_tx_syn)) {
            break;
        }

        do {
            retval = connect(ncb->sockfd, (const struct sockaddr *)&ncb->domain_addr, sizeof(ncb->domain_addr));
        }while((EINTR == errno) && (retval < 0));

        /* immediate success, some BSD/SystemV maybe happen */
        if ( 0 == retval) {
            mxx_call_ecr("Link:%lld established domain:%s", ncb->hld, ncb->domain_addr.sun_path);
            tcp_tx_syn(ncb);
            status = NSP_STATUS_SUCCESSFUL;
            break;
        }

        if (EINPROGRESS == errno ) {
            status = io_attach(ncb, EPOLLOUT);
            break;
        }

        if (EAGAIN == errno) {
            mxx_call_ecr("Insufficient entries in the routing cache, link:%lld", link);
        } else {
            mxx_call_ecr("Fatal syscall connect(2) for link:%lld,domain:\"%s\",error:%u", ncb->hld, ncb->domain_addr.sun_path, errno);
        }
        status = posix__makeerror(errno);
    } while (0);

    return status;
}

static nsp_status_t _tcp_connect2(ncb_t *ncb, const char* ipstr, uint16_t port)
{
    nsp_status_t status;
    struct tcp_info ktcp;
    int retval;
    nsp_status_t (*expect)(struct _ncb *);

    do {
        if ( unlikely(!ipstr || 0 == port || 0xFFFF == port) ) {
            status = posix__makeerror(EINVAL);
            break;
        }
        if ( unlikely(0 == ipstr[0]) ) {
            status = posix__makeerror(EINVAL);
            break;
        }

        status = NSP_STATUS_FATAL;

        /* for asynchronous connect, set file-descriptor to non-blocked mode first */
        status = io_fnbio(ncb->sockfd);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        /* get the socket status of tcp_info to check the socket tcp statues */
        status = tcp_save_info(ncb, &ktcp);
        if (NSP_SUCCESS(status)) {
            if (ktcp.tcpi_state != TCP_CLOSE) {
                mxx_call_ecr("Link:%lld, kernel states error:%s.", link, tcp_state2name(ktcp.tcpi_state));
                status = posix__makeerror((TCP_ESTABLISHED == ktcp.tcpi_state) ? EISCONN : EBADFD);
                break;
            }
        }

        /* set time elapse for TCP sender timeout error trigger */
        tcp_set_user_timeout(ncb, 3000);
        /* try no more than 3 times for tcp::syn */
        tcp_set_syncnt(ncb, 3);
        /* On individual connections, the socket buffer size must be set prior to the listen(2) or connect(2) calls in order to have it take effect. */
        ncb_set_buffsize(ncb);
        /* mark normal attributes */
        tcp_set_nodelay(ncb, 1);

        /* bind on particular local address:port tuple when need. */
        status = _tcp_bind(ncb);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        /* double check the tx_syn routine */
        expect = NULL;
        if (!atom_compare_exchange_strong( &ncb->ncb_write, &expect, &tcp_tx_syn)) {
            break;
        }

        ncb->remot_addr.sin_family = PF_INET;
        ncb->remot_addr.sin_port = htons(port);
        ncb->remot_addr.sin_addr.s_addr = inet_addr(ipstr);

        do {
            retval = connect(ncb->sockfd, (const struct sockaddr *) &ncb->remot_addr, sizeof (struct sockaddr));
        }while((EINTR == errno) && (retval < 0));

        /* immediate success, some BSD/SystemV maybe happen */
        if ( 0 == retval) {
            mxx_call_ecr("Link:%lld connection established to %s:%d",
                ncb->hld, inet_ntoa(ncb->local_addr.sin_addr), ntohs(ncb->local_addr.sin_port));
            tcp_tx_syn(ncb);
            status = NSP_STATUS_SUCCESSFUL;
            break;
        }

        /*
         *  queue object to epoll manage befor syscall @connect,
         *  epoll_wait will get a EPOLLOUT signal when syn success.
         *  so, file descriptor MUST certain be in asynchronous mode before next stage
         *
         *  attach MUST early than connect(2) call,
         *  in some case, very short time after connect(2) called, the EPOLLRDHUP event has been arrived,
         *  if attach not in time, error information maybe lost, then bring the file-descriptor leak.
         *
         *  ncb object willbe destroy on fatal.
         *
         *  EPOLLOUT and EPOLLHUP for asynchronous connect(2):
         *  1.When the connect function is not called locally, but the socket is attach to epoll for detection,
         *       epoll will generate an EPOLLOUT | EPOLLHUP, that is, an event with a value of 0x14
         *   2.When the local connect event occurs, but the connection fails to be established,
         *       epoll will generate EPOLLIN | EPOLLERR | EPOLLHUP, that is, an event with a value of 0x19
         *   3.When the connect function is also called and the connection is successfully established,
         *       epoll will generate EPOLLOUT once, with a value of 0x4, indicating that the socket is writable
        */
        if (EINPROGRESS == errno ) {
            status = io_attach(ncb, EPOLLOUT);
            break;
        }

        if (EAGAIN == errno) {
            mxx_call_ecr("Insufficient entries in the routing cache, link:%lld", link);
        } else {
            mxx_call_ecr("Fatal syscall connect(2) for link:%lld,endpoint:\"%s:%u\",error:%u", link, ipstr, port, errno);
        }
        status = posix__makeerror(errno);
    } while (0);

    return status;
}

nsp_status_t tcp_connect2(HTCPLINK link, const char* ipstr, uint16_t port)
{
    ncb_t *ncb;
    nsp_status_t status;

    if (unlikely(link < 0)) {
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = (AF_UNIX == ncb->local_addr.sin_family) ?  _tcp_connect2_domain(ncb, ipstr) : _tcp_connect2(ncb, ipstr, port);
    objdefr(link);
    return status;
}

nsp_status_t tcp_listen(HTCPLINK link, int block)
{
    ncb_t *ncb;
    struct tcp_info ktcp;
    socklen_t addrlen;
    nsp_status_t status;

    if ( unlikely(link < 0 || block < 0 || block >= 0x7FFF) ) {
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    do {
        status = NSP_STATUS_FATAL;

        /* cope with the domain socket situation */
        if (AF_UNIX == ncb->local_addr.sin_family ) {
            /* server host has resposibility to remove the existing domain file on filesystem before create a socket file-descriptor */
            unlink(ncb->domain_addr.sun_path);
            /* mark this is a server host */
            ncb->local_addr.sin_port = 1;
        }

        /* get the socket status of tcp_info to check the socket tcp statues */
        status = tcp_save_info(ncb, &ktcp);
        if (NSP_SUCCESS(status)) {
            if (ktcp.tcpi_state != TCP_CLOSE) {
                mxx_call_ecr("Link:%lld, kernel states error:%s.", link, tcp_state2name(ktcp.tcpi_state));
                status = posix__makeerror(EBADFD);
                break;
            }
        }

        /* allow port reuse(the same port number binding on different IP address)
         *  this call will always failed when this is a domain unix socket, but we don't care */
        ncb_set_reuseaddr(ncb);

        /* binding on local adpater before listen */
        status =  _tcp_bind(ncb);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        /* On individual connections, the socket buffer size must be set prior to the listen(2) or connect(2) calls in order to have it take effect. */
#if 0
        ncb_set_buffsize(ncb);
#endif
        /* /proc/sys/net/core/somaxconn' in POSIX.1 this value default to 128
         *  so,for ensure high concurrency performance in the establishment phase of the TCP connection,
         *  we will ignore the @block argument and use macro SOMAXCONN which defined in /usr/include/bits/socket.h anyway */
        if ( -1 == listen(ncb->sockfd, ((0 == block) || (block > SOMAXCONN)) ? SOMAXCONN : block) ) {
            mxx_call_ecr("Fatal syscall listen(2),link:%lld,error:%u", link, errno);
            status = posix__makeerror(errno);
            break;
        }

        /* this NCB object is readonly， and it must be used for accept */
        if (NULL != atom_compare_exchange(&ncb->ncb_read, NULL, &tcp_syn)) {
            status = posix__makeerror(EEXIST);
            break;
        }
        atom_set(&ncb->ncb_write, NULL);

        /* set file descriptor to asynchronous mode and attach to it's own epoll object,
         *  ncb object willbe destroy on fatal. */
        status = io_attach(ncb, EPOLLIN);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        /* allow application to listen on the random port,
         * therefor, framework MUST query the real address information for this file descriptor now */
        if (ncb->local_addr.sin_family != AF_UNIX) {
            addrlen = sizeof(struct sockaddr);
            getsockname(ncb->sockfd, (struct sockaddr *) &ncb->local_addr, &addrlen);
            mxx_call_ecr("Link:%lld listen on %s:%d, ", link, inet_ntoa(ncb->local_addr.sin_addr), ntohs(ncb->local_addr.sin_port));
        } else {
            mxx_call_ecr("Link:%lld listen for domain %s, ", link, ncb->domain_addr.sun_path);
        }

    } while (0);

    /*
    if (!NSP_SUCCESS(status)) {
        objclos(link);
    }*/

    objdefr(link);
    return status;
}

nsp_status_t tcp_awaken(HTCPLINK link, const void *pipedata, unsigned int cb)
{
    nsp_status_t status;
    ncb_t *ncb;

    if ( unlikely(link < 0 || !pipedata || 0 == cb)) {
        return posix__makeerror(EINVAL);
    }

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = pipe_write_message(ncb, pipedata, cb);
    objdefr(link);
    return status;
}

nsp_status_t tcp_write(HTCPLINK link, const void *origin, int cb, const nis_serializer_fp serializer)
{
    ncb_t *ncb;
    unsigned char *buffer;
    int packet_length;
    struct tcp_info ktcp;
    struct tx_node *node;
    nsp_status_t status;

    if ( unlikely(link < 0 || cb <= 0 || cb > TCP_MAXIMUM_PACKET_SIZE || !origin)) {
        return -EINVAL;
    }

    buffer = NULL;
    node = NULL;

    status = _tcprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    do {
        status = NSP_STATUS_FATAL;

        /* the following situation maybe occur when tcp_write called:
         * immediately call @tcp_write after @tcp_create, but no connection established and no listening has yet been taken
         * in this situation, @wpool::run_task maybe take a task, but @ncb->ncb_write is ineffectiveness.application may crashed.
         * examine these two parameters to ensure their effectiveness
         */
        if (!ncb->ncb_write || !ncb->ncb_read) {
            status = posix__makeerror(EINVAL);
            break;
        }

        /* get the socket status of tcp_info to check the socket tcp statues */
        status = tcp_save_info(ncb, &ktcp);
        if (NSP_SUCCESS(status)) {
            if (ktcp.tcpi_state != TCP_ESTABLISHED) {
                mxx_call_ecr("Link:%lld, kernel states error:%s.", link, tcp_state2name(ktcp.tcpi_state));
                break;
            }
        }

        /* if @template.builder is not null then use it, otherwise,
            indicate that calling thread want to specify the packet length through input parameter @cb */
        if (!(*ncb->u.tcp.template.builder_) || (ncb->attr & LINKATTR_TCP_NO_BUILD)) {
            packet_length = cb;
            if (NULL == (buffer = (unsigned char *)ztrymalloc(packet_length))) {
                status = posix__makeerror(ENOMEM);
                break;
            }

            /* serialize data into packet or direct use data pointer by @origin */
            if (serializer) {
                status = (*serializer)(buffer, origin, cb);
                if ( !NSP_SUCCESS(status) ) {
                    mxx_call_ecr("Fails on user define serialize.");
                    break;
                }
            } else {
                memcpy(buffer, origin, cb);
            }

        } else {
            packet_length = cb + ncb->u.tcp.template.cb_;
            if (NULL == (buffer = (unsigned char *)ztrymalloc(packet_length))) {
                status = posix__makeerror(ENOMEM);
                break;
            }

            /* build protocol head */
            status = (*ncb->u.tcp.template.builder_)(buffer, cb);
            if (!NSP_SUCCESS(status)) {
                mxx_call_ecr("Fails on user tst builder");
                break;
            }

            /* serialize data into packet or direct use data pointer by @origin */
            if (serializer) {
                status = (*serializer)(buffer + ncb->u.tcp.template.cb_, origin, cb);
                if (!NSP_SUCCESS(status)) {
                    mxx_call_ecr("Fails on user define serialize.");
                    break;
                }
            } else {
                memcpy(buffer + ncb->u.tcp.template.cb_, origin, cb );
            }
        }

        if (NULL == (node = (struct tx_node *)ztrymalloc(sizeof (struct tx_node)))) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        memset(node, 0, sizeof(struct tx_node));
        node->data = buffer;
        node->wcb = packet_length;
        node->offset = 0;

        if (!fifo_tx_overflow(ncb)) {

            /* the write buffer is full, active EPOLLOUT and waitting for epoll event trigger
             * at this point, we need to deal with the queue header node and restore the unprocessed node back to the queue header.
             * the way 'oneshot' focus on the write operation completion point
             *
             * the return value means direct failed when it equal to -1 or success when it greater than zero.
             * in these case, destroy memory resource outside loop, no matter what the actually result it is.
             */
            status = tcp_txn(ncb, node);

            /* break if success or failed without EAGAIN */
            if (!NSP_FAILED_AND_ERROR_EQUAL(status, EAGAIN)) {
                break;
            }
        }

        /*
         * 1. when the IO state is blocking, any send or write call certain to be fail immediately,
         *
         * 2. the meaning of -EAGAIN return by @tcp_txn is send or write operation cannot be complete immediately,
         *      IO state should change to blocking now
         *
         * one way to handle the above two aspects, queue data to the tail of fifo manager, preserve the sequence of output order
         * in this case, memory of @buffer and @node cannot be destroy until asynchronous completed
         *
         * after @fifo_queue success called, IO blocking flag is set, and EPOLLOUT event has been associated with ncb object.
         * wpool thread canbe awaken by any kernel cache writable event trigger
         *
         * on failure of function call, @node and it's owned buffer MUST be free
         */
        status = fifo_queue(ncb, node);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        objdefr(link);
        return status;
    } while (0);

    if (likely(buffer)) {
        zfree(buffer);
    }

    if (likely(node)) {
        zfree(node);
    }

    objdefr(link);

    /* @fifo_queue may raise a EBUSY error indicate the user-level cache of sender is full.
     * in this case, current send request is going to be ignore but link shall not be close.
     * otherwise, close link nomatter what error code it is
    if ( !NSP_SUCCESS_OR_ERROR_EQUAL(status, EBUSY)) {
        objclos(link);
    }*/

    return status;
}

nsp_status_t tcp_getaddr(HTCPLINK link, int type, uint32_t* ipv4, uint16_t* port)
{
    ncb_t *ncb;
    struct sockaddr_in *addr;
    nsp_status_t status;

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = posix__makeerror(EINVAL);
    addr = NULL;

    if (AF_UNIX == ncb->local_addr.sin_family) {
        status = posix__makeerror(EPROTOTYPE);
    } else {
        addr = (LINK_ADDR_LOCAL == type) ? &ncb->local_addr :
            ((LINK_ADDR_REMOTE == type) ? &ncb->remot_addr : NULL);
    }

    if (addr) {
        if (ipv4) {
            *ipv4 = htonl(addr->sin_addr.s_addr);
        }
        if (port) {
            *port = htons(addr->sin_port);
        }
        status = NSP_STATUS_SUCCESSFUL;
    }

    objdefr(link);
    return status;
}

nsp_status_t tcp_getipcpath(HTCPLINK link, const char **path)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = posix__makeerror(EINVAL);

    if (AF_UNIX == ncb->local_addr.sin_family) {
        if (path) {
            *path = ncb->domain_addr.sun_path;
            status = NSP_STATUS_SUCCESSFUL;
        }
    }
    objdefr(link);
    return status;
}

nsp_status_t tcp_setopt(HTCPLINK link, int level, int opt, const char *val, int len)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    if (-1 == setsockopt(ncb->sockfd, level, opt, (const void *) val, (socklen_t) len)) {
        mxx_call_ecr("fatal error occurred syscall setsockopt(2) with level:%d optname:%d,error:%d", level, opt, errno);
        status = posix__makeerror(errno);
    }

    objdefr(link);
    return status;
}

nsp_status_t tcp_getopt(HTCPLINK link, int level, int opt, char *__restrict val, int *len)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _tcprefr(link, &ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    if ( -1 == getsockopt(ncb->sockfd, level, opt, (void * __restrict)val, (socklen_t *) len) ) {
        mxx_call_ecr("fatal error occurred syscall getsockopt(2) with level:%d optname:%d,error:%d", level, opt, errno);
        status = posix__makeerror(errno);
    }

    objdefr(link);
    return status;
}

nsp_status_t tcp_save_info(const ncb_t *ncb, struct tcp_info *ktcp)
{
    socklen_t len;

    len = sizeof (struct tcp_info);
    return (0 == getsockopt(ncb->sockfd, IPPROTO_TCP, TCP_INFO, (void * __restrict)ktcp, &len)) ?
         NSP_STATUS_SUCCESSFUL : posix__makeerror(errno);
}

nsp_status_t tcp_setmss(const ncb_t *ncb, int mss)
{
    return ( 0 == setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_MAXSEG, (const void *) &mss, sizeof (mss)) ) ?
         NSP_STATUS_SUCCESSFUL : posix__makeerror(errno);
}

nsp_status_t tcp_getmss(const ncb_t *ncb)
{
    socklen_t lenmss;

    lenmss = sizeof (ncb->u.tcp.mss);
    return (0 == getsockopt(ncb->sockfd, IPPROTO_TCP, TCP_MAXSEG, (void *__restrict)&ncb->u.tcp.mss, &lenmss)) ?
         NSP_STATUS_SUCCESSFUL : posix__makeerror(errno);
}

nsp_status_t tcp_set_nodelay(const ncb_t *ncb, int set)
{
    return ( 0 == setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_NODELAY, (const void *) &set, sizeof (set)) ) ?
         NSP_STATUS_SUCCESSFUL : posix__makeerror(errno);
}

nsp_status_t tcp_get_nodelay(const ncb_t *ncb, int *nodelay)
{
    socklen_t optlen;

    optlen = sizeof (int);
    return ( 0 == getsockopt(ncb->sockfd, IPPROTO_TCP, TCP_NODELAY, (void *__restrict)nodelay, &optlen) ) ?
         NSP_STATUS_SUCCESSFUL : posix__makeerror(errno);
}

nsp_status_t tcp_set_cork(const ncb_t *ncb, int set)
{
    return (0 == setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_CORK, (const void *) &set, sizeof ( set))) ?
        NSP_STATUS_SUCCESSFUL : posix__makeerror(errno);
}

nsp_status_t tcp_get_cork(const ncb_t *ncb, int *set)
{
    socklen_t optlen;

    optlen = sizeof (int);
    return (0 == getsockopt(ncb->sockfd, IPPROTO_TCP, TCP_CORK, (void *__restrict)set, &optlen)) ?
         NSP_STATUS_SUCCESSFUL : posix__makeerror(errno);
}

nsp_status_t tcp_set_keepalive(const ncb_t *ncb, int interval)
{
    int optval;

    optval = 1;
    if ( setsockopt(ncb->sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char *)&optval, sizeof(optval)) < 0 ) {
        mxx_call_ecr("fatal syscall setsockopt(2) on level:%d, name:%d, errno:%d, link:%lld", SOL_SOCKET, SO_KEEPALIVE, errno, ncb->sockfd);
        return posix__makeerror(errno);
    }

#if __linux__
    /* Default settings are more or less garbage, with the keepalive time
     * set to 7200 by default on Linux. Modify settings to make the feature
     * actually useful. */

    /* Send first probe after interval(in seconds). */
    optval = interval;
    if (setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_KEEPIDLE, &optval, sizeof(optval)) < 0) {
        mxx_call_ecr("fatal syscall setsockopt(2) on level:%d, name:%d, errno:%d, link:%lld", IPPROTO_TCP, TCP_KEEPIDLE, errno, ncb->sockfd);
        return posix__makeerror(errno);
    }

    /* Send next probes after the specified interval. Note that we set the
     * delay as interval / 3, as we send three probes before detecting
     * an error (see the next setsockopt call). */
    optval = interval / 3;
    if (optval == 0) {
        optval = 1;
    }
    if (setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_KEEPINTVL, &optval, sizeof(optval)) < 0) {
        mxx_call_ecr("fatal syscall setsockopt(2) on level:%d, name:%d, errno:%d, link:%lld", IPPROTO_TCP, TCP_KEEPINTVL, errno, ncb->sockfd);
        return posix__makeerror(errno);
    }

    /* Consider the socket in error state after three we send three ACK
     * probes without getting a reply. */
    optval = 3;
    if (setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_KEEPCNT, &optval, sizeof(optval)) < 0) {
        mxx_call_ecr("fatal syscall setsockopt(2) on level:%d, name:%d, errno:%d, link:%lld", IPPROTO_TCP, TCP_KEEPCNT, errno, ncb->sockfd);
        return posix__makeerror(errno);
    }
#endif /* __linux__ */

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t tcp_set_syncnt(const ncb_t *ncb, int cnt)
{
    if ( setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_SYNCNT, (const void *)&cnt, sizeof(cnt)) < 0 ) {
        mxx_call_ecr("fatal syscall setsockopt(2) on level:%d, name:%d, errno:%d, link:%lld", IPPROTO_TCP, TCP_SYNCNT, errno, ncb->sockfd);
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t tcp_set_user_timeout(const ncb_t *ncb, unsigned int uto)
{
    if ( setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_USER_TIMEOUT, (const char *)&uto, sizeof(uto)) < 0 ) {
        mxx_call_ecr("fatal syscall setsockopt(2) on level:%d, name:%d, errno:%d, link:%lld", IPPROTO_TCP, TCP_USER_TIMEOUT, errno, ncb->sockfd);
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t tcp_set_quickack(const ncb_t *ncb, int set)
{
    if ( setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_QUICKACK, (const char *)&set, sizeof(set)) < 0 ) {
        mxx_call_ecr("fatal syscall setsockopt(2) on level:%d, name:%d, errno:%d, link:%lld", IPPROTO_TCP, TCP_QUICKACK, errno, ncb->sockfd);
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t tcp_setattr(HTCPLINK link, int attr, int enable)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _tcprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    switch(attr) {
        case LINKATTR_TCP_FULLY_RECEIVE:
        case LINKATTR_TCP_NO_BUILD:
        case LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT:
            (enable > 0) ? (ncb->attr |= attr) : (ncb->attr &= ~attr);
            status = NSP_STATUS_SUCCESSFUL;
            break;
        default:
            status = posix__makeerror(EINVAL);
            break;
    }

    objdefr(link);
    return status;
}

nsp_status_t tcp_getattr(HTCPLINK link, int attr, int *enabled)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _tcprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    if (ncb->attr & attr) {
        *enabled = 1;
    } else {
        *enabled = 0;
    }

    objdefr(link);
    return status;
}

void tcp_setattr_r(ncb_t *ncb, int attr)
{
    atom_exchange(&ncb->attr, attr);
}

void tcp_getattr_r(ncb_t *ncb, int *attr)
{
    atom_exchange(attr, ncb->attr);
}

void tcp_relate_address(ncb_t *ncb)
{
    socklen_t addrlen;

    assert(ncb);

    /* get peer address information */
    addrlen = sizeof (struct sockaddr);
    /* remote address information */
    if ( 0 != getpeername(ncb->sockfd, (struct sockaddr *) &ncb->remot_addr, &addrlen) ) {
        mxx_call_ecr("fatal error occurred syscall getpeername(2) with error:%d, on link:%lld", errno, link);
    }
    /* local address information */
    if (0 != getsockname(ncb->sockfd, (struct sockaddr *) &ncb->local_addr, &addrlen) ) {
        mxx_call_ecr("fatal error occurred syscall getsockname(2) with error:%d, on link:%lld", errno, link);
    }
}
