#include "udp.h"

#include "mxx.h"
#include "fifo.h"
#include "io.h"
#include "wpool.h"
#include "pipe.h"

#include "atom.h"
#include "zmalloc.h"

static nsp_status_t _udprefr( objhld_t hld, ncb_t **ncb )
{
    if (unlikely( hld < 0 || !ncb)) {
        return posix__makeerror(EINVAL);
    }

    *ncb = objrefr( hld );
    if ( NULL != (*ncb) ) {
        if (likely(IPPROTO_UDP == (*ncb)->protocol)) {
            return NSP_STATUS_SUCCESSFUL;
        }

        objdefr( hld );
        *ncb = NULL;
        return posix__makeerror(EPROTOTYPE);
    }

    return posix__makeerror(ENOENT);
}

#define _udp_invoke(foo)   foo(IPPROTO_UDP)

nsp_status_t udp_init2(int nprocs)
{
    nsp_status_t status;

    status = io_init(IPPROTO_UDP, nprocs);
    if ( !NSP_SUCCESS(status) ) {
        return status;
    }

    status = _udp_invoke(wp_init);
    if ( !NSP_SUCCESS(status) ) {
        _udp_invoke(io_uninit);
    }

    return status;
}

nsp_status_t udp_init()
{
    return udp_init2(0);
}

void udp_uninit()
{
    _udp_invoke(ncb_uninit);
    _udp_invoke(io_uninit);
    _udp_invoke(wp_uninit);
}

static nsp_status_t _udp_create_domain(ncb_t *ncb, const char* domain)
{
    int fd;
    nsp_status_t status;
    int expect;

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (unlikely(fd < 0)) {
       mxx_call_ecr("fatal error occurred syscall socket(2) for domain, error:%d", errno);
       return posix__makeerror(errno);
    }

    /* prevent double creation */
    expect = 0;
    if (!atom_compare_exchange_strong(&ncb->sockfd, &expect, fd)) {
        close(fd);
        return posix__makeerror(EEXIST);
    }

    do {
        /* local address information redirect to domain pattern */
        ncb->local_addr.sin_addr.s_addr = 0;
        ncb->local_addr.sin_family = AF_UNIX;
        ncb->local_addr.sin_port = 1; /* this is a server domain */

        /* UDP domain socket allow user keep only Tx ability
           in this case, socket didn't necessary to bind with any IPC file but couldn't receive data
           below condition indicate local socket acquire to binding on a specify domain file */
        if (0 != domain[0]) {
            memset(&ncb->domain_addr, 0, sizeof(ncb->domain_addr));
            ncb->domain_addr.sun_family = AF_UNIX;
            strncpy(ncb->domain_addr.sun_path, domain, sizeof(ncb->domain_addr.sun_path) - 1);

            /* try to remove file before bind, to avoid EADDRINUSE error */
            unlink(ncb->domain_addr.sun_path);

            if (-1 == bind(fd, (struct sockaddr *) &ncb->domain_addr, sizeof(ncb->domain_addr))) {
                mxx_call_ecr("fatal error occurred syscall bind(2) for domain %s, error:%d,", domain, errno);
                status = posix__makeerror(errno);
                break;
            }

            /* allocate buffer for normal packet */
            ncb->packet = (unsigned char *)ztrymalloc(MAX_UDP_UNIT);
            if (unlikely(!ncb->packet)) {
                status = posix__makeerror(ENOMEM);
                break;
            }

            atom_set(&ncb->ncb_read, &udp_rx);
        }

        /* set data handler function pointer for Rx/Tx */
        atom_set(&ncb->ncb_write, &udp_tx);

        /* attach to epoll */
        status = io_attach(ncb, EPOLLIN);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        mxx_call_ecr("success allocate link:%lld, sockfd:%d, binding on domain %s", ncb->hld, ncb->sockfd, domain);
        return ncb->hld;
    } while (0);

    return status;
}

