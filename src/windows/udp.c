#include "network.h"
#include "ncb.h"
#include "packet.h"
#include "io.h"
#include "mxx.h"

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct _udp_cinit {
	uint32_t ipv4_;
	uint16_t port_;
	udp_io_fp f_user_callback_;
	int ncb_flag_;
}udp_cinit_t;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static
void udp_shutdown_by_packet( packet_t * packet );
static
int udp_set_boardcast( ncb_t *ncb, int enable );
static
int udp_get_boardcast( ncb_t *ncb, int *enabled );

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static
int udprefr(objhld_t hld, ncb_t **ncb)
{
	if (hld < 0 || !ncb) {
		return -ENOENT;
	}

	*ncb = objrefr(hld);
	if (NULL != (*ncb)) {
		if ((*ncb)->proto_type == kProto_UDP) {
			return 0;
		}

		objdefr(hld);
		*ncb = NULL;
		return -EPROTOTYPE;
	}

	return -ENOENT;
}

static void udp_dispatch_io_recv( packet_t *packet )
{
	nis_event_t c_event;
	udp_data_t c_data;
	ncb_t *ncb;

	if (!packet) {
		return;
	}

	if (udprefr(packet->link, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", packet->link);
		return;
	}

	if (ncb->nis_callback) {
		/* 填充接收数据事件 */
		c_event.Ln.Udp.Link = (HUDPLINK)packet->link;
		c_event.Event = EVT_RECEIVEDATA;

		/* 通知接收事件 */
		c_data.e.Packet.Size = packet->size_for_translation_;
		c_data.e.Packet.Data = ( const char * ) packet->irp_;
		c_data.e.Packet.RemotePort = ntohs( packet->remote_addr.sin_port );
		if ( !inet_ntop( AF_INET, &packet->remote_addr.sin_addr, c_data.e.Packet.RemoteAddress, _countof( c_data.e.Packet.RemoteAddress ) ) ) {
			RtlZeroMemory( c_data.e.Packet.RemoteAddress, _countof( c_data.e.Packet.RemoteAddress ) );
		}

		ncb->nis_callback(&c_event, &c_data);
	}

	/* 这个时候已经是完成了处理, 继续将一个异步的 IRP 投递下去
	   本次操作， 包括缓冲区在内的一切东西都可以忽略了 */
	packet->size_for_translation_ = 0;
	asio_udp_recv( packet );

	objdefr( ncb->hld );
}

static void udp_dispatch_io_send( packet_t * packet )
{
	if ( packet ) {
		freepkt( packet );
		packet = NULL;
	}
}

static void udp_dispatch_io_exception( packet_t * packet, NTSTATUS status )
{
	ncb_t *ncb;

	if ( !packet ) {
		return;
	}

	if (udprefr(packet->link, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", packet->link);
		return;
	}

	/* 对 STATUS_PORT_UNREACHABLE / STATUS_PROTOCOL_UNREACHABLE / STATUS_HOST_UNREACHABLE 状态做过滤
	   避免这几种 ICMP PORT UNREACHABLE 导致UDP收包PENDING耗竭， 这里再次投递kRecv请求
	   并且不关闭这个链接
	 */
	if (status == STATUS_PORT_UNREACHABLE ||
		status == STATUS_PROTOCOL_UNREACHABLE ||
		status == STATUS_HOST_UNREACHABLE) {
		mxx_call_ecr("type:%d, NTSTATUS:0x%08X,link:%I64d, link remain", packet->type_, status, packet->link);
		packet->size_for_translation_ = 0;
		asio_udp_recv(packet);
	} else {
		mxx_call_ecr("type:%d, NTSTATUS:0x%08X,link:%I64d, this link will be shutdown", packet->type_, status, packet->link);
		udp_shutdown_by_packet(packet);
	}
	objdefr(ncb->hld);
}

static int udp_update_opts(ncb_t *ncb)
{
	static const int RECV_BUFFER_SIZE = 0xFFFF;
    static const int SEND_BUFFER_SIZE = 0xFFFF;

    if (!ncb) {
		return -EINVAL;
    }

    ncb_set_window_size(ncb, SO_RCVBUF, RECV_BUFFER_SIZE);
    ncb_set_window_size(ncb, SO_SNDBUF, SEND_BUFFER_SIZE);
    ncb_set_linger(ncb, 1, 0);
	return 0;
}

static int udp_entry( objhld_t h, void * user_buffer, const void * ncb_ctx )
{
	udp_cinit_t *ctx = ( udp_cinit_t * ) ncb_ctx;
	BOOL behavior;
	ncb_t *ncb = ( ncb_t * ) user_buffer;
	uint32_t bytes_returned;
	struct sockaddr_in conn_addr;
	int addrlen;

	if (!ctx || !ncb) {
		return -1;
	}

	ncb_init( ncb, kProto_UDP );
	ncb->hld = h;
	ncb->sockfd = so_create(SOCK_DGRAM, IPPROTO_UDP);
	if (ncb->sockfd == INVALID_SOCKET) {
		return -1;
	}

	do {
		if (so_bind(ncb->sockfd, ctx->ipv4_, ctx->port_) < 0) {
			break;
		}

		ncb->local_addr.sin_family = AF_INET;
		ncb->local_addr.sin_addr.S_un.S_addr = ctx->ipv4_;
		ncb->local_addr.sin_port = ctx->port_;

		// 如果采用随机端口绑定， 则应该获取真实的绑定端口
		if ( 0 == ctx->port_ ) {
			addrlen = sizeof(conn_addr);
			if (getsockname(ncb->sockfd, (SOCKADDR*)&conn_addr, &addrlen) >= 0) {
				ncb->local_addr.sin_port = ntohs( conn_addr.sin_port );
			}
		}

		// 设置一些套接字参数
		udp_update_opts( ncb );

		// UDP标记
		if ( ctx->ncb_flag_ &  UDP_FLAG_BROADCAST ) {
			if ( udp_set_boardcast( ncb, 1 ) < 0 ) {
				break;
			}
			ncb->flag_ |= UDP_FLAG_BROADCAST;
		} else {
            if (ctx->ncb_flag_ & UDP_FLAG_MULTICAST) {
                ncb->flag_ |= UDP_FLAG_MULTICAST;
            }
        }

		// 关闭因对端被强制无效化，导致的本地 io 错误
		// 这项属性打开情形下会导致 WSAECONNRESET 错误返回
		behavior = FALSE;
		if (WSAIoctl(ncb->sockfd, SIO_UDP_CONNRESET, &behavior, sizeof(behavior), NULL, 0, &bytes_returned, NULL, NULL) < 0) {
			mxx_call_ecr("syscall WSAIoctl failed to control SIO_UDP_CONNRESET,error cdoe=%u,link:%I64d", WSAGetLastError(), ncb->hld);
			break;
		}

		// 将对象绑定到异步IO的完成端口
		if (ioatth(ncb) < 0) {
			break;
		}

		// 回调用户地址， 通知调用线程， UDP 子对象已经创建完成
		ncb_set_callback( ncb, ctx->f_user_callback_ );
		return 0;
	} while ( FALSE );

	ioclose(ncb);
	return -1;
}

static void udp_unload( objhld_t h, void * user_buffer )
{
	ncb_t *ncb;

	ncb = (ncb_t *)user_buffer;
	if (!ncb) {
		return;
	}

	/* 关闭前事件 */
	ncb_post_preclose(ncb);

	/* 关闭内部套接字 */
	ioclose(ncb);

	/* 释放用户上下文数据指针 */
	if ( ncb->ncb_ctx_ && 0 != ncb->ncb_ctx_size_ ) {
		free( ncb->ncb_ctx_ );
	}

	mxx_call_ecr("object:%I64d finalization released", ncb->hld);
	ncb_post_close(ncb);
}

static objhld_t udp_allocate_object(const udp_cinit_t *ctx)
{
	ncb_t *ncb;
	objhld_t h;
	int retval;

	h = objallo( (int)sizeof( ncb_t ), NULL, &udp_unload, NULL, 0 );
	if ( h < 0 ) {
		return -1;
	}
	ncb = objrefr( h );
	retval = udp_entry( h, ncb, ctx );
	objdefr( h );

	if ( retval < 0 ) {
		objclos( h );
		return -1;
	}

	return h;
}

static packet_t **udp_allocate_recv_array( objhld_t h, int cnt )
{
	packet_t **pkt_array;
	int retval = -1;
	int i;
	int a_size = sizeof( packet_t * ) * cnt;

	// 用多个包来记录申请内存过程可能存在的异常
	pkt_array = ( packet_t ** ) malloc( a_size );
	if ( !pkt_array ) {
		return NULL;
	}
	memset( pkt_array, 0, a_size );

	// 对于每个 UDP 对象， 都会有一块 8KB 的数据在异步等待中
	// 为了避免这块内存的缺页影响性能， 这里将该片内存提升为非分页池
	for ( i = 0; i < cnt; i++ ) {
		retval = allocate_packet( h, kProto_UDP, kRecv, PAGE_SIZE, kNonPagedPool, &pkt_array[i] );
		if ( retval < 0 ) {
			break;
		}
	}

	// 申请过程发生错误， 应该会滚并释放所有的包内存
	if ( retval < 0 ) {
		for ( i = 0; i < cnt; i++ ) {
			freepkt( pkt_array[i] );
		}
		free( pkt_array );
		return NULL;
	}

	return pkt_array;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void udp_shutdown_by_packet( packet_t * packet )
{
	if ( packet ) {
		objclos( packet->link );
		freepkt( packet );
	}
}

void udp_dispatch_io_event( packet_t *packet, NTSTATUS status )
{
	// 进行IO结果判定， 如果IO失败， 应该通过回调的方式通告调用线程
	if ( !NT_SUCCESS( status ) ) {
		udp_dispatch_io_exception( packet, status );
		return;
	}

	// 状态没有反馈错误, 但是交换字节长度为0， 此对象不应该继续存在
	if ( 0 == packet->size_for_translation_ ) {
		udp_shutdown_by_packet( packet );
		return;
	}

	// 收发事件处理分派
	switch ( packet->type_ ) {
		case kRecv:
			udp_dispatch_io_recv( packet );
			break;
		case kSend:
			udp_dispatch_io_send( packet );
			break;
		default:
			break;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
PORTABLEIMPL(int) udp_init()
{
	return so_init( kProto_UDP, 0 );
}

PORTABLEIMPL(HUDPLINK) udp_create( udp_io_fp user_callback, const char* l_ipstr, uint16_t l_port, int flag )
{
	objhld_t h;
	udp_cinit_t ctx;
	packet_t **pkt_array;
	int cnts;
	int i;

	if (!user_callback) {
		return INVALID_HUDPLINK;
	}

	// 预先驱动可以投递给系统的 IRP 的恰当数量
	cnts = so_asio_count();
	if (0 == cnts) {
		return INVALID_HUDPLINK;
	}

	if ( !l_ipstr ) {
		ctx.ipv4_ = INADDR_ANY;
	} else {
		IN_ADDR l_in_addr;
		/* The InetPton function returns a value of 0 if the pAddrBuf parameter points to a string that is not a valid IPv4 dotted-decimal string or a valid IPv6 address string.
			Otherwise, a value of -1 is returned, and a specific error code can be retrieved by calling the WSAGetLastError for extended error information. */
		if ( inet_pton( AF_INET, l_ipstr, &l_in_addr ) <= 0 ) {
			return INVALID_HUDPLINK;
		}
		ctx.ipv4_ = l_in_addr.S_un.S_addr;
	}
	ctx.port_ = htons( l_port );
	ctx.f_user_callback_ = user_callback;
	ctx.ncb_flag_ = flag;

	h = udp_allocate_object( &ctx );
	if ( h < 0 ) {
		return -1;
	}

	pkt_array = udp_allocate_recv_array( h, cnts );
	if ( !pkt_array ) {
		objclos( h );
		return INVALID_HUDPLINK;
	}

	// 投递接收请求
	for ( i = 0; i < cnts; i++ ) {
		asio_udp_recv( pkt_array[i] );
	}
	free( pkt_array );
	return ( HUDPLINK ) h;
}

static
int __udp_tx_single_packet(ncb_t *ncb, const unsigned char *data, int cb, const char* r_ipstr, uint16_t r_port)
{
	int wcb;
	int offset;
	struct sockaddr_in addr;

	offset = 0;

	addr.sin_family = AF_INET;
	/*addr.sin_addr.s_addr = inet_addr(r_ipstr); */
	inet_pton(AF_INET, r_ipstr, &addr.sin_addr.S_un.S_addr);
	addr.sin_port = htons(r_port);

	while (offset < cb) {
		wcb = sendto(ncb->sockfd, data + offset, cb - offset, 0, (const struct sockaddr *)&addr, sizeof(struct sockaddr));
		if (wcb <= 0) {
			/* interrupt by other operation, continue */
			if (EINTR == errno) {
				continue;
			}

			mxx_call_ecr("syscall sendto(2) failed,error:%d,link:%I64d", WSAGetLastError(), ncb->hld);
			return -1;
		}

		offset += wcb;
	}

	return 0;
}

PORTABLEIMPL(int) udp_awaken(HUDPLINK link, const void *pipedata, int cb)
{
	ncb_t *ncb;
	packet_t *packet;
	unsigned char *buffer;
	void *epfd;

	ncb = (ncb_t *)objrefr(link);
	if (!ncb) {
		mxx_call_ecr("fail to reference link:%I64d", link);
		return -ENOENT;
	}

	buffer = NULL;
	packet = NULL;

	do {
		epfd = io_get_pipefd(ncb);
		if (!epfd) {
			break;
		}

		if (cb > 0 && pipedata) {
			buffer = (unsigned char *)malloc(cb);
			if (!buffer) {
				break;
			}
			memcpy(buffer, pipedata, cb);
		}

		if (allocate_packet((objhld_t)link, kProto_PIPE, kPipe, 0, kNoAccess, &packet) < 0) {
			break;
		}
		packet->link = link;
		packet->ori_buffer_ = buffer;

		if (!PostQueuedCompletionStatus(epfd, cb, 0, &packet->overlapped_)) {
			break;
		}
		return 0;
	} while (0);

	if (packet) {
		freepkt(packet);
	} else {
		if (buffer) {
			free(buffer);
		}
	}

	return -1;
}

PORTABLEIMPL(int) udp_write(HUDPLINK lnk, const void *origin, int cb, const char* r_ipstr, uint16_t r_port, const nis_serializer_fp serializer)
{
	int retval;
	ncb_t *ncb;
	objhld_t hld = (objhld_t)lnk;
	unsigned char *buffer;

	if (!r_ipstr || (0 == r_port) || (cb <= 0) || (lnk < 0) || (cb > MAX_UDP_UNIT)) {
		return -EINVAL;
	}

	ncb = (ncb_t *)objrefr(hld);
	if (!ncb) {
		mxx_call_ecr("fail to reference link:%I64d", hld);
		return -ENOENT;
	}

	retval = -1;
	buffer = NULL;

	do {
		buffer = (unsigned char *)malloc(cb);
		if (!buffer) {
			retval = -ENOMEM;
			break;
		}

		if (serializer) {
			if (serializer(buffer, origin, cb) < 0) {
				break;
			}
		} else {
			memcpy(buffer, origin, cb);
		}

		retval = __udp_tx_single_packet(ncb, buffer, cb, r_ipstr, r_port);
	} while (0);

	if (buffer) {
		free(buffer);
	}
	objdefr(hld);
	return retval;
}

PORTABLEIMPL(void) udp_destroy( HUDPLINK lnk )
{
	ncb_t *ncb;

	/* it should be the last reference operation of this object no matter how many ref-count now. */
	ncb = objreff(lnk);
	if (ncb) {
		mxx_call_ecr("link:%I64d order to destroy", ncb->hld);
		ioclose(ncb);
		objdefr(lnk);
	}
}

PORTABLEIMPL(void) udp_uninit()
{
	so_uninit( kProto_UDP );
}

PORTABLEIMPL(int) udp_getaddr( HUDPLINK lnk, uint32_t* ipv4, uint16_t *port_output )
{
	ncb_t * ncb;

	if ( INVALID_HUDPLINK == lnk || !ipv4 || !port_output ) {
		return -1;
	}

	if (udprefr(lnk, &ncb) >= 0) {
		*ipv4 = htonl(ncb->local_addr.sin_addr.S_un.S_addr);
		*port_output = htons(ncb->local_addr.sin_port);
		objdefr( ncb->hld );
		return 0;
	}

	mxx_call_ecr("fail to reference link:%I64d", lnk);
	return -1;
}

PORTABLEIMPL(int) udp_setopt( HUDPLINK lnk, int level, int opt, const char *val, int len )
{
	ncb_t * ncb;
	int retval = -1;

	if ( ( INVALID_HUDPLINK == lnk ) || ( !val ) ) {
		return -1;
	}

	if (udprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	retval = setsockopt(ncb->sockfd, level, opt, val, len);
	objdefr( ncb->hld );
	return retval;
}

PORTABLEIMPL(int) udp_getopt( HUDPLINK lnk, int level, int opt, char *val, int *len )
{
	ncb_t *ncb;
	int retval = -1;

	if ( ( INVALID_HUDPLINK == lnk ) || ( !val ) || ( !len ) ) {
		return -1;
	}

	if (udprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	retval = getsockopt(ncb->sockfd, level, opt, val, len);

	objdefr( ncb->hld );
	return retval;
}

PORTABLEIMPL(int) udp_initialize_grp( HUDPLINK lnk, packet_grp_t *grp )
{
	ncb_t *ncb;
	int retval = -1;
	int i;

	if (!grp) {
		return -1;
	}

	if (grp->Count <= 0) {
		return -1;
	}

	if (udprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	do {
		if (!ncb->connected_) {
			break;
		}

		for ( i = 0; i < grp->Count; i++ ) {
			if ( ( 0 == grp->Items[i].Length ) || ( grp->Items[i].Length > MTU ) ) break;
			if ( NULL == ( grp->Items[i].Data = ( char * ) malloc( grp->Items[i].Length ) ) ) break;
		}

		retval = ( ( i == grp->Count ) ? 0 : -1 );
		if (0 == retval) {
			break;
		}

		// 错误回滚
		for ( i = 0; i < grp->Count; i++ ) {
			if ( !grp->Items[i].Data ) {
				break;
			}
			free( grp->Items[i].Data );
			grp->Items[i].Data = NULL;
		}

	} while ( FALSE );

	objdefr( ncb->hld );
	return retval;
}

PORTABLEIMPL(void) udp_release_grp( packet_grp_t *grp )
{
	int i;

	if (!grp) {
		return;
	}

	if (grp->Count <= 0) {
		return;
	}

	for ( i = 0; i < grp->Count; i++ ) {
		if ( !grp->Items[i].Data ) break;
		free( grp->Items[i].Data );
		grp->Items[i].Data = NULL;
	}
}

PORTABLEIMPL(int) udp_raise_grp( HUDPLINK lnk, const char *r_ipstr, uint16_t r_port )
{
	ncb_t *ncb;
	int retval;
	struct sockaddr_in r_addr;

	if (udprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	do {
		retval = -1;

		if ( kProto_UDP != ncb->proto_type ) {
			break;
		}

		if ( inet_pton( AF_INET, r_ipstr, &r_addr.sin_addr ) <= 0 ) {
			break;
		}
		r_addr.sin_family = AF_INET;
		r_addr.sin_port = htons( r_port );

		if ( ncb->connected_ ) {
			if ( ncb->connected_ == (int)r_addr.sin_addr.S_un.S_addr ) {
				retval = 0;
			}
			break;
		}

		retval = syio_v_connect( ncb, &r_addr );
		if ( retval >= 0 ) {
			ncb->connected_ = (int)r_addr.sin_addr.S_un.S_addr;
		}

	} while ( FALSE );

	objdefr( ncb->hld );
	return retval;
}

PORTABLEIMPL(void) udp_detach_grp( HUDPLINK lnk )
{
	ncb_t * ncb;

	if (udprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return;
	}

	if (0 != ncb->connected_) {
		syio_v_disconnect(ncb);
	}

	objdefr( ncb->hld );
}

PORTABLEIMPL(int) udp_write_grp( HUDPLINK lnk, packet_grp_t *grp )
{
	ncb_t *ncb;
	packet_t *packet;
	int i;
	int retval = -1;

	if (!grp) {
		return -1;
	}

	if (grp->Count <= 0) {
		return -1;
	}

	if (udprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	do {
		if (!ncb->connected_) {
			break;
		}

		// 创建分组包
		retval = allocate_packet( ( objhld_t ) lnk, kProto_UDP, kSend, 0, kNoAccess, &packet );
		if ( retval < 0 ) {
			break;
		}

		packet->grp_packets_cnt_ = grp->Count;
		packet->grp_packets_ = ( PTRANSMIT_PACKETS_ELEMENT ) malloc( sizeof( TRANSMIT_PACKETS_ELEMENT ) * grp->Count );
		if ( packet->grp_packets_ ) {
			for ( i = 0; i < packet->grp_packets_cnt_; i++ ) {
				packet->grp_packets_[i].pBuffer = grp->Items[i].Data;
				packet->grp_packets_[i].cLength = grp->Items[i].Length;
				packet->grp_packets_[i].dwElFlags = TP_ELEMENT_MEMORY | TP_ELEMENT_EOP;
			}
			retval = syio_grp_send( packet );
		}

		// 同步完成后自动释放包， 但包中的 grp 只能交由 xx_release_grp 进行释放
		freepkt( packet );
	} while ( FALSE );

	objdefr( ncb->hld );
	return retval;
}

PORTABLEIMPL(int) udp_joingrp(HUDPLINK lnk, const char *g_ipstr, uint16_t g_port)
{
    ncb_t *ncb;
    objhld_t hld = (objhld_t) lnk;
    int retval;

    if (lnk < 0 || !g_ipstr || 0 == g_port) {
        return -EINVAL;
    }

    ncb = objrefr(hld);
	if (!ncb) {
		return -1;
	}

    do {
        retval = -1;

        if (!(ncb->flag_ & UDP_FLAG_MULTICAST)) {
            break;
        }

        /*设置回环许可*/
        int loop = 1;
		retval = setsockopt(ncb->sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof (loop));
        if (retval == SOCKET_ERROR) {
            break;
        }

        /*加入多播组*/
        if (!ncb->mreq){
            ncb->mreq = (struct ip_mreq *)malloc(sizeof(struct ip_mreq));
		}
		if ( !ncb->mreq ) {
			break;
		}
        /* ncb->mreq->imr_multiaddr.s_addr = inet_addr(g_ipstr); */
		inet_pton(AF_INET, g_ipstr, &ncb->mreq->imr_multiaddr.S_un.S_addr);
        ncb->mreq->imr_interface.s_addr = ncb->local_addr.sin_addr.s_addr;
		retval = setsockopt(ncb->sockfd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const void *)ncb->mreq, sizeof(struct ip_mreq));
        if (retval == SOCKET_ERROR){
            break;
        }

		retval = 0;
    } while (0);

    objdefr(hld);
    return retval;
}

PORTABLEIMPL(int) udp_dropgrp(HUDPLINK lnk)
{
    ncb_t *ncb;
    objhld_t hld = (objhld_t) lnk;
    int retval;

    if (lnk < 0){
        return -EINVAL;
    }

    ncb = objrefr(hld);
	if (!ncb) {
		return -1;
	}

	retval = -1;
    do{
        if (!(ncb->flag_ & UDP_FLAG_MULTICAST) || !ncb->mreq) {
            break;
        }

		/*还原回环许可*/
        int loop = 0;
		retval = setsockopt(ncb->sockfd, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loop, sizeof (loop));
        if (retval == SOCKET_ERROR) {
            break;
        }

        /*离开多播组*/
		retval = setsockopt(ncb->sockfd, IPPROTO_IP, IP_DROP_MEMBERSHIP, (const void *)ncb->mreq, sizeof(struct ip_mreq));
		if (retval == SOCKET_ERROR) {
			break;
		}

		retval = 0;
    }while(0);

    objdefr(hld);
    return retval;
}

int udp_set_boardcast(ncb_t *ncb, int enable)
{
    if (ncb) {
        return setsockopt(ncb->sockfd, SOL_SOCKET, SO_BROADCAST, (const void *) &enable, sizeof (enable));
    }
    return -EINVAL;
}

int udp_get_boardcast(ncb_t *ncb, int *enabled)
{
    if (ncb && enabled) {
        socklen_t optlen = sizeof (int);
        return getsockopt(ncb->sockfd, SOL_SOCKET, SO_BROADCAST, (void * __restrict)enabled, &optlen);
    }
    return -EINVAL;
}
