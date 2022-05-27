#include "tcp.h"

#include <poll.h>

#include "mxx.h"
#include "fifo.h"
#include "io.h"
#include "atom.h"

static nsp_status_t __tcp_syn_try(ncb_t *ncb_server, int *clientfd)
{
    struct sockaddr_in addr_income;
    socklen_t addrlen;
    int incomefd;

    addrlen = sizeof ( addr_income);
    incomefd = accept(ncb_server->sockfd, (struct sockaddr *) &addr_income, &addrlen);
    if (incomefd < 0) {
        switch (errno) {
        /* The system call was interrupted by a signal that was caught before a valid connection arrived, or this connection has been aborted.
            in these case , this round of operation ignore, try next round accept notified by epoll */
            case EINTR:
            case ECONNABORTED:
                break;

             /* no more data canbe read, waitting for next epoll edge trigger */
            case EAGAIN:
                break;

        /* The per-process/system-wide limit on the number of open file descriptors has been reached, or
            Not enough free memory, or Firewall rules forbid connection.
            in these cases, this round of operation can fail, but the service link must be retain */
            case ENFILE:
            case ENOBUFS:
            case ENOMEM:
            case EPERM:
                mxx_call_ecr("Non-fatal error occurred syscall accept(2), code:%d, link:%lld", errno, ncb_server->hld);
                break;

        /* ERRORs: (in the any of the following cases, the listening service link will be automatic destroy)
            EBADFD      The sockfd is not an open file descriptor
            EFAULT      The addr argument is not in a writable part of the user address space
            EINVAL      Socket is not listening for connections, or addrlen is invalid (e.g., is negative), or invalid value in falgs
            ENOTSOCK    The file descriptor sockfd does not refer to a socket
            EOPNOTSUPP  The referenced socket is not of type SOCK_STREAM.
            EPROTO      Protocol error. */
            default:
                mxx_call_ecr("Fatal error occurred syscall accept(2), error:%d, link:%lld", errno, ncb_server->hld);
                break;
        }
        return posix__makeerror(errno);
    }

    if (likely(clientfd)) {
        *clientfd = incomefd;
    }
    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t __tcp_syn_dpc(ncb_t *ncb_server, ncb_t *ncb)
{
    nsp_status_t status;

    /* set options */
    if (AF_UNIX == ncb_server->local_addr.sin_family ) {
        ncb->local_addr.sin_family = AF_UNIX;
        ncb->local_addr.sin_port = 0;
        strncpy(ncb->domain_addr.sun_path, ncb_server->domain_addr.sun_path, sizeof(ncb_server->domain_addr.sun_path) - 1);
    } else {
        /* save local and remote address structure */
        tcp_relate_address(ncb);
        /* the low-level [TCP Keep-ALive] are usable. */
        tcp_set_keepalive(ncb);
        /* acquire save TCP Info and adjust linger in the accept phase.
            l_onoff on and l_linger not zero, these settings means:
            TCP drop any data cached in the kernel buffer of this socket file descriptor when close(2) called.
            post a TCP-RST to peer, do not use FIN-FINACK, using this flag to avoid TIME_WAIT stauts */
        ncb_set_linger(ncb);
    }

    /* disable delay optimization */
    tcp_set_nodelay(ncb, 1);

    /* adjust TCP window size to mimimum require. */
    ncb_set_buffsize(ncb);

    /* this link use to receive data from remote peer,
            so the packet and rx memory acquire to allocate now */
    status = tcp_allocate_rx_buffer(ncb);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    /* specify data handler proc for client ncb object */
    atom_set(&ncb->ncb_read, &tcp_rx);
    atom_set(&ncb->ncb_write, &tcp_tx);

    /* copy the context from listen fd to accepted one in needed */
    if (ncb_server->attr & LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT) {
        ncb->attr = ncb_server->attr;
        memcpy(&ncb->u.tcp.template, &ncb_server->u.tcp.template, sizeof(tst_t));
    }

    /* attach to epoll as early as it can to ensure the EPOLLRDHUP and EPOLLERR event not be lost,
        BUT do NOT allow the EPOLLIN event, because receive message should NOT early than accepted message */
    status = io_attach(ncb, 0);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    /* tell calling thread, link has been accepted.
        user can rewrite some context in callback even if LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT is set */
    ncb_post_accepted(ncb_server, ncb->hld);

    /* allow the EPOLLIN event to visit this file-descriptor */
    io_modify(ncb, EPOLLIN);
    return status;
}

static nsp_status_t __tcp_syn(ncb_t *ncb_server)
{
    ncb_t *ncb;
    objhld_t hld;
    struct tcp_info ktcp;
    int clientfd;
    struct objcreator creator;
    nsp_status_t status;

    clientfd = -1;

    /* get the socket status of tcp_info to check the socket tcp statues,
        it must be listen states when accept syscall */
    status = tcp_save_info(ncb_server, &ktcp);
    if (NSP_SUCCESS(status)) {
        if (ktcp.tcpi_state != TCP_LISTEN) {
            mxx_call_ecr("Link:%lld, kernel states error:%s.", ncb_server->hld, tcp_state2name(ktcp.tcpi_state));
            return NSP_STATUS_SUCCESSFUL;
        }
    }

    /* try syscall connect(2) once, if accept socket fatal, the ncb object willbe destroy */
    status = __tcp_syn_try(ncb_server, &clientfd);
    if ( NSP_SUCCESS(status)) {
        creator.known = INVALID_OBJHLD;
        creator.size = sizeof(ncb_t);
        creator.initializer = &ncb_allocator;
        creator.unloader = &ncb_deconstruct;
        creator.context = NULL;
        creator.ctxsize = 0;
        hld = objallo3(&creator);
        if (hld < 0) {
            close(clientfd);
            return NSP_STATUS_SUCCESSFUL;
        }
        ncb = objrefr(hld);
        assert(ncb);
        ncb->sockfd = clientfd;
        ncb->hld = hld;
        ncb->protocol = IPPROTO_TCP;
        ncb->nis_callback = ncb_server->nis_callback;

        mxx_call_ecr("Accepted link:%lld, socket:%d ", hld, clientfd);

        /* initial the client ncb object, link willbe destroy on fatal. */
        status = __tcp_syn_dpc(ncb_server, ncb);
        if ( !NSP_SUCCESS(status) ) {
            objclos(hld);
        }
        objdefr(hld);
    }

    return status;
}

nsp_status_t tcp_syn(ncb_t *ncb_server)
{
    nsp_status_t status;

    do {
        status = __tcp_syn(ncb_server);
    } while (NSP_SUCCESS(status));
    return status;
}

static nsp_status_t __tcp_rx(ncb_t *ncb)
{
    int recvcb;
    int overplus;
    int offset;
    int cpcb;
    int nread;

    /* FIONREAD query the length of data can read in device buffer. */
    if ( 0 == ioctl(ncb->sockfd, FIONREAD, &nread)) {
        if (0 == nread) {
            return posix__makeerror(EAGAIN);
        }
    }

    recvcb = recv(ncb->sockfd, ncb->u.tcp.rx_buffer, TCP_BUFFER_SIZE, 0);
    if (recvcb > 0) {
        cpcb = recvcb;
        overplus = recvcb;
        offset = 0;
        do {
            overplus = tcp_parse_pkt(ncb, ncb->u.tcp.rx_buffer + offset, cpcb);
            if (overplus < 0) {
                /* fatal to parse low level protocol,
                    close the object immediately */
                return NSP_STATUS_FATAL;
            }
            offset += (cpcb - overplus);
            cpcb = overplus;
        } while (overplus > 0);
    }

    /* a stream socket peer has performed an orderly shutdown */
    if (0 == recvcb) {
        mxx_call_ecr("Fatal error occurred syscall recv(2), the return value equal to zero, link:%lld", ncb->hld );
        return posix__makeerror(ECONNRESET);
    }

    /* ECONNRESET 104 Connection reset by peer */
    if (recvcb < 0) {
        /* A signal occurred before any data  was  transmitted, try again by next loop */
        if (errno == EINTR) {
            return NSP_STATUS_SUCCESSFUL;
        }
        mxx_call_ecr("Fatal error occurred syscall recv(2), error:%d, link:%lld", errno, ncb->hld );
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t tcp_rx(ncb_t *ncb)
{
    nsp_status_t status;

    /* read receive buffer until it's empty */
    do {
        status = __tcp_rx(ncb);
    }while( NSP_SUCCESS(status) );
    return status;
}

nsp_status_t tcp_txn(ncb_t *ncb, void *p)
{
    int wcb;
    struct tx_node *node;

    node = (struct tx_node *)p;

    while (node->offset < node->wcb) {
        wcb = send(ncb->sockfd, node->data + node->offset, node->wcb - node->offset, MSG_NOSIGNAL);

        /* fatal-error/connection-terminated  */
        if (0 == wcb) {
            mxx_call_ecr("Fatal error occurred syscall send(2), the return value equal to zero, link:%lld", ncb->hld );
            return NSP_STATUS_FATAL;
        }

        if (wcb < 0) {

            /* A signal occurred before any data  was  transmitted
                continue and send again */
            if (EINTR == errno) {
                continue;
            }

            /* EAGAIN didn't acquire a log record */
            if (EAGAIN != errno) {
                mxx_call_ecr("Fatal error occurred syscall send(2), error:%d, link:%lld",errno, ncb->hld );
            }

            /* other error, these errors should cause link close */
            return posix__makeerror(errno);
        }

        node->offset += wcb;
    }

    return NSP_STATUS_SUCCESSFUL;
}

/* TCP sender proc */
nsp_status_t tcp_tx(ncb_t *ncb)
{
    struct tx_node *node;
    struct tcp_info ktcp;
    nsp_status_t status;

    /* get the socket status of tcp_info to check the socket tcp statues */
    status = tcp_save_info(ncb, &ktcp);
    if (NSP_SUCCESS(status)) {
        if (ktcp.tcpi_state != TCP_ESTABLISHED) {
            mxx_call_ecr("state illegal,link:%lld, kernel states:%s.", ncb->hld, tcp_state2name(ktcp.tcpi_state));
            return posix__makeerror(EINVAL);
        }
    }

    /* try to write front package into system kernel send-buffer */
    status = fifo_top(ncb, &node);
    if (NSP_SUCCESS(status)) {
        return tcp_txn(ncb, node);
    }

    return status;
}

#if 0
static int __tcp_poll_syn(int sockfd, int *err)
{
    struct pollfd pofd;
    socklen_t errlen;
    int error;

    pofd.fd = sockfd;
    pofd.events = POLLOUT;
    errlen = sizeof(error);

    if (!err) {
        return -EINVAL;
    }

    do {
        if (poll(&pofd, 1, -1) < 0) {
            break;
        }

        if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &errlen) < 0) {
            break;
        }

        *err = error;
        return ((0 == error) ? (0) : (-1));

    } while (0);

    *err = errno;
    return -1;
}
#endif

/*
 * tcp connect request asynchronous completed handler
 */
nsp_status_t tcp_tx_syn(ncb_t *ncb)
{
    int e;
    nsp_status_t status;

    while (1) {
        if( 0 == ncb_query_link_error(ncb, &e)) {

            /* this link use to receive data from remote peer,
                so the packet and rx memory acquire to allocate now */
            status = tcp_allocate_rx_buffer(ncb);
            if (!NSP_SUCCESS(status) ) {
                objclos(ncb->hld);
                return status;
            }

            if (ncb->local_addr.sin_family != AF_UNIX) {
                /* the low-level [TCP Keep-ALive] are usable. */
                tcp_set_keepalive(ncb);
                /* get peer address information */
                tcp_relate_address(ncb);
            }

            /* follow tcp rx/tx event */
            atom_set(&ncb->ncb_read, &tcp_rx);
            atom_set(&ncb->ncb_write, &tcp_tx);

            /* focus EPOLLIN only */
            status = io_modify(ncb, EPOLLIN);
            if (!NSP_SUCCESS(status) ) {
                objclos(ncb->hld);
                return status;
            }

            if (ncb->local_addr.sin_family != AF_UNIX) {
                mxx_call_ecr("connection established associated binding on %s:%d, link:%lld .",
                    inet_ntoa(ncb->local_addr.sin_addr), ntohs(ncb->local_addr.sin_port), ncb->hld);
            } else {
                mxx_call_ecr("connection established associated binding on domain %s, link:%lld .",
                    ncb->domain_addr.sun_path, ncb->hld);
            }
            ncb_post_connected(ncb);
            return NSP_STATUS_SUCCESSFUL;
        }

        switch (e) {
            /* connection has been establish or already existed */
            case EISCONN:
            case EALREADY:
                return NSP_STATUS_SUCCESSFUL;

            /* other interrupted or full cached,try again
                Only a few linux version likely to happen. */
            case EINTR:
                break;

            case EAGAIN:
                return -EAGAIN;

            /* Connection refused
             * ulimit -n overflow(open file cout lg then 1024 in default) */
            case ECONNREFUSED:
            default:
                mxx_call_ecr("fatal error occurred syscall poll(2), error:%d, link %lld.", e, ncb->hld);
                return posix__makeerror(e);
        }
    }

    return NSP_STATUS_SUCCESSFUL;
}

/*
 * tcp connect request asynchronous error handler
 */
nsp_status_t tcp_rx_syn(ncb_t *ncb)
{
    int error;

    if ( unlikely(!ncb)) {
        return posix__makeerror(EINVAL);
    }

    if (ncb_query_link_error(ncb, &error) >= 0) {
        if (0 == error) {
            return NSP_STATUS_SUCCESSFUL;
        }
    }

    mxx_call_ecr("link error:%d,link:%lld", error, ncb->hld);
    return posix__makeerror(error);
}