static nsp_status_t _udp_create(ncb_t *ncb, const char* ipstr, uint16_t port, int flag)
{
    int fd;
    int expect;
    struct sockaddr_in addrlocal;
    socklen_t addrlen;
    nsp_status_t status;

    fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (unlikely(-1 == fd)) {
        mxx_call_ecr("fatal error occurred syscall socket(2), error:%d", errno);
        return posix__makeerror(errno);
    }

    expect = 0;
    if (!atom_compare_exchange_strong(&ncb->sockfd, &expect, fd)) {
        close(fd);
        return posix__makeerror(EEXIST);
    }

    do {
        /* setsockopt */
        ncb_set_buffsize(ncb);
        /* allow port reuse(the same port number binding on different IP address) */
        ncb_set_reuseaddr(ncb);

        addrlocal.sin_addr.s_addr = ipstr ? inet_addr(ipstr) : INADDR_ANY;
        addrlocal.sin_family = AF_INET;
        addrlocal.sin_port = htons(port);
        if (-1 == bind(fd, (struct sockaddr *) &addrlocal, sizeof ( struct sockaddr))) {
            mxx_call_ecr("fatal error occurred syscall bind(2),local endpoint %s:%u, error:%d,", (ipstr ? ipstr : "0.0.0.0"), port, errno);
            status = posix__makeerror(errno);
            break;
        }

        /* allocate buffer for normal packet */
        ncb->packet = (unsigned char *)ztrymalloc(MAX_UDP_UNIT);
        if (unlikely(!ncb->packet)) {
            status = posix__makeerror(ENOMEM);
            break;
        }

        /* extension of broadcast/multicast */
        if (flag & UDP_FLAG_BROADCAST) {
            status = udp_set_boardcast(ncb, 1);
            if ( !NSP_SUCCESS(status)) {
                break;
            }
            ncb->attr |= UDP_FLAG_BROADCAST;
        } else {
            if (flag & UDP_FLAG_MULTICAST) {
                ncb->attr |= UDP_FLAG_MULTICAST;
            }
        }

        /* get local address info */
        addrlen = sizeof(ncb->local_addr);
        getsockname(ncb->sockfd, (struct sockaddr *) &ncb->local_addr, &addrlen);

        /* set data handler function pointer for Rx/Tx */
        atom_set(&ncb->ncb_read, &udp_rx);
        atom_set(&ncb->ncb_write, &udp_tx);

        /* attach to epoll */
        status = io_attach(ncb, EPOLLIN);
        if (!NSP_SUCCESS(status)) {
            break;
        }

        mxx_call_ecr("success allocate link:%lld, sockfd:%d, binding on %s:%d",
            ncb->hld, ncb->sockfd, inet_ntoa(ncb->local_addr.sin_addr), ntohs(ncb->local_addr.sin_port));
        return ncb->hld;
    } while (0);

    return status;
}

HUDPLINK udp_create(udp_io_fp callback, const char* ipstr, uint16_t port, int flag)
{
    ncb_t *ncb;
    struct objcreator creator;
    nsp_status_t status;
    objhld_t hld;

    creator.known = INVALID_OBJHLD;
    creator.size = sizeof(ncb_t);
    creator.initializer = &ncb_allocator;
    creator.unloader = &ncb_deconstruct;
    creator.context = NULL;
    creator.ctxsize = 0;
    hld = objallo3(&creator);
    if (unlikely(hld < 0)) {
        mxx_call_ecr("insufficient resource for allocate inner object");
        return INVALID_HUDPLINK;
    }

    ncb = (ncb_t *) objrefr(hld);
    assert(ncb);

    ncb->hld = hld;
    ncb->protocol = IPPROTO_UDP;
    ncb->nis_callback = callback;

    do {
        if (ipstr) {
            if (0 == strncasecmp(ipstr, "IPC:", 4)) {
                status = _udp_create_domain(ncb, &ipstr[4]);
                break;
            }
        }

        status = _udp_create(ncb, ipstr, port, flag);
    } while(0);

    objdefr(hld);
    if (!NSP_SUCCESS(status)) {
        objclos(hld);
        return INVALID_HUDPLINK;
    }

    return ncb->hld;
}

