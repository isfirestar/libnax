#if !defined SWNET_DEF_HEADER_2016_5_24
#define SWNET_DEF_HEADER_2016_5_24

#include "compiler.h"
#include "object.h"
#include "abuff.h"

/* data-link layer restriction */
#define ETHER_LAYER_SIZE  (14)

/* IP protocol restrict */
#define MTU     (1500)
#define IP_LATER_SIZE   (20)
#define UDP_LAYER_SIZE  (8)
#define TCP_LAYER_SIZE  (20)
/* 1472 */
#define MAX_UDP_UNIT        (MTU - IP_LATER_SIZE - UDP_LAYER_SIZE)
/* 1460 */
#define MAX_TCP_UNIT        (MTU - IP_LATER_SIZE - TCP_LAYER_SIZE)
#define IIS_MTU             (576)
#define IIS_MAX_UDP_UNIT    (IIS_MTU - IP_LATER_SIZE - UDP_LAYER_SIZE)
#define IIS_MAX_TCP_UNIT    (IIS_MTU - IP_LATER_SIZE - TCP_LAYER_SIZE)

/* types of nshost handle */
typedef objhld_t HLNK;
typedef HLNK HTCPLINK;
typedef HLNK HUDPLINK;
typedef HLNK HARPLINK;

/* common string type */
typedef abuff_type(6)   abuff_mac_t;        /* binary buffer to hold a MAC address */
typedef abuff_type(16)  abuff_ddn_ipv4_t;   /* DDN is a abbreviation of "Dotted Decimal Notation" - one way to representation a IPv4 address  */
typedef abuff_type(112) abuff_ipc_path_t;   /* a string with maximum length to hold a legal IPC file path including prefix */

#if !defined INVALID_HTCPLINK
    #define INVALID_HTCPLINK ((HTCPLINK)(~0))
#endif

#if !defined INVALID_HUDPLINK
    #define INVALID_HUDPLINK ((HUDPLINK)(~0))
#endif

/* common network events */
#define EVT_PRE_CLOSE   (0x0002)    /* ready to close*/
#define EVT_CLOSED      (0x0003)    /* has been closed */
#define EVT_RECEIVEDATA (0x0004)    /* receive data*/
#define EVT_PIPEDATA    (0x0005)    /* event from manual pipe notification */

/* TCP events */
#define EVT_TCP_ACCEPTED    (0x0013)   /* has been Accepted */
#define EVT_TCP_CONNECTED   (0x0014)  /* success connect to remote */

/* UDP events */
#define EVT_UDP_RECEIVE_DOMAIN (0x0021) /* UDP received data from a UNIX domian IPC */

/* option to get link address */
#define LINK_ADDR_LOCAL   (1)   /* get local using endpoint pair */
#define LINK_ADDR_REMOTE  (2)   /* get remote using endpoint pair */

/* optional  attributes of TCP link */
#define LINKATTR_TCP_FULLY_RECEIVE                      (1) /* receive fully packet include low-level head */
#define LINKATTR_TCP_NO_BUILD                           (2) /* not use @tst::builder when calling @tcp_write */
#define LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT              (4) /* copy tst and attr to accepted link when syn */

/* optional  attributes of UDP link */
#define LINKATTR_UDP_BAORDCAST                          (1)
#define LINKATTR_UDP_MULTICAST                          (2)

/* the definition control types for @nis_cntl */
#define NI_SETATTR      (1)
#define NI_GETATTR      (2)
#define NI_SETCTX       (3)
#define NI_GETCTX       (4)
#define NI_SETTST       (5)
#define NI_GETTST       (6)
#define NI_DUPCTX       (7)	/* not used */
#define NI_RISECTX      (8)
#define NI_SINKCTX      (9)

/* the dotted decimal notation for IPv4 or IPv6 */
struct nis_inet_addr {
    char i_addr[INET_ADDRSTRLEN];
};

struct nis_inet6_addr {
    char i6_addr[INET6_ADDRSTRLEN];
};

struct nis_event {
    int Event;

    union {
        struct {
            HTCPLINK Link;
        } Tcp;

        struct {
            HUDPLINK Link;
        } Udp;
    } Ln;
} __POSIX_TYPE_ALIGNED__;

typedef struct nis_event nis_event_t;

/* user callback definition for network events */
typedef void( STDCALL *nis_callback_t)(const struct nis_event *event, const void *data);
typedef nis_callback_t tcp_io_callback_t;
typedef nis_callback_t udp_io_callback_t;

/*---------------------------------------------------------------------------------------------------------------------------------------------------------
    TCP implement
---------------------------------------------------------------------------------------------------------------------------------------------------------*/

