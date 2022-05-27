#include "network.h"
#include "ncb.h"
#include "packet.h"
#include "io.h"
#include "mxx.h"

#include <assert.h>
#include <mstcpip.h>

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define TCP_ACCEPT_EXTENSION_SIZE					( 1024 )
#define TCP_RECV_BUFFER_SIZE						( 17 * PAGE_SIZE )
#define TCP_LISTEN_BLOCK_COUNT						( 5 )
#define TCP_SYN_REQ_TIMES							( 150 )

#define TCP_BUFFER_SIZE								( 0x11000 )
#define TCP_MAXIMUM_PACKET_SIZE						( 50 << 20 )
#define TCP_MAXIMUM_TEMPLATE_SIZE					(32)

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
typedef struct _TCP_INIT_CONTEXT {
	uint32_t ip_;
	uint16_t port_;
	tcp_io_callback_t callback_;
	int is_remote_;
}tcp_cinit_t;

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define TCP_MAXIMUM_SENDER_CACHED_CNT				( 2000 ) // 以每个包64KB计, 最多可以接受 327MB 的发送堆积
#define TCP_MAXIMUM_SENDER_CACHED_CNT_PRE_LINK		( 500 )	 // 以每个包64KB计, 最多可以接受 32MB/Link 的发送堆积

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if (!(NTDDI_VERSION >= NTDDI_WIN10_RS2))
typedef enum _TCPSTATE {
	TCPSTATE_CLOSED,
	TCPSTATE_LISTEN,
	TCPSTATE_SYN_SENT,
	TCPSTATE_SYN_RCVD,
	TCPSTATE_ESTABLISHED,
	TCPSTATE_FIN_WAIT_1,
	TCPSTATE_FIN_WAIT_2,
	TCPSTATE_CLOSE_WAIT,
	TCPSTATE_CLOSING,
	TCPSTATE_LAST_ACK,
	TCPSTATE_TIME_WAIT,
	TCPSTATE_MAX
} TCPSTATE;

typedef struct _TCP_INFO_v0 {
	TCPSTATE State;
	ULONG    Mss;
	ULONG64  ConnectionTimeMs;
	BOOLEAN  TimestampsEnabled;
	ULONG    RttUs;
	ULONG    MinRttUs;
	ULONG    BytesInFlight;
	ULONG    Cwnd;
	ULONG    SndWnd;
	ULONG    RcvWnd;
	ULONG    RcvBuf;
	ULONG64  BytesOut;
	ULONG64  BytesIn;
	ULONG    BytesReordered;
	ULONG    BytesRetrans;
	ULONG    FastRetrans;
	ULONG    DupAcksIn;
	ULONG    TimeoutEpisodes;
	UCHAR    SynRetrans;
} TCP_INFO_v0, *PTCP_INFO_v0;
#endif

static void tcp_shutdown_by_packet( packet_t * packet );
static int tcp_save_info(ncb_t *ncb, TCP_INFO_v0 *ktcp);
static int tcp_setmss( ncb_t *ncb, int mss );
static int tcp_getmss( ncb_t *ncb );
static int tcp_set_nodelay( ncb_t *ncb, int set );
static int tcp_get_nodelay( ncb_t *ncb, int *set );

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static
int tcprefr(objhld_t hld, ncb_t **ncb)
{
	if (hld < 0 || !ncb) {
		return -EINVAL;
	}

	*ncb = objrefr(hld);
	if (NULL != (*ncb)) {
		if ((*ncb)->proto_type == kProto_TCP) {
			return 0;
		}

		objdefr(hld);
		*ncb = NULL;
		return -EPROTOTYPE;
	}

	return -ENOENT;
}

/*
 * definition of return value:
 * -1 : the calling thread has responsibility to free the memory of @packet when @packet is not a NULL pointer
 *  0 : @packet MUST NOT free
 */
static int tcp_try_write( ncb_t * ncb, packet_t *packet )
{
	packet_t *next_packet;
	int retval;
	static const int MAX_TCP_KERNEL_PENDING = 1;

	if (!ncb) {
		return -1;
	}

	retval = -1;
	next_packet = NULL;

	EnterCriticalSection(&ncb->tcp_sender_locker_);

	do {
		/* user specify the definite packet buffer pointer,
			but, current queue is not empty, it MUST add the packet to the tail of queue and pop the head element to send */
		if (packet) {
			/* increase the cached count and than push item into the tail of queue */
			list_add_tail(&packet->pkt_lst_entry_, &ncb->tcp_sender_cache_head_);
			InterlockedIncrement((volatile LONG *)&ncb->tcp_sender_list_size_);
		} else {
			/* current queue cached nothing, or, all data have been sent */
			if (list_empty(&ncb->tcp_sender_cache_head_)) {
				break;
			}
		}

		/* from now on, @packet MUST NOT free */
		retval = 0;

		/* another asynchronous operations may be in progress,
			double check to estimate whether the opportunity of send is right */
		while ( InterlockedIncrement((volatile LONG *)&ncb->tcp_write_pending_) > MAX_TCP_KERNEL_PENDING ) {
			if (InterlockedDecrement((volatile LONG *)&ncb->tcp_write_pending_) > 0) {
				break;
			}
		}

		/* the next packet should be remove from queue */
		next_packet = list_first_entry(&ncb->tcp_sender_cache_head_, packet_t, pkt_lst_entry_);
		/* detach first packet node from list */
		list_del_init(&next_packet->pkt_lst_entry_);
		/* cache count canbe decrease immediately */
		InterlockedDecrement((volatile LONG *)&ncb->tcp_sender_list_size_);
	} while(0);

	LeaveCriticalSection( &ncb->tcp_sender_locker_ );

	/* no any packet are in queue now */
	if (!next_packet) {
		return retval;
	}

	/* while error occur without IO_PENDING, the IOCP routine will not be trigger,
		nshost module consider this is a unhandleable error and the TCP link will be disconnect immediately.
		@tcp_shutdown_by_packet routine should release the owner memory save in @next_packet */
	retval = asio_tcp_send(next_packet);
	if (IOR_SHUTDOWN == retval) {
		tcp_shutdown_by_packet(next_packet);
	} else if (IOR_CAN_FREE == retval) {
		freepkt(next_packet);
	}

	return 0;
}

