#include "udp.h"

#include "mxx.h"
#include "fifo.h"

static nsp_status_t _udp_rx(ncb_t *ncb)
{
    int recvcb;
    struct sockaddr_in remote;
    udp_data_t c_data;
    nis_event_t c_event;

    recvcb = ncb_recvdata_nonblock(ncb, (struct sockaddr *)&remote, sizeof(remote));
    if (recvcb > 0) {
        c_event.Ln.Udp.Link = ncb->hld;
        c_event.Event = EVT_RECEIVEDATA;
        c_data.e.Packet.Data = ncb->rx_buffer;
        c_data.e.Packet.Size = recvcb;
        inet_ntop(AF_INET, &remote.sin_addr, c_data.e.Packet.RemoteAddress, sizeof (c_data.e.Packet.RemoteAddress));
        c_data.e.Packet.RemotePort = ntohs(remote.sin_port);
        if (ncb->nis_callback) {
            ncb->nis_callback(&c_event, &c_data);
        }
        return NSP_STATUS_SUCCESSFUL;
    }

    if ( 0 == recvcb ) {
        /* Datagram sockets in various domains (e.g., the UNIX and Internet domains) permit zero-length datagrams.
            When such a datagram is received, the return value is 0. */
        return NSP_STATUS_SUCCESSFUL;
    }

    /* ECONNRESET 104 Connection reset by peer */
    if (recvcb < 0) {
        if (!NSP_FAILED_AND_ERROR_EQUAL(recvcb, EAGAIN)) {
            mxx_call_ecr("fatal error occurred syscall recvfrom(2), error:%d, link:%lld", errno, ncb->hld );
        }
        return recvcb;
    }

    return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t _udp_rx_domain(ncb_t *ncb)
{
    int recvcb;
    struct sockaddr_un remote;
    udp_data_t c_data;
    nis_event_t c_event;

    remote.sun_path[0] = 0;
    recvcb = ncb_recvdata_nonblock(ncb, (struct sockaddr *)&remote, sizeof(remote));
    if (recvcb > 0) {
        c_event.Ln.Udp.Link = ncb->hld;
        c_event.Event = EVT_UDP_RECEIVE_DOMAIN;
        c_data.e.Domain.Data = ncb->rx_buffer;
        c_data.e.Domain.Size = recvcb;
        c_data.e.Domain.Path = ((0 == remote.sun_path[0]) ? NULL : &remote.sun_path[0]);
        if (ncb->nis_callback) {
            ncb->nis_callback(&c_event, &c_data);
        }
        return NSP_STATUS_SUCCESSFUL;
    }

    if ( 0 == recvcb ) {
        /* Datagram sockets in various domains (e.g., the UNIX and Internet domains) permit zero-length datagrams.
            When such a datagram is received, the return value is 0. */
        return NSP_STATUS_SUCCESSFUL;
    }

    /* ECONNRESET 104 Connection reset by peer */
    if (recvcb < 0) {
        /* system interrupted */
        if (!NSP_FAILED_AND_ERROR_EQUAL(recvcb, EAGAIN)) {
            mxx_call_ecr("fatal error occurred syscall recvfrom(2), error:%d, link:%lld", errno, ncb->hld );
        }
        return recvcb;
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t udp_rx(ncb_t *ncb)
{
    nsp_status_t status;
    nsp_status_t (*rxfn)(ncb_t *);

    rxfn = (AF_UNIX == ncb->local_addr.sin_family) ? &_udp_rx_domain : &_udp_rx;
    do {
        status = rxfn(ncb);
    } while(NSP_SUCCESS(status));
    return status;
}

nsp_status_t udp_txn(ncb_t *ncb, void *p)
{
    int wcb;
    struct tx_node *node;

	node = (struct tx_node *)p;
	if (!node) {
		return -EINVAL;
	}

    while (node->offset < node->wcb) {
        if (AF_INET == ncb->local_addr.sin_family ) {
            wcb = sendto(ncb->sockfd, node->data + node->offset, node->wcb - node->offset, MSG_DONTWAIT,
                    (const struct sockaddr *)&node->udp_target, sizeof(node->udp_target) );
        } else if (AF_UNIX == ncb->local_addr.sin_family) {
            wcb = sendto(ncb->sockfd, node->data + node->offset, node->wcb - node->offset, MSG_DONTWAIT,
                    (const struct sockaddr *)&node->domain_target, sizeof(node->domain_target) );
        } else {
            return posix__makeerror(EPROTO);
        }

        /* fatal-error/connection-terminated  */
        if (0 == wcb) {
            mxx_call_ecr("Fatal error occurred syscall sendto(2), the return value equal to zero, link:%lld", ncb->hld);
            return NSP_STATUS_FATAL;
        }

        if (wcb < 0) {
            /* the write buffer is full, active EPOLLOUT and waitting for epoll event trigger
             * at this point, we need to deal with the queue header node and restore the unprocessed node back to the queue header.
             * the way 'oneshot' focus on the write operation completion point */
            if (EAGAIN == errno) {
                mxx_call_ecr("syscall sendto(2) would block cause by kernel memory overload,link:%lld", ncb->hld);
            }

            /* A signal occurred before any data  was  transmitted
                continue and send again */
            if (EINTR == errno) {
                continue;
            }

             /* other error, these errors should cause link close */
            mxx_call_ecr("fatal error occurred syscall sendto(2), error:%d, link:%lld",errno, ncb->hld );
            return posix__makeerror(errno);
        }

        node->offset += wcb;
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t udp_tx(ncb_t *ncb)
{
    struct tx_node *node;
    nsp_status_t status;

    /* try to write front package into system kernel send-buffer */
    status = fifo_top(ncb, &node);
    if (NSP_SUCCESS(status)) {
        status = udp_txn(ncb, node);
        if (NSP_SUCCESS(status)) {
            return fifo_pop(ncb, NULL);
        }
    }

    return status;
}