/*  private protocol template(PPT,) support
    protocol parse template: tcp_ppt_parser_t
            @data               data stream
            @cb                 bytes of data stream
            @user_data_size     bytes of user data stream (eliminate length of protocol)

    protocol builder template: tcp_ppt_builder_t
            @data               data stream
            @cb                 bytes of data stream for build

    Any negative return of PPT templates will terminate the subsequent operation
 */
typedef nsp_status_t( STDCALL *tcp_ppt_parser_t)(void *data, int cb, int *user_data_size);
typedef nsp_status_t( STDCALL *tcp_ppt_builder_t)(void *data, int cb);

struct __tcp_stream_template {
    tcp_ppt_parser_t parser_;
    tcp_ppt_builder_t builder_;
    int cb_;
} __POSIX_TYPE_ALIGNED__;

typedef struct __tcp_stream_template tst_t;

/*  @nis_serializer_t target object use for @tcp_write or @udp_write procedure call,
 *  when:
 *  @origin is a pointer to a C-style strcuture object without 1 byte aligned,
 *  or,
 *  @origin is a pointer to a simple C++ object.
 *
 *  the @serializer parameter should specify a method how to serialize @origin into byte-stream @packet,
 *  @packet will be the data buffer that is actually delivered to the kernel after @serializer call.
 *
 *  when @origin pointer to a standard C byte-stream, @serializer is ignore and can be set to null
 */
typedef nsp_status_t( STDCALL *nis_serializer_t)(unsigned char *packet, const void *origin, int cb);

struct nis_tcp_data {
    union {
        /* only used in case of EVT_RECEIVEDATA,
            @Size of bytes of data storage in @Data has been received from kernel,  */
        struct {
            const unsigned char *Data;
            int Size;
        } Packet;

        /* only used in case of EVT_TCP_ACCEPTED,
            @AcceptLink is the remote link which accepted by listener @nis_event.Ln.Tcp.Link */
        struct {
            HTCPLINK AcceptLink;
        } Accept;

        /* only used in case of EVT_PRE_CLOSE,
            @Context pointer to user define context of each link object */
        struct {
            void *Context;
        } PreClose;
    } e;
}__POSIX_TYPE_ALIGNED__;

typedef struct nis_tcp_data tcp_data_t;

/*---------------------------------------------------------------------------------------------------------------------------------------------------------
    UDP implement
---------------------------------------------------------------------------------------------------------------------------------------------------------*/
#define UDP_FLAG_NONE           (0)
#define UDP_FLAG_UNITCAST       (UDP_FLAG_NONE)
#define UDP_FLAG_BROADCAST      (LINKATTR_UDP_BAORDCAST)
#define UDP_FLAG_MULTICAST      (LINKATTR_UDP_MULTICAST)

struct nis_udp_data {
    union {
        /* only used in case of EVT_RECEIVEDATA,
            @Size of bytes of data storage in @Data has been received from kernel,
            the sender endpoint is @RemoteAddress:@RemotePort */
        struct {
            const unsigned char *Data;
            int Size;
            char RemoteAddress[16];
            unsigned short RemotePort;
        } Packet;

        struct {
            const unsigned char *Data;
            int Size;
            const char *Path; /* this field will be NULL in the case of client didn't binding with any IPC file */
        } Domain;

        /* only used in case of EVT_PRE_CLOSE,
            @Context  pointer to user defined context of each link object */
        struct {
            void *Context;
        } PreClose;
    } e;
} __POSIX_TYPE_ALIGNED__;

typedef struct nis_udp_data udp_data_t;

/*---------------------------------------------------------------------------------------------------------------------------------------------------------
    GRP implement
---------------------------------------------------------------------------------------------------------------------------------------------------------*/
struct __packet_grp_node {
    char *Data;
    int Length;
} __POSIX_TYPE_ALIGNED__;
typedef struct __packet_grp_node packet_grp_node_t;

struct __packet_grp {
    packet_grp_node_t *Items;
    int Count;
} __POSIX_TYPE_ALIGNED__;
typedef struct __packet_grp packet_grp_t;

/* the local version of nshost shared library */
struct __swnet_version {
    char compile_date[128];
} __POSIX_TYPE_ALIGNED__;
typedef struct __swnet_version swnet_version_t;

/*  receiving notification text informations from nshost moudle
    version > 9.6.0
*/
typedef void( STDCALL *nis_event_callback_t)(const char *host_event, const char *reserved, int rescb);

struct __ifmisc {
    char eth[64];
    unsigned int inet;
    unsigned int mask;
    unsigned int boardcast;
}__POSIX_TYPE_ALIGNED__;
typedef struct __ifmisc ifmisc_t;

#endif