static
int tcp_lb_assemble( ncb_t * ncb, packet_t * packet )
{
	int current_usefule_size;
	int logic_revise_acquire_size;
	nis_event_t c_event;
	tcp_data_t c_data;
	int lb_acquire_size;
	int user_size;

	if (!packet || !ncb) {
		return -1;
	}

	if (0 == packet->size_for_translation_) {
		mxx_call_ecr("size of translation equal to zero, link:%I64d", ncb->hld);
		return -1;
	}

	if (!ncb->tcp_tst_.parser_) {
		mxx_call_ecr("tcp parser proc is empty, link:%I64d", ncb->hld);
		return -1;
	}

	// 当前可用长度定义为已分析长度加上本次交换长度
	current_usefule_size = packet->size_for_translation_;

	// 从大包缓冲区中，取得大包需要的长度
	if ( ncb->tcp_tst_.parser_( ncb->lb_data_, ncb->tcp_tst_.cb_, &user_size ) < 0 ) {
		mxx_call_ecr("tcp parser call failed, link:%I64d", ncb->hld);
		return -1;
	}

	// 大包修正请求长度 = 用户数据长度 + 底层协议长度
	logic_revise_acquire_size = user_size + ncb->tcp_tst_.cb_;

	// 大包填满长度 = 需要的修正长度 - 已经拷贝的数据偏移
	lb_acquire_size = logic_revise_acquire_size - ncb->lb_cpy_offset_;

	// 当前可用数据长度， 不足以填充整个大包, 进行数据拷贝， 调整大包偏移， 继续接收数据
	if ( current_usefule_size < lb_acquire_size ) {
		assert(ncb->lb_cpy_offset_ + current_usefule_size <= ncb->lb_length_);
		memcpy( ncb->lb_data_ + ncb->lb_cpy_offset_, ( char * ) packet->ori_buffer_, current_usefule_size );
		ncb->lb_cpy_offset_ += current_usefule_size;
		return 0;
	}

	// 足以填充大包， 则将大包填充满并回调到用户例程
	assert(ncb->lb_cpy_offset_ + lb_acquire_size <= ncb->lb_length_);
	memcpy( ncb->lb_data_ + ncb->lb_cpy_offset_, ( char * ) packet->ori_buffer_, lb_acquire_size );
	c_event.Ln.Tcp.Link = ( HTCPLINK ) ncb->hld;
	c_event.Event = EVT_RECEIVEDATA;
	c_data.e.Packet.Size = user_size;
	c_data.e.Packet.Data = ( const char * ) ( ( char * ) ncb->lb_data_ + ncb->tcp_tst_.cb_ );
	if (ncb->nis_callback) {
		ncb->nis_callback(&c_event, &c_data);
	}

	// 一次大包的解析已经完成， 销毁ncb_t中的大包字段
	ncb_unmark_lb( ncb );

	// 如果不是刚好填满大包， 即:有效数据有冗余， 则应该将有效数据剩余长度告知调用线程， 以资进行下一轮的解包操作
	if ( current_usefule_size != lb_acquire_size ) {
		memmove( packet->ori_buffer_, ( char * ) packet->ori_buffer_ + lb_acquire_size, current_usefule_size - lb_acquire_size );
		packet->size_for_translation_ -= lb_acquire_size;
	}

	return ( current_usefule_size - lb_acquire_size );
}

static
int tcp_prase_logic_packet( ncb_t * ncb, packet_t * packet )
{
	int current_usefule_size;
	int logic_revise_acquire_size;
	nis_event_t c_event;
	tcp_data_t c_data;
	int current_parse_offset;
	int user_size;
	int total_packet_length;

	if ( !packet || !ncb ) {
		return -1;
	}
	if ( 0 == packet->size_for_translation_ ) {
		return -1;
	}

	// 当前可用长度定义为已分析长度加上本次交换长度
	current_usefule_size = packet->analyzed_offset_ + packet->size_for_translation_;
	current_parse_offset = 0;

	// 没有指定包头模板， 直接回调整个TCP包
	if ( 0 == ncb->tcp_tst_.cb_ ) {
		c_event.Ln.Tcp.Link = ( HTCPLINK ) ncb->hld;
		c_event.Event = EVT_RECEIVEDATA;
		c_data.e.Packet.Size = current_usefule_size;
		c_data.e.Packet.Data = ( const char * ) ( ( char * ) packet->ori_buffer_ + current_parse_offset + ncb->tcp_tst_.cb_ );
		if (ncb->nis_callback) {
			ncb->nis_callback(&c_event, &c_data);
		}
		packet->analyzed_offset_ = 0;
		return 0;
	}

	while ( TRUE ) {

		// 如果整体包长度， 不足以填充包头， 则必须进行继续接收操作
		if (current_usefule_size < ncb->tcp_tst_.cb_) {
			break;
		}

		// 底层协议交互给协议模板处理， 处理失败则解包操作无法继续
		if ( ncb->tcp_tst_.parser_( ( char * ) packet->ori_buffer_ + current_parse_offset, ncb->tcp_tst_.cb_, &user_size ) < 0 ) {
			return -1;
		}

		/* 如果用户数据长度超出最大容忍长度，则直接报告为错误, 有可能是恶意攻击 */
		if (user_size > TCP_MAXIMUM_PACKET_SIZE || user_size < 0) {
			return -1;
		}

		// 总包长度 = 用户数据长度 + 底层协议长度
		total_packet_length = user_size + ncb->tcp_tst_.cb_;

		// 大包，不存在后续解析， 直接全拷贝后标记为大包等待状态， 从原始缓冲区开始投递IRP
		if ( total_packet_length > TCP_RECV_BUFFER_SIZE ) {
			if ( ncb_mark_lb( ncb, total_packet_length, current_usefule_size, ( char * ) packet->ori_buffer_ + current_parse_offset ) < 0 ) {
				return -1;
			}
			mxx_call_ecr("large packet marked, link:%I64d, size:%u", ncb->hld, total_packet_length);
			packet->analyzed_offset_ = 0;
			return 0;
		}

		// 当前逻辑包修正请求长度 = 底层协议中记载的用户数据长度 + 包头长度
		logic_revise_acquire_size = total_packet_length;

		// 当前可用长度不足以填充逻辑包长度， 需要继续接收数据
		if (current_usefule_size < logic_revise_acquire_size) {
			break;
		}

		// 回调到用户例程, 使用其实地址累加解析偏移， 直接赋予回调例程的结构指针， 因为const限制， 调用线程不应该刻意修改该串的值
		c_event.Ln.Tcp.Link = (HTCPLINK)ncb->hld;
		c_event.Event = EVT_RECEIVEDATA;
		if (ncb->attr & LINKATTR_TCP_FULLY_RECEIVE) {
			c_data.e.Packet.Size = user_size + ncb->tcp_tst_.cb_;
			c_data.e.Packet.Data = (const char *)((char *)packet->ori_buffer_ + current_parse_offset);
		} else {
			c_data.e.Packet.Size = user_size;
			c_data.e.Packet.Data = (const char *)((char *)packet->ori_buffer_ + current_parse_offset + ncb->tcp_tst_.cb_);
		}

		if (ncb->nis_callback) {
			ncb->nis_callback(&c_event, &c_data);
		}

		// 调整当前解析长度
		current_usefule_size -= logic_revise_acquire_size;
		current_parse_offset += logic_revise_acquire_size;
	}

	packet->analyzed_offset_ = current_usefule_size;

	// 有残余数据无法完成组包， 则拷贝到缓冲区原始起点， 并以残余长度作为下一个收包请求的偏移
	if ( ( 0 != current_usefule_size ) && ( 0 != current_parse_offset ) ) {
		memmove( packet->ori_buffer_, ( void * ) ( ( char * ) packet->ori_buffer_ + current_parse_offset ), current_usefule_size );
	}

	return 0;
}

