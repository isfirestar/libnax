#include "tcp.h"
#include "mxx.h"
#include "zmalloc.h"

static int __tcp_parse_marked_lb(ncb_t *ncb, const unsigned char *cpbuff, int cpcb)
{
    int overplus;

    /* The arrival data are not enough to fill the large-block. */
    if (cpcb + ncb->u.tcp.lboffset < ncb->u.tcp.lbsize) {
        memcpy(ncb->u.tcp.lbdata + ncb->u.tcp.lboffset, cpbuff, cpcb);
        ncb->u.tcp.lboffset += cpcb;
        return 0;
    }

    /* The arrival data not enough to fill the large-block. */
    overplus = ncb->u.tcp.lbsize - ncb->u.tcp.lboffset;
    memcpy(ncb->u.tcp.lbdata + ncb->u.tcp.lboffset, cpbuff, overplus);

    if (ncb->attr & LINKATTR_TCP_FULLY_RECEIVE) {
        ncb_post_recvdata(ncb, ncb->u.tcp.lbsize, ncb->u.tcp.lbdata);
    } else {
        ncb_post_recvdata(ncb, ncb->u.tcp.lbsize - ncb->u.tcp.template.cb_, ncb->u.tcp.lbdata + ncb->u.tcp.template.cb_);
    }

    /* free the large-block buffer */
    zfree(ncb->u.tcp.lbdata);
    ncb->u.tcp.lbdata = NULL;
    ncb->u.tcp.lboffset = 0;
    ncb->u.tcp.lbsize = 0;
    return (cpcb - overplus);
}

int tcp_parse_pkt(ncb_t *ncb, const unsigned char *data, int cpcb)
{
    int used;
    int overplus;
    const unsigned char *cpbuff;
    int total_packet_length;
    int user_data_size;
    int retcb;
    nsp_status_t status;

    if ( unlikely(!ncb || !data || 0 == cpcb)) {
        return -1;
    }

    cpbuff = data;

    /* no template specified, direct give the whole packet */
    if (0 == ncb->u.tcp.template.cb_ && !(*ncb->u.tcp.template.parser_)) {
        ncb_post_recvdata(ncb, cpcb, data);
        ncb->u.tcp.rx_parse_offset = 0;
        return 0;
    }

    /* it is in the large-block status */
    if (ncb_lb_marked(ncb)) {
        return __tcp_parse_marked_lb(ncb, cpbuff, cpcb);
    }

    /* the length of data is not enough to constitute the protocol header.
    *  All data is used to construct the protocol header and return the remaining length of 0. */
    if (ncb->u.tcp.rx_parse_offset + cpcb < ncb->u.tcp.template.cb_) {
        memcpy(ncb->packet + ncb->u.tcp.rx_parse_offset, cpbuff, cpcb);
        ncb->u.tcp.rx_parse_offset += cpcb;
        return 0;
    }

    overplus = cpcb;
    used = 0;

    /* The data in the current package is not enough to construct the protocol header,
        but with these data, it is enough to construct the protocol header. */
    if (ncb->u.tcp.rx_parse_offset < ncb->u.tcp.template.cb_) {
        used += (ncb->u.tcp.template.cb_ - ncb->u.tcp.rx_parse_offset);
        overplus = cpcb - used;
        memcpy(ncb->packet + ncb->u.tcp.rx_parse_offset, cpbuff, used);
        cpbuff += used;
        ncb->u.tcp.rx_parse_offset = ncb->u.tcp.template.cb_;
    }

    /* The low-level protocol interacts with the protocol template, and the unpacking operation cannot continue if the processing fails.  */
    if (!(*ncb->u.tcp.template.parser_)) {
        mxx_call_ecr("parser tempalte method illegal.");
        return -1;
    }

	/* Get the length of user segment data by interpreting routines  */
    status = (*ncb->u.tcp.template.parser_)(ncb->packet, ncb->u.tcp.rx_parse_offset, &user_data_size);
    if (!NSP_SUCCESS(status)) {
        mxx_call_ecr("failed to parse template header.");
		return -1;
	}

	/* If the user data length exceeds the maximum tolerance length,
     * it will be reported as an error directly, possibly a malicious attack.  */
    if ((user_data_size > TCP_MAXIMUM_PACKET_SIZE) || (user_data_size <= 0)) {
        mxx_call_ecr("bad data size:%d.", user_data_size);
		return -1;
	}

    /* total package length, include the packet head */
    total_packet_length = user_data_size + ncb->u.tcp.template.cb_;

    /* If it is a large-block, then we should establish a large-block process.  */
    if (total_packet_length > TCP_BUFFER_SIZE) {
        if (NULL == (ncb->u.tcp.lbdata = (unsigned char *)ztrymalloc(total_packet_length))) {
            return -1;
        }

        /* total large-block length, include the low-level protocol head length */
        ncb->u.tcp.lbsize = total_packet_length;

        /* copy all data to buffer */
        memcpy(ncb->u.tcp.lbdata, data, cpcb);
        ncb->u.tcp.lboffset = cpcb;

        /* clear the describe information of buffer */
        ncb->u.tcp.rx_parse_offset = 0;

        /* while building large-block,  the data from a single receive buffer is bound to be exhausted at one time. */
        return 0;
    }

    /* the remain data it's enough to build package */
    if ((ncb->u.tcp.rx_parse_offset + overplus) >= total_packet_length) {
        memcpy(ncb->packet + ncb->u.tcp.rx_parse_offset, cpbuff, total_packet_length - ncb->u.tcp.rx_parse_offset);

        /*The number of bytes returned to the calling thread =
            (The number of bytes remaining this time) -
            (The total number of bytes consumed to build this package) */
        retcb = (overplus - (total_packet_length - ncb->u.tcp.rx_parse_offset));

        if (ncb->attr & LINKATTR_TCP_FULLY_RECEIVE) {
            ncb_post_recvdata(ncb, user_data_size + ncb->u.tcp.template.cb_, ncb->packet);
        } else {
            ncb_post_recvdata(ncb, user_data_size, ncb->packet + ncb->u.tcp.template.cb_);
        }


        ncb->u.tcp.rx_parse_offset = 0;
        return retcb;
    }

    /*If the number of bytes remaining is not enough to construct a complete package,
        the remain bytes are going to put into buffer and the packet resolution offset is adjusted. */
    memmove(ncb->packet + ncb->u.tcp.rx_parse_offset, cpbuff, overplus);
    ncb->u.tcp.rx_parse_offset += overplus;
    return 0;
}