void udp_destroy(HUDPLINK link)
{
    ncb_t *ncb;

    if (!NSP_SUCCESS(_udprefr(link, &ncb))) {
        return;
    }

    mxx_call_ecr("link:%lld order to destroy", ncb->hld);
    io_shutdown(ncb, SHUT_RDWR);
    objdefr(link);
}

nsp_status_t udp_awaken(HUDPLINK link, const void *pipedata, unsigned int cb)
{
    nsp_status_t status;
    ncb_t *ncb;

    if (unlikely(link < 0 || !pipedata || 0 == cb)) {
        return posix__makeerror(EINVAL);
    }

    status = _udprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    status = pipe_write_message(ncb, pipedata, cb);
    objdefr(link);
    return status;
}

nsp_status_t udp_write(HUDPLINK link, const void *origin, unsigned int cb, const char* ipstr, uint16_t port, const nis_serializer_fp serializer)
{
    ncb_t *ncb;
    unsigned char *buffer;
    struct tx_node *node;
    nsp_status_t status;

    if (unlikely( !ipstr || (cb <= 0) || (link < 0) || (cb > MAX_UDP_UNIT) || !origin)) {
        return posix__makeerror(EINVAL);
    }

    if (unlikely(0 == ipstr[0])) {
        return posix__makeerror(EINVAL);
    }

    buffer = NULL;
    node = NULL;

    status = _udprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    do {
        status = NSP_STATUS_FATAL;

        /* domain socket can ignore @port but TCP/IP not */
        if (0 == port && ncb->local_addr.sin_family != AF_UNIX ) {
            status = posix__makeerror(EINVAL);
            break;
        }

        buffer = (unsigned char *)ztrymalloc(cb);
        if (unlikely(!buffer)) {
            status = posix__makeerror(ENOMEM);
            break;
        }

        /* serialize data into packet or direct use data pointer by @origin */
        if (serializer) {
            if ((*serializer)(buffer, origin, cb) < 0 ) {
                break;
            }
        } else {
            memcpy(buffer, origin, cb);
        }

        node = (struct tx_node *)ztrymalloc(sizeof (struct tx_node));
        if (unlikely(!node)) {
            status = posix__makeerror(ENOMEM);
            break;
        }
        memset(node, 0, sizeof(struct tx_node));
        node->data = buffer;
        node->wcb = cb;
        node->offset = 0;

        if (AF_UNIX == ncb->local_addr.sin_family ) {
            memset(&node->domain_target, 0, sizeof(node->domain_target));
            node->domain_target.sun_family = AF_UNIX;
            if (0 == strncasecmp(ipstr, "IPC:", 4)) {
                strncpy(node->domain_target.sun_path, &ipstr[4], sizeof(node->domain_target.sun_path) - 1);
            } else {
                strncpy(node->domain_target.sun_path, ipstr, sizeof(node->domain_target.sun_path) - 1);
            }
        } else {
            node->udp_target.sin_family = AF_INET;
            node->udp_target.sin_addr.s_addr = inet_addr(ipstr);
            node->udp_target.sin_port = htons(port);
        }

        if (!fifo_tx_overflow(ncb)) {

            /* the return value means direct failed when it equal to -1 or success when it greater than zero.
             * in these case, destroy memory resource outside loop, no matter what the actually result it is.
             */
            status = udp_txn(ncb, node);

            /* break if success or failed without EAGAIN
             */
            if (!NSP_FAILED_AND_ERROR_EQUAL(status, EAGAIN)) {
                break;
            }
        }

        /* 1. when the IO state is blocking, any send or write call certain to be fail immediately,
         *
         * 2. the meaning of -EAGAIN return by @udp_txn is send or write operation cannot be complete immediately,
         *      IO state should change to blocking now
         *
         * one way to handle the above two aspects, queue data to the tail of fifo manager, preserve the sequence of output order
         * in this case, memory of @buffer and @node cannot be destroy until asynchronous completed
         *
         * after @fifo_queue success called, IO blocking flag is set, and EPOLLOUT event has been associated with ncb object.
         * wpool thread canbe awaken by any kernel cache writable event trigger  */
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

nsp_status_t udp_getaddr(HUDPLINK link, uint32_t *ipv4, uint16_t *port)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _udprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    status = posix__makeerror(EPROTOTYPE);
    if (AF_UNIX != ncb->local_addr.sin_family) {
        if (ipv4) {
            *ipv4 = htonl(ncb->local_addr.sin_addr.s_addr);
        }
        if (port) {
            *port = htons(ncb->local_addr.sin_port);
        }
        status = NSP_STATUS_SUCCESSFUL;
    }

    objdefr(link);
    return status;
}

nsp_status_t udp_getipcpath(HUDPLINK link, const char **path)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _udprefr(link, &ncb);
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

nsp_status_t udp_setopt(HUDPLINK link, int level, int opt, const char *val, unsigned int len)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _udprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    if (-1 == setsockopt(ncb->sockfd, level, opt, val, len)) {
        status = posix__makeerror(errno);
    }

    objdefr(link);
    return status;
}