int tcp_update_opts(ncb_t *ncb)
{
    if (!ncb) {
        return -1;
    }

    ncb_set_window_size(ncb, SO_RCVBUF, TCP_BUFFER_SIZE );
    ncb_set_window_size(ncb, SO_SNDBUF, TCP_BUFFER_SIZE );
    ncb_set_linger(ncb, 1, 0);
    ncb_set_keepalive(ncb, 1);

    tcp_set_nodelay(ncb, 1);   /* 为保证小包效率， 禁用 Nginx 算法 */

    return 0;
}

static
int tcp_entry( objhld_t h, ncb_t * ncb, const void * ctx )
{
	tcp_cinit_t *init_ctx = ( tcp_cinit_t * ) ctx;;
	int retval;

	if (!ncb || h < 0 || !ctx) {
		return -1;
	}

	retval = -1;
	memset( ncb, 0, sizeof( ncb_t ) );

	do {
		ncb_init( ncb, kProto_TCP );
		ncb_set_callback(ncb, init_ctx->callback_);
		ncb->hld = h;

		ncb->sockfd = so_create(SOCK_STREAM, IPPROTO_TCP);
		if (ncb->sockfd == INVALID_SOCKET) {
			break;
		}

		// 描述每个链接上的TCP下级缓冲区大小
		InitializeCriticalSection(&ncb->tcp_sender_locker_);
		INIT_LIST_HEAD(&ncb->tcp_sender_cache_head_);
		ncb->tcp_sender_list_size_ = 0;
		ncb->tcp_write_pending_ = 0;

		// 如果是远程连接得到的ncb_t, 操作到此完成
		if ( init_ctx->is_remote_ ) {
			retval = 0;
			break;
		}

		// setsockopt 设置套接字参数
		if (tcp_update_opts(ncb) < 0) {
			break;
		}

		// 创建阶段， 无论是否随机网卡，随机端口绑定， 都先行计入本地地址信息
		// 在执行accept, connect后， 如果是随机端口绑定， 则可以取到实际生效的地址信息
		ncb->local_addr.sin_family = PF_INET;
		ncb->local_addr.sin_addr.S_un.S_addr = init_ctx->ip_;
		ncb->local_addr.sin_port = init_ctx->port_;

		// 执行本地绑定
		if (so_bind(ncb->sockfd, ncb->local_addr.sin_addr.S_un.S_addr, ncb->local_addr.sin_port) < 0) {
			break;
		}

		// 将对象绑定到异步IO的完成端口
		if (ioatth(ncb) < 0) {
			break;
		}

		retval = 0;

	} while ( FALSE );

	if ( retval < 0 ) {
		if (ncb->sockfd > 0)  {
			ioclose(ncb);
		}
	}

	return retval;
}

static
void tcp_unload( objhld_t h, void * user_buffer )
{
	ncb_t *ncb;
	packet_t *packet;

	ncb = (ncb_t *)user_buffer;
	if (!ncb) {
		return;
	}

	// 处理关闭前事件
	ncb_post_preclose(ncb);

	// 关闭内部套接字
	ioclose(ncb);

	// 如果有未完成的大包， 则将大包内存释放
	ncb_unmark_lb( ncb );

	// 取消所有等待发送的包链
	InterlockedExchange((volatile LONG *)&ncb->tcp_sender_list_size_, 0);
	EnterCriticalSection( &ncb->tcp_sender_locker_ );
	while (!list_empty(&ncb->tcp_sender_cache_head_)) {
		packet = list_first_entry( &ncb->tcp_sender_cache_head_, packet_t, pkt_lst_entry_ );
		assert(NULL != packet);
		list_del_init(&packet->pkt_lst_entry_);
		freepkt( packet );
	}
	LeaveCriticalSection( &ncb->tcp_sender_locker_ );

	// 关闭包链的锁
	DeleteCriticalSection( &ncb->tcp_sender_locker_ );

	// 释放用户上下文数据指针
	if ( ncb->ncb_ctx_ && 0 != ncb->ncb_ctx_size_ ) {
		free( ncb->ncb_ctx_ );
	}

	mxx_call_ecr("object:%I64d finalization released", ncb->hld);

	// 处理关闭后事件
	ncb_post_close(ncb);
}

static
objhld_t tcp_allocate_object(const tcp_cinit_t *ctx)
{
	ncb_t *ncb;
	objhld_t h;
	int retval;

	h = objallo( (int)sizeof( ncb_t ), NULL, &tcp_unload, NULL, 0 );
	if ( h < 0 ) {
		return -1;
	}
	ncb = objrefr( h );
	retval = tcp_entry( h, ncb, ctx );
	objdefr( h );
	ncb = NULL;

	if ( retval < 0 ) {
		objclos( h );
		return -1;
	}

	return h;
}

