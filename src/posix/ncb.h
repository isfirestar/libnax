#if !defined NETCONTROLBLOCK
#define NETCONTROLBLOCK

#include "compiler.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <netinet/tcp.h>

#include <arpa/inet.h>

#include <linux/if.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>     /* ETH_P_ARP */

#include "nis.h"
#include "clist.h"
#include "object.h"
#include "threading.h"

struct tx_fifo {
    nsp_boolean_t tx_overflow;
    int size;
    lwp_mutex_t lock;
    struct list_head head;
};

struct _ncb;
typedef nsp_status_t (*ncb_rw_t)(struct _ncb *);

struct _ncb {
    /* the object handle of this ncb */
    objhld_t hld;

    /* the file-descriptor of socket of this ncb */
    int sockfd;

    /* the file-descriptor of epoll object which binding with @sockfd */
    int epfd;

    /* Rx thread-id binding upon epoll */
    pid_t rx_tid;

    /* the IP protocol type of this ncb, only support these two types:IPPROTO_TCP/IPPROTO_UDP */
    int protocol;

    /* the link entry of all ncb object */
    struct list_head nl_entry;

    /* the actually buffer for receive */
    unsigned char *packet;

    /* fifo queue of pending packet for send */
    struct tx_fifo fifo;

    /* local/remote address information */
    struct sockaddr_in remot_addr;
    struct sockaddr_in local_addr;
    struct sockaddr_un domain_addr;

    /* the user-specified nshost event handler */
    nis_callback_t nis_callback;

    /* IO response routine */
    ncb_rw_t ncb_read;
    ncb_rw_t ncb_write;

    /* save the timeout information/options */
    struct timeval rcvtimeo;
    struct timeval sndtimeo;

    /* tos item in IP-head
     * Differentiated Services Field: Dirrerentiated Services Codepoint/Explicit Congestion Not fication */
    int iptos;

    /* the attributes of TCP link */
    int attr;

    /* user definition context pointer */
    void *context;
    void *prcontext;

    union {
        struct {
            /* TCP packet user-parse offset when receving */
            int rx_parse_offset;

            /* the actually buffer give to syscall @recv */
            unsigned char *rx_buffer;

            /* the large-block information(TCP packets larger than 0x11000 Bytes but less than 50MBytes) */
            unsigned char* lbdata;   /* large-block data buffer */
            int lboffset;   /* save offset in @lbdata */
            int lbsize;     /* the total length include protocol-head */

             /* template for make/build package */
            tst_t template;
            tst_t prtemplate;

            /* MSS of tcp link */
            int mss;
        } tcp;

        struct {
            /* mreq object for IP multicast */
            struct ip_mreq *mreq;
        } udp;

        struct {
            objhld_t trigger;
        } pipe;
    } u;
};
typedef struct _ncb ncb_t;

#define ncb_lb_marked(ncb) ((ncb) ? ((NULL != ncb->u.tcp.lbdata) && (ncb->u.tcp.lbsize > 0)) : (0))

extern
void ncb_uninit(int protocol);
extern
int ncb_allocator(void *udata, const void *ctx, int ctxcb);
extern
void ncb_deconstruct(objhld_t ignore, void */*ncb_t * */ncb);

extern
void ncb_set_buffsize(const ncb_t *ncb);

extern
nsp_status_t ncb_set_reuseaddr(const ncb_t *ncb);
extern
nsp_status_t ncb_query_link_error(const ncb_t *ncb, int *err);

extern
nsp_status_t ncb_set_rcvtimeo(const ncb_t *ncb, long long ms);
extern
nsp_status_t ncb_get_rcvtimeo(const ncb_t *ncb);
extern
nsp_status_t ncb_set_sndtimeo(const ncb_t *ncb, long long ms);
extern
nsp_status_t ncb_get_sndtimeo(const ncb_t *ncb);

extern
nsp_status_t ncb_set_iptos(const ncb_t *ncb, int tos);
extern
nsp_status_t ncb_get_iptos(const ncb_t *ncb);

extern
nsp_status_t ncb_set_window_size(const ncb_t *ncb, int dir, int size);
extern
nsp_status_t ncb_get_window_size(const ncb_t *ncb, int dir, int *size);

extern
nsp_status_t ncb_set_linger(const ncb_t *ncb);
extern
nsp_status_t ncb_get_linger(const ncb_t *ncb, int *onoff, int *lin);

extern
void ncb_post_recvdata(const ncb_t *ncb,  int cb, const unsigned char *data);
extern
void ncb_post_pipedata(const ncb_t *ncb,  int cb, const unsigned char *data);
extern
void ncb_post_accepted(const ncb_t *ncb, HTCPLINK link);
extern
void ncb_post_connected(const ncb_t *ncb);

#endif