nsp_status_t udp_getopt(HUDPLINK link, int level, int opt, char *val, unsigned int *len)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _udprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    if (-1 == getsockopt(ncb->sockfd, level, opt, val, (socklen_t *)len)) {
        status = posix__makeerror(errno);
    }

    objdefr(link);
    return status;
}

nsp_status_t udp_set_boardcast(ncb_t *ncb, int enable)
{
    if (0 == setsockopt(ncb->sockfd, SOL_SOCKET, SO_BROADCAST, (const void *) &enable, sizeof (enable))) {
        return NSP_STATUS_SUCCESSFUL;
    }

    return posix__makeerror(errno);
}

nsp_status_t udp_get_boardcast(ncb_t *ncb, int *enabled)
{
    socklen_t optlen;

    optlen = sizeof (int);
    if (0 == getsockopt(ncb->sockfd, SOL_SOCKET, SO_BROADCAST, (void * __restrict)enabled, &optlen)) {
        return NSP_STATUS_SUCCESSFUL;
    }
    return posix__makeerror(errno);
}

/*
* The destination address of multicast message uses Class D IP address. Class D address cannot appear in the source IP address field of IP message.
* In the process of unicast data transmission, a data packet transmission path is routed from the source address to the destination address,
* which is transmitted in the IP network using the "hop-by-hop" principle.
*
* However, in the IP multicast ring, the destination address of the packet is not one, but a group, forming a group address.
* All information receivers join a group, and once they join, the data flowing to the group address begins to be transmitted to the receivers immediately,
* and all members of the group can receive the data packet.
*
* The members of multicast group are dynamic, and the host can join and leave the multicast group at any time.
*
* All hosts receiving multicast packets with the same IP multicast address constitute a host group, also known as multicast group.
* The membership of a multicast group changes at any time. A host can join or leave the multiple group at any time.
* The number and location of the members of the multicast group are unrestricted. A host can also belong to several multiple groups.
*
* In addition, hosts that do not belong to a multicast group can also send data packets to the multicast group.
*
* multicast addressing:
* Multicast groups can be permanent or temporary. In multicast group addresses, some of them are officially assigned, which is called permanent multicast group.
*
* Permanent multicast group keeps its IP address unchanged, and its membership can change.
* The number of members in a permanent multicast group can be arbitrary or even zero.
* IP multicast addresses that are not reserved for permanent multicast groups can be used by temporary multicast groups.
*       224.0.0.0-224.0.0.255 is the reserved multicast address (permanent group address). The address 224.0.0.0 is reserved without allocation.
*                                Other addresses are used by routing protocols.
*       224.0.1.0-224.0.1.255 is a public multicast address that can be used on the Internet.
*       224.0.2.0-238.255.255.255 is user-available multicast address (temporary group address), which is valid throughout the network.
*       239.0.0.0-239.255.255.255 is a locally managed multicast address, which is valid only within a specific local range.
*
* Multicast is a one-to-many transmission mode, in which there is a concept of multicast group.
* The sender sends data to a group. The router in the network automatically sends data to all terminals listening to the group through the underlying IGMP protocol.
*
* As for broadcasting, there are some similarities with multicast.
* The difference is that the router sends a packet to every terminal in the subnet, whether or not these terminals are willing to receive the packet.
* UDP broadcasting can only be effective in the intranet (the same network segment), while multicast can better achieve cross-network segment mass data.
*
* UDP multicast is a connectionless and datagram connection mode, so it is unreliable.
* That is to say, whether the data can reach the receiving end and the order of data arrival are not guaranteed.
* But because UDP does not guarantee the reliability of data, all data transmission efficiency is very fast.
*/
nsp_status_t udp_joingrp(HUDPLINK link, const char *ipstr, uint16_t port)
{
    ncb_t *ncb;
    nsp_status_t status;

    if (unlikely(link < 0 || !ipstr || 0 == port)) {
        return -EINVAL;
    }

    status = _udprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    do {
        status = NSP_STATUS_FATAL;

        if (!(ncb->attr & UDP_FLAG_MULTICAST)) {
            break;
        }

        /* set permit for loopback */
        int loop = 1;
        if (-1 == setsockopt(ncb->sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, (const void *)&loop, sizeof (loop))) {
            status = posix__makeerror(errno);
            break;
        }

        /* insert into multicast group */
        if (!ncb->u.udp.mreq){
            ncb->u.udp.mreq = (struct ip_mreq *)ztrymalloc(sizeof(struct ip_mreq));
            if (unlikely(!ncb->u.udp.mreq)) {
                status = posix__makeerror(ENOMEM);
                break;
            }
        }
        ncb->u.udp.mreq->imr_multiaddr.s_addr = inet_addr(ipstr);
        ncb->u.udp.mreq->imr_interface.s_addr = ncb->local_addr.sin_addr.s_addr;
        if ( -1 == setsockopt(ncb->sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)ncb->u.udp.mreq, sizeof(struct ip_mreq)) ) {
            status = posix__makeerror(errno);
            break;
        }

        status = NSP_STATUS_SUCCESSFUL;
    } while (0);

    objdefr(link);
    return status;
}