static
int tcp_accept(ncb_t * ncb_listen)
{
	packet_t *accept_packet;
	objhld_t remote_link;
	tcp_cinit_t ctx;
	int i;
	int retval;

	accept_packet = NULL;
	i = 0;

	if (!ncb_listen) {
		return -1;
	}

	if (allocate_packet(ncb_listen->hld, kProto_TCP, kSyn, TCP_ACCEPT_EXTENSION_SIZE, kVirtualHeap, &accept_packet) < 0) {
		return -1;
	}

	do {
		ctx.is_remote_ = TRUE;
		ctx.ip_ = 0;
		ctx.port_ = 0;
		ctx.callback_ = ncb_listen->nis_callback;
		remote_link = tcp_allocate_object(&ctx);
		if (remote_link < 0) {
			break;
		}

		/* using handle of new ncb object for accept IO */
		accept_packet->accepted_link = remote_link;

		/* post accept to asynchronous pool */
		retval = asio_tcp_accept(accept_packet);
		if (IOR_CAN_FREE == retval) {
			freepkt(accept_packet);
		} else if (IOR_SHUTDOWN == retval) {
			tcp_shutdown_by_packet(accept_packet);
		}

		return 0;
	} while ( i++ < TCP_SYN_REQ_TIMES );

	objclos( ncb_listen->hld );

	if (accept_packet) {
		freepkt(accept_packet);
	}
	return -1;
}

static
int tcp_syn_copy( ncb_t * ncb_listen, ncb_t * ncb_accepted, packet_t * packet )
{
	static GUID GUID_GET_ACCEPTEX_SOCK_ADDRS = WSAID_GETACCEPTEXSOCKADDRS;
	static LPFN_GETACCEPTEXSOCKADDRS WSAGetAcceptExSockAddrs = NULL;
	int retval;
	NTSTATUS status;
	int cb_ioctl = 0;
	struct sockaddr *r_addr;
	struct sockaddr_in *pr, *pl;
	struct sockaddr *l_addr;
	int r_len;
	int l_len;

	if (!ncb_listen || !ncb_accepted || !packet) {
		return -1;
	}

	if ( !WSAGetAcceptExSockAddrs ) {
		status = (NTSTATUS)WSAIoctl(ncb_accepted->sockfd, SIO_GET_EXTENSION_FUNCTION_POINTER, &GUID_GET_ACCEPTEX_SOCK_ADDRS,
			sizeof( GUID_GET_ACCEPTEX_SOCK_ADDRS ), &WSAGetAcceptExSockAddrs, sizeof( WSAGetAcceptExSockAddrs ), &cb_ioctl, NULL, NULL );
		if ( !NT_SUCCESS( status ) ) {
			mxx_call_ecr("syscall WSAIoctl for GUID_GET_ACCEPTEX_SOCK_ADDRS failed,NTSTATUS=0x%08X, link:%I64d", status, ncb_accepted->hld);
			return -1;
		}
	}

	retval = setsockopt(ncb_accepted->sockfd, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char *)&ncb_listen->sockfd, sizeof(SOCKET));
	if ( retval >= 0 ) {
		WSAGetAcceptExSockAddrs( packet->irp_, 0, sizeof( struct sockaddr_in ) + 16, sizeof( struct sockaddr_in ) + 16,
			&l_addr, &l_len, &r_addr, &r_len );

		/* copy the context from listen fd to accepted one in needed */
		if (ncb_listen->attr & LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT) {
			ncb_accepted->attr = ncb_listen->attr;
			memcpy(&ncb_accepted->tcp_tst_, &ncb_listen->tcp_tst_, sizeof(tst_t));
		}

		// 将取得的链接属性赋予accept上来的ncb_t
		pr = ( struct sockaddr_in * )r_addr;
		ncb_accepted->remote_addr.sin_family = pr->sin_family;
		ncb_accepted->remote_addr.sin_port = pr->sin_port;
		ncb_accepted->remote_addr.sin_addr.S_un.S_addr = pr->sin_addr.S_un.S_addr;

		pl = ( struct sockaddr_in * )l_addr;
		ncb_accepted->local_addr.sin_family = pl->sin_family;
		ncb_accepted->local_addr.sin_port = pl->sin_port;
		ncb_accepted->local_addr.sin_addr.S_un.S_addr = pl->sin_addr.S_un.S_addr;

		if (ioatth(ncb_accepted) >= 0) {
			ncb_post_accepted(ncb_listen, ncb_accepted->hld);
			return 0;
		}
	}

	mxx_call_ecr("syscall setsockopt(2) failed(SO_UPDATE_ACCEPT_CONTEXT), error code:%u, link:%I64d", WSAGetLastError(), ncb_accepted->hld);
	return -1;
}

/* 接收连接操作包含如下部分：
	1. 接收获得的远端套接字应该允许其接收数据
	2. 本地监听套接字应该允许继续接收远端连接
	3. 接收获得的远端套接字应该赋值本地监听套接字的属性
	4. 接收缓冲区与套接字关联， 这里是远端套接字生成其唯一窗口中唯一接收缓冲区的唯一接口点 */
static
void tcp_dispatch_io_accepted( packet_t * packet )
{
	ncb_t * ncb_listen;
	ncb_t * ncb_accepted;
	packet_t *packet_recv;
	int retval;

	if (!packet)  {
		return;
	}

	if (tcprefr(packet->link, &ncb_listen) < 0) {
		mxx_call_ecr("fail to reference listen link:%I64d", packet->link);
		if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet->iopp_)) {
			freepkt(packet);
		}
		return;
	}

	ncb_accepted  = NULL;
	do {
		/* 后续操作不依赖于远端对象的引用成功， 即远端对象即使引用失败， 也不影响下个accept请求的投递 */
		if (tcprefr(packet->accepted_link, &ncb_accepted) < 0) {
			break;
		}

		retval = tcp_syn_copy(ncb_listen, ncb_accepted, packet);
		if (retval < 0) {
			objclos(ncb_accepted->hld);
			break;
		}

		retval = allocate_packet(ncb_accepted->hld, kProto_TCP, kRecv, TCP_RECV_BUFFER_SIZE, kVirtualHeap, &packet_recv);
		if (retval < 0) {
			objclos(ncb_accepted->hld);
			break;
		}

		/* ingore free event, because this piece of memory need reuse. */
		if (IOR_SHUTDOWN == asio_tcp_recv(packet_recv)){
			tcp_shutdown_by_packet(packet_recv);
		}
	} while (0);

	if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet->iopp_)) {
		freepkt(packet);
	}

	if (ncb_accepted) {
		objdefr(ncb_accepted->hld);
	}

	if (tcp_accept(ncb_listen) < 0) {
		objclos(ncb_listen->hld);
	}

	objdefr(ncb_listen->hld);
}

static
void tcp_dispatch_io_send( packet_t *packet )
{
	ncb_t *ncb;
	objhld_t h;

	if (!packet) {
		return;
	}

	/* translate bytes is zero, the only legal situation is TCP-SYN completed, all the other situation regard as fatal error, link will is going to destroy */
	if ( packet->size_for_translation_ <= 0 ) {
		mxx_call_ecr("the translated size equal to zero, link:%I64d", packet->link);
		if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet->iopp_)) {
			tcp_shutdown_by_packet(packet);
		}
		return;
	}

	h = packet->link;
	if (tcprefr(h, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", h);
		if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet->iopp_)) {
			freepkt(packet);
		}
		return;
	}

	/* release the packet buffer */
	if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet->iopp_)) {
		freepkt(packet);
	}

	/* reduce the pending count(to zero) on this link */
	InterlockedDecrement((volatile LONG *)&ncb->tcp_write_pending_);
	/* next write request MUST later than pending count decrease */
	tcp_try_write( ncb, NULL );
	objdefr( h );
}

static
void tcp_dispatch_io_recv( packet_t * packet )
{
	ncb_t *ncb;
	int retval;

	if (!packet) {
		return;
	}

	/* increase the IO phase, but didn't do any more operations in receive proc */
	InterlockedIncrement((volatile LONG *)&packet->iopp_);

	/* count of exchange bytes less than or equal to zero meat a error, for example: remote link disconnected. */
	if ( packet->size_for_translation_ <= 0 ) {
		mxx_call_ecr("the translated size equal to zero, link:%I64d", packet->link);
		if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet->iopp_)) {
			tcp_shutdown_by_packet(packet);
		}
		return;
	}

	if (tcprefr(packet->link, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", packet->link);
		if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet->iopp_)) {
			freepkt(packet);
		}
		return;
	}

	do {
		/*	已经被标记为大包， 则应该进行大包的解析和存储
			返回值小于零: 大包解析失败, 错误退出
			返回值等于零: 数据量不足以本次填充大包, 或刚好填充满本次大包后无任何数据残留
			返回值大于零: 大包完成解析后剩余的数据长度(还需要继续解下一个包) */
		if ( ncb_lb_marked( ncb ) ) {
			retval = tcp_lb_assemble( ncb, packet );
			if ( retval <= 0 ) {
				break;
			}
		}

		retval = 0;

		/* 解析 TCP 包为符合协议规范的逻辑包 */
		retval = tcp_prase_logic_packet( ncb, packet );
		if ( retval < 0 ) {
			mxx_call_ecr("fail to parse logic packet, link:%I64d", packet->link);
			break;
		}

		/* 单次解包完成， 并不一定可以确认下次投递请求的偏移在缓冲区头部， 所以需要调整 */
		packet->irp_ = ( void * ) ( ( char * ) packet->ori_buffer_ + packet->analyzed_offset_ );
		packet->size_for_req_ = TCP_RECV_BUFFER_SIZE - packet->analyzed_offset_;
	} while ( FALSE );

	if ( retval >= 0 ) {
		/* ignore anyother return values,
			IOR_CAN_FREE: receive packet should not be free.
			IOR_SYSERR: waitting for shutdown message trigger */
		if (IOR_SHUTDOWN == (retval = asio_tcp_recv(packet))) {
			tcp_shutdown_by_packet(packet);
		}
	}

	objdefr(ncb->hld);
}

static
void tcp_dispatch_io_connected(packet_t * packet_connect){
	ncb_t *ncb;
	packet_t *packet_recv;
	struct sockaddr_in addr;
	int addrlen;
	objhld_t link;

	if (!packet_connect) {
		return;
	}

	packet_recv = NULL;
	link = packet_connect->link;

	/* connect packet nolonger use */
	if (kPagePhase_CanbeFree == InterlockedIncrement((volatile LONG *)&packet_connect->iopp_)) {
		freepkt(packet_connect);
	}

	if (tcprefr(link, &ncb) < 0) {
		mxx_call_ecr("failed reference link:%I64d", link);
		return;
	}

	do {
		/* 如果本地采取随机地址结构或端口， 则需要取得唯一生效的地址结构和端口 */
		if (0 == ncb->local_addr.sin_port || 0 == ncb->local_addr.sin_addr.S_un.S_addr) {
			addrlen = sizeof(addr);
			if (getsockname(ncb->sockfd, (struct sockaddr *)&addr, &addrlen) >= 0) {
				ncb->local_addr.sin_port = addr.sin_port;
				/*为了保持兼容性， 这里转换地址为大端*/
				ncb->local_addr.sin_addr.S_un.S_addr = ntohl(addr.sin_addr.S_un.S_addr);
			}
		}

		/* 异步连接完成后，更新连接对象的上下文属性 */
		if (setsockopt(ncb->sockfd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) >= 0){
			addrlen = sizeof(ncb->remote_addr);
			getpeername(ncb->sockfd, (struct sockaddr *)&ncb->remote_addr, &addrlen);
		}

		/* 成功连接对端， 应该投递一个接收数据的IRP， 允许这个连接接收数据 */
		if (allocate_packet(ncb->hld, kProto_TCP, kRecv, TCP_RECV_BUFFER_SIZE, kVirtualHeap, &packet_recv) < 0) {
			mxx_call_ecr("alloc packet failed,link:%I64d", ncb->hld);
			break;
		}

		if (IOR_SHUTDOWN == asio_tcp_recv(packet_recv)) {
			mxx_call_ecr("asio_tcp_recv failed,link:%I64d", ncb->hld);
			tcp_shutdown_by_packet(packet_recv);
			break;
		}

		/* notify the connected result */
		ncb_post_connected(ncb);
		mxx_call_ecr("tcp asynchronous connect success,link:%I64d", ncb->hld);
		objdefr(ncb->hld);
		return;
	} while ( 0 );

	objdefr(ncb->hld);
	objclos(ncb->hld);
}