nsp_status_t udp_dropgrp(HUDPLINK link)
{
    ncb_t *ncb;
    nsp_status_t status;

    status = _udprefr(link, &ncb);
    if (!NSP_SUCCESS(status)) {
        return status;
    }

    do{
        status = NSP_STATUS_FATAL;

        if (!(ncb->attr & UDP_FLAG_MULTICAST) || !ncb->u.udp.mreq) {
            break;
        }

        /* reduction permit for loopback */
        int loop = 0;
        if (-1 == setsockopt(ncb->sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, (const void *)&loop, sizeof (loop))) {
            status = posix__makeerror(errno);
            break;
        }

        /* leave multicast group */
        if (-1 == setsockopt(ncb->sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const void *)ncb->u.udp.mreq, sizeof(struct ip_mreq))) {
            status = posix__makeerror(errno);
            break;
        }

        status = NSP_STATUS_SUCCESSFUL;
    }while(0);

    objdefr(link);
    return status;
}

nsp_status_t udp_setattr_r(ncb_t *ncb, int attr)
{
    atom_exchange(&ncb->attr, attr);
    if (ncb->attr & LINKATTR_UDP_BAORDCAST) {
        return udp_set_boardcast(ncb, 1);
    } else {
        return udp_set_boardcast(ncb, 0);
    }
}

void udp_getattr_r(ncb_t *ncb, int *attr)
{
    atom_exchange(attr, ncb->attr);
}