static
void tcp_dispatch_io_exception( packet_t * packet, NTSTATUS status )
{
	ncb_t * ncb;

	if (!packet) {
		return;
	}

	mxx_call_ecr("IO exception catched on type:%d, NTSTATUS:0x%08X, lnk:%I64d", packet->type_, status, packet->link);

	/* ncb object no longer effective, packet should freed rightnow */
	if (tcprefr(packet->link, &ncb) < 0) {
		mxx_call_ecr("failed reference link:%I64d", packet->link);
		freepkt(packet);
		return;
	}

	do {
		/* link will be destroy,when the exception happen on the origin request without send.*/
		if (kSend != packet->type_) {
			tcp_shutdown_by_packet(packet);
			break;
		}

		/* these reasons are not necessary to continue */
		if (STATUS_REMOTE_DISCONNECT == status ||
			STATUS_CONNECTION_DISCONNECTED == status ||
			STATUS_CONNECTION_RESET == status) {
			tcp_shutdown_by_packet(packet);
			break;
		}

		/* reduce pending packet count in current ncb context */
		InterlockedDecrement((volatile LONG *)&ncb->tcp_write_pending_);
		/* release the current package */
		freepkt(packet);
		/* try post next package */
		//tcp_try_write(ncb, NULL);
	} while (0);

	objdefr(ncb->hld);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void tcp_dispatch_io_event( packet_t *packet, NTSTATUS status )
{
	if (!packet) {
		return;
	}

	if ( !NT_SUCCESS( status ) ) {
		tcp_dispatch_io_exception( packet, status );
		return;
	}

	switch ( packet->type_ ) {
		case kSyn:
			tcp_dispatch_io_accepted(packet);
			break;
		case kRecv:
			tcp_dispatch_io_recv( packet );
			break;
		case kSend:
			tcp_dispatch_io_send( packet );
			break;
		case kConnect:
			tcp_dispatch_io_connected(packet);
			break;
		default:
			break;
	}
}

/*++
	重要:
	tcp_shutdown_by_packet 不是一个应该被经常被调用的过程
	这个函数的调用多用于无法处理的异常， 而且这个函数的调用， 除了会释放包的内存外， 还会关闭包内的ncb_t对象
	需要谨慎使用
	--*/
void tcp_shutdown_by_packet( packet_t * packet )
{
	if (!packet) {
		return;
	}

	switch ( packet->type_ ) {
		case kRecv:
		case kSend:
		case kConnect:
			mxx_call_ecr("type:%d link:%I64d", packet->type_, packet->link);
			objclos(packet->link);
			freepkt( packet );
			break;

			//
			// ACCEPT需要作出特殊处理
			// 1. 不关闭监听的对象
			// 2. 关闭出错的请求中， accept的对象
			// 3. 重新扔出一个accept请求
			//
		case kSyn:
			mxx_call_ecr("accept link:%I64d, listen link:%I64d", packet->accepted_link, packet->link);
			/*
			if (tcprefr(packet->link, &ncb) >= 0) {
				tcp_accept(ncb);
				objdefr(ncb->hld);
			} else {
				mxx_call_ecr("fail reference listen object link:%I64d", packet->link);
			}*/

			objclos(packet->accepted_link);
			freepkt(packet);
			break;

		default:
			break;
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
PORTABLEIMPL(int) tcp_init()
{
	return so_init( kProto_TCP, 0 );
}

PORTABLEIMPL(void) tcp_uninit()
{
	so_uninit( kProto_TCP );
}

PORTABLEIMPL(int) tcp_settst( HTCPLINK lnk, const tst_t *tst )
{
	ncb_t *ncb;

	if (!tst) {
		return -1;
	}

	if (tcprefr(lnk, &ncb) < 0) {
		return -1;
	}

	ncb->tcp_tst_.cb_ = tst->cb_;
	ncb->tcp_tst_.builder_ = tst->builder_;
	ncb->tcp_tst_.parser_ = tst->parser_;

	objdefr(ncb->hld);
	return 0;
}

int tcp_settst_r(HTCPLINK link, tst_t *tst)
{
	ncb_t *ncb;
	int retval;

	if (!tst) {
		return -EINVAL;
	}

	/* size of tcp template must be less or equal to 32 bytes */
	if (tst->cb_ > TCP_MAXIMUM_TEMPLATE_SIZE) {
		mxx_call_ecr("tst size must less than 32 byte.");
		return -EINVAL;
	}

	retval = tcprefr(link, &ncb);
	if (retval < 0) {
		return retval;
	}

	InterlockedExchange((volatile LONG *)&ncb->tcp_tst_.cb_, tst->cb_);
	InterlockedExchangePointer((volatile PVOID *)&ncb->tcp_tst_.builder_, tst->builder_);
	InterlockedExchangePointer((volatile PVOID *)&ncb->tcp_tst_.parser_, tst->parser_);
	objdefr(link);
	return retval;
}

PORTABLEIMPL(int) tcp_gettst( HTCPLINK lnk, tst_t *tst )
{
	ncb_t *ncb;

	if (tcprefr(lnk, &ncb) < 0) {
		return -1;
	}

	tst->cb_ = ncb->tcp_tst_.cb_;
	tst->builder_ = ncb->tcp_tst_.builder_;
	tst->parser_ = ncb->tcp_tst_.parser_;

	objdefr(ncb->hld);
	return 0;
}

int tcp_gettst_r(HTCPLINK link, tst_t *tst, tst_t *previous)
{
	ncb_t *ncb;
	int retval;
	tst_t local;

	if (!tst) {
		return -EINVAL;
	}

	retval = tcprefr(link, &ncb);
	if (retval < 0) {
		return retval;
	}

	local.cb_ = InterlockedExchange((volatile LONG *)&tst->cb_, ncb->tcp_tst_.cb_);
	local.builder_ = InterlockedExchangePointer((volatile PVOID *)&tst->builder_, ncb->tcp_tst_.builder_);
	local.parser_ = InterlockedExchangePointer((volatile PVOID *)&tst->parser_, ncb->tcp_tst_.parser_);
	objdefr(link);

	if (previous) {
		memcpy(previous, &local, sizeof(local));
	}
	return retval;
}

PORTABLEIMPL(HTCPLINK) tcp_create(tcp_io_callback_t user_callback, const char* l_ipstr, uint16_t l_port)
{
	tcp_cinit_t ctx;

	if (!user_callback) {
		return INVALID_HTCPLINK;
	}

	if ( !l_ipstr ) {
		ctx.ip_ = INADDR_ANY;
	} else {
		struct in_addr l_in_addr;
		if ( inet_pton( AF_INET, l_ipstr, &l_in_addr ) <= 0 ) {
			return INVALID_HTCPLINK;
		}
		ctx.ip_ = l_in_addr.S_un.S_addr;
	}
	ctx.port_ = htons( l_port );
	ctx.callback_ = user_callback;
	ctx.is_remote_ = FALSE;

	return ( HTCPLINK ) tcp_allocate_object( &ctx );
}

/*
 * 关闭响应变更:
 * 对象销毁操作有可能是希望中断某些阻塞操作， 如 connect
 * 故将销毁行为调整为直接关闭描述符后， 通过智能指针销毁对象
 */
PORTABLEIMPL(void) tcp_destroy( HTCPLINK lnk )
{
	ncb_t *ncb;

	/* it should be the last reference operation of this object, no matter how many ref-count now. */
	ncb = objreff(lnk);
	if (ncb) {
		mxx_call_ecr("link:%I64d order to destroy", ncb->hld);
		ioclose(ncb);
		objdefr(lnk);
	}
}

PORTABLEIMPL(int) tcp_connect( HTCPLINK lnk, const char* r_ipstr, uint16_t port )
{
	ncb_t *ncb;
	struct sockaddr_in r_addr;
	packet_t *packet ;

	packet = NULL;

	if (!r_ipstr || 0 == port) {
		return -EINVAL;
	}

	if (tcprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	if ( inet_pton( AF_INET, r_ipstr, &r_addr.sin_addr ) <= 0 ) {
		objdefr(ncb->hld);
		return - 1;
	}
	r_addr.sin_family = AF_INET;
	r_addr.sin_port = htons( port );

	do {
		if (connect(ncb->sockfd, (const struct sockaddr *)&r_addr, sizeof(r_addr)) == SOCKET_ERROR) {
			mxx_call_ecr("syscall connect(2) failed,target endpoint=%s:%u, error:%u, link:%I64d", r_ipstr, port, WSAGetLastError(), ncb->hld);
			break;
		}

		// 如果本地采取随机地址结构或端口， 则需要取得唯一生效的地址结构和端口
		if ( 0 == ncb->local_addr.sin_port || 0 == ncb->local_addr.sin_addr.S_un.S_addr ) {
			struct sockaddr_in name;
			int name_length = sizeof( name );
			if (getsockname(ncb->sockfd, (struct sockaddr *)&name, &name_length) >= 0) {
				ncb->local_addr.sin_port = name.sin_port;
				ncb->local_addr.sin_addr.S_un.S_addr = ntohl( name.sin_addr.S_un.S_addr );/*为了保持兼容性， 这里转换地址为大端*/
			}
		}

		// 成功连接对端， 应该投递一个接收数据的IRP， 允许这个连接接收数据
		if (allocate_packet(ncb->hld, kProto_TCP, kRecv, TCP_RECV_BUFFER_SIZE, kVirtualHeap, &packet) < 0) {
			break;
		}

		if ( IOR_SHUTDOWN == asio_tcp_recv(packet) ) {
			tcp_shutdown_by_packet(packet);
			break;
		}

		ncb_post_connected(ncb);
		inet_pton( AF_INET, r_ipstr, &ncb->remote_addr.sin_addr );
		ncb->remote_addr.sin_port = htons( port );

		objdefr(ncb->hld);
		return 0;

	} while ( FALSE );

	objdefr(ncb->hld);
	return -1;
}

PORTABLEIMPL(int) tcp_connect2(HTCPLINK lnk, const char* r_ipstr, uint16_t port)
{
	ncb_t *ncb;
	packet_t *packet;
	int retval;

	if (!r_ipstr || 0 == port) {
		return -EINVAL;
	}

	if (tcprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	do {
		packet = NULL;
		if (allocate_packet(ncb->hld, kProto_TCP, kConnect, 0, kNoAccess, &packet) < 0) {
			return -1;
		}

		if (inet_pton(AF_INET, r_ipstr, &packet->remote_addr.sin_addr) <= 0) {
			break;
		}
		packet->remote_addr.sin_family = AF_INET;
		packet->remote_addr.sin_port = htons(port);

		retval = asio_tcp_connect(packet);
		if (IOR_SHUTDOWN == retval) {
			tcp_shutdown_by_packet(packet);
		} else if (IOR_CAN_FREE == retval) {
			freepkt(packet);
		} else {
			;
		}

		objdefr(ncb->hld);
		return 0;

	} while (FALSE);

	if (packet) {
		freepkt(packet);
	}
	objdefr(ncb->hld);
	return -1;
}

PORTABLEIMPL(int) tcp_listen( HTCPLINK lnk, int block )
{
	ncb_t *ncb;
	int retval;

	if (tcprefr(lnk, &ncb) < 0 ) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	do {
		if (0 == block) {
			block = TCP_LISTEN_BLOCK_COUNT;
		}

		retval = listen(ncb->sockfd, block);
		if ( retval < 0 ) {
			mxx_call_ecr("syscall listen failed,error code=%u", WSAGetLastError());
			break;
		}

		while ( block-- >= 0 && retval >= 0 ) {
			retval = tcp_accept(ncb);
		}

	} while ( FALSE );

	objdefr(ncb->hld);
	return retval;
}

PORTABLEIMPL(int) tcp_awaken(HUDPLINK link, const void *pipedata, int cb)
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

PORTABLEIMPL(int) tcp_write(HTCPLINK lnk, const void *origin, int cb, const nis_serializer_t serializer)
{
	char *buffer;
	ncb_t *ncb;
	packet_t *packet;
	int total_packet_length;
	int retval;

	if (INVALID_HTCPLINK == lnk || cb <= 0 || cb > TCP_MAXIMUM_PACKET_SIZE || !origin) {
		return -1;
	}

	ncb = NULL;
	buffer = NULL;
	packet = NULL;
	retval = -1;

	if ( tcprefr(lnk, &ncb) < 0 ) {
		mxx_call_ecr("failed reference object, link:%I64d", lnk);
		return -ENOENT;
	}

	do {
		/* examine the cached count, it MUST less than the restrict right now.
			for multiple threading reason, the real size maybe large than restrict size*/
		if ( InterlockedAdd((volatile LONG *)&ncb->tcp_sender_list_size_, 0) >= TCP_MAXIMUM_SENDER_CACHED_CNT_PRE_LINK) {
			mxx_call_ecr("pre-sent cache overflow, link:%I64d", lnk);
			break;
		}

		if ((!ncb->tcp_tst_.builder_) || (ncb->attr & LINKATTR_TCP_NO_BUILD)) {
			total_packet_length = cb;
			/* the protocol builder are not specified, @tcp_write proc has responsibility to fill the packet buffer */
			if (NULL == (buffer = (char *)malloc(total_packet_length))) {
				break;
			}

			if (serializer) {
				if (serializer(buffer, origin, cb) < 0) {
					break;
				}
			} else {
				memcpy(buffer, origin, cb);
			}
		} else {
			total_packet_length = ncb->tcp_tst_.cb_ + cb;
			if (NULL == (buffer = (char *)malloc(total_packet_length))) {
				break;
			}

			/* pass the buffer to the protocol builder */
			if (ncb->tcp_tst_.builder_(buffer, cb) < 0) {
				break;
			}

			if (serializer) {
				if (serializer(buffer + ncb->tcp_tst_.cb_, origin, cb - ncb->tcp_tst_.cb_) < 0) {
					break;
				}
			} else {
				memcpy(buffer + ncb->tcp_tst_.cb_, origin, cb);
			}
		}

		/* allocate the package */
		if ( allocate_packet( ( objhld_t ) lnk, kProto_TCP, kSend, 0, kNoAccess, &packet ) < 0 ) {
			break;
		}
		//mxx_call_ecr("allocate packet;%p", packet);
		packet->ori_buffer_ = buffer;
		packet->irp_ = buffer;
		packet->size_for_req_ = total_packet_length;

		/* try to write this packet to network adpater or cache it into low-level queue */
		retval = tcp_try_write(ncb, packet);
	} while ( FALSE );

	if (retval < 0) {
		if (packet) {
			freepkt(packet);
		} else {
			if (buffer) {
				free(buffer);
				buffer = NULL;
			}
		}
	}

	objdefr(ncb->hld);
	return 0;
}

PORTABLEIMPL(int) tcp_getaddr( HTCPLINK lnk, int nType, uint32_t *ipv4, uint16_t *port )
{
	ncb_t * ncb;
	int retval;

	if ( INVALID_HTCPLINK == lnk || !ipv4 || !port ) {
		return -1;
	}

	if (tcprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	retval = 0;

	if ( LINK_ADDR_LOCAL == nType ) {
		*ipv4 = htonl( ncb->local_addr.sin_addr.S_un.S_addr );
		*port = htons( ncb->local_addr.sin_port );
	} else {
		if (LINK_ADDR_REMOTE == nType) {
			*ipv4 = htonl(ncb->remote_addr.sin_addr.S_un.S_addr);
			*port = htons(ncb->remote_addr.sin_port);
		} else {
			retval = -1;
		}
	}

	objdefr(ncb->hld);

	return retval;
}

PORTABLEIMPL(int) tcp_setopt( HTCPLINK lnk, int level, int opt, const char *val, int len )
{
	ncb_t * ncb;
	int retval = -1;

	if ((INVALID_HTCPLINK == lnk) || (!val)) 	{
		return -1;
	}

	if (tcprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	if ( kProto_TCP == ncb->proto_type ) {
		retval = setsockopt(ncb->sockfd, level, opt, val, len);
		if ( retval == SOCKET_ERROR ) {
			mxx_call_ecr("syscall setsockopt(2) failed,error code:%u, link:%I64d", WSAGetLastError(), ncb->hld);
		}
	}

	objdefr(ncb->hld);
	return retval;
}

PORTABLEIMPL(int) tcp_getopt( HTCPLINK lnk, int level, int opt, char *OptVal, int *len )
{
	ncb_t * ncb;
	int retval = -1;

	if ((INVALID_HTCPLINK == lnk) || (!OptVal) || (!len)) {
		return -1;
	}

	if (tcprefr(lnk, &ncb) < 0) {
		mxx_call_ecr("fail to reference link:%I64d", lnk);
		return -1;
	}

	if ( kProto_TCP == ncb->proto_type ) {
		retval = getsockopt(ncb->sockfd, level, opt, OptVal, len);
		if ( retval == SOCKET_ERROR ) {
			mxx_call_ecr("syscall failed getsockopt ,error code:%u,link:%I64d", WSAGetLastError(), ncb->hld);
		}
	}

	objdefr(ncb->hld);
	return retval;
}

//  Minimum supported client
//	Windows 10, version 1703[desktop apps only]
//	Minimum supported server
//	Windows Server 2016[desktop apps only]
int tcp_save_info(ncb_t *ncb, TCP_INFO_v0 *ktcp)
{
	//WSAIoctl(ncb->sockfd, SIO_TCP_INFO,
	//	(LPVOID)lpvInBuffer,   // pointer to a DWORD
	//	(DWORD)cbInBuffer,    // size, in bytes, of the input buffer
	//	(LPVOID)lpvOutBuffer,         // pointer to a TCP_INFO_v0 structure
	//	(DWORD)cbOutBuffer,       // size of the output buffer
	//	(LPDWORD)lpcbBytesReturned,    // number of bytes returned
	//	(LPWSAOVERLAPPED)lpOverlapped,   // OVERLAPPED structure
	//	(LPWSAOVERLAPPED_COMPLETION_ROUTINE)lpCompletionRoutine,  // completion routine
	//	);
	return -1;
}

int tcp_setmss(ncb_t *ncb, int mss)
{
    if (ncb && mss > 0) {
        return setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_MAXSEG, (const void *) &mss, sizeof(mss));
    }

    return -EINVAL;
}

int tcp_getmss(ncb_t *ncb)
{
    if (ncb){
        socklen_t lenmss = sizeof(ncb->mss);
        return getsockopt(ncb->sockfd, IPPROTO_TCP, TCP_MAXSEG, (void *__restrict)&ncb->mss, &lenmss);
    }
    return -EINVAL;
}

int tcp_set_nodelay(ncb_t *ncb, int set){
    if (ncb ){
        return setsockopt(ncb->sockfd, IPPROTO_TCP, TCP_NODELAY, (const void *) &set, sizeof ( set));
    }

    return -EINVAL;
}

int tcp_get_nodelay(ncb_t *ncb, int *set)
{
    if (ncb && set) {
        socklen_t optlen = sizeof(int);
        return getsockopt(ncb->sockfd, IPPROTO_TCP, TCP_NODELAY, (void *__restrict)set, &optlen);
    }
    return -EINVAL;
}

PORTABLEIMPL(int) tcp_setattr(HTCPLINK lnk, int attr, int enable)
{
	ncb_t *ncb;
	int retval;

	retval = tcprefr(lnk, &ncb);
	if (retval < 0) {
		return retval;
	}

	switch (attr) {
		case LINKATTR_TCP_FULLY_RECEIVE:
		case LINKATTR_TCP_NO_BUILD:
		case LINKATTR_TCP_UPDATE_ACCEPT_CONTEXT:
			(enable > 0) ? (ncb->attr |= attr) : (ncb->attr &= ~attr);
			retval = 0;
			break;
		default:
			retval = -EINVAL;
			break;
	}

	objdefr(lnk);
	return retval;
}

PORTABLEIMPL(int) tcp_getattr(HTCPLINK lnk, int attr, int *enabled)
{
	ncb_t *ncb;
	int retval;

	retval = tcprefr(lnk, &ncb);
	if (retval < 0) {
		return retval;
	}

	if (ncb->attr & attr) {
		*enabled = 1;
	} else {
		*enabled = 0;
	}

	objdefr(lnk);
	return retval;
}
