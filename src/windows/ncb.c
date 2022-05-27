#include "network.h"
#include "ncb.h"

#include "mxx.h"

#include <assert.h>

/*++
	重要:

	无法处理的异常:
	假如对端有一个socket成功accept本地connect上去的socket后， 不调用recv收取数据，理论上，本地发送缓冲区满后溢出， 或者[TCP WINDOW]满后， 本地调用[WSASend]应该得到一个失败
	但是事实证明不会， 操作系统会无休止的接收WSASend的异步请求， 虽然IRP无法被完成， 但是允许无限堆积， 最终导致系统崩溃
	更加严重的后果是， 因为[TCP WINDOW]已经为0， 即使此时对端断开链接， 本地也无法收到 fin ack 的应答，  不会得到IOCP对WSASend调用者的通知， 这会导致程序很快的死亡
	为了避免这个问题， 在没有更标准的方法解决前， 只能限制每个链接的窗口数， 在一定层度上保证安全性

	实验证明， 即使发生了上述异常情况， 本地closesocket的调用， 可以恢复异常， 并得到一个STATUS_LOCAL_DISCONNECT(0xC000013B)的结果， 因此， 只要保证程序不在发生异常阶段崩溃
	就好有挽回余地

	由于任何原因导致的PENDING个数高于 NCB_MAXIMUM_TCP_SENDER_IRP_PEDNING_COUNT, tcp_write 过程调用将会直接返回失败

	(2016-05-26)
	对 TCP 发送缓冲区作长度限制，在达到指定长度之前，均可以将缓冲区投递给操作系统, 详见 ncb_t::tcp_usable_sender_cache_ 的使用
	--*/

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define NCB_UDP_ROOT_IDX							(0)
#define NCB_TCP_ROOT_IDX							(1)
#define NCB_MAXIMUM_PROTOCOL_ROOT					(3)

#define NCB_UDP_HASHMAP_SIZE						(59)
#define NCB_TCP_HASHMAP_SIZE						(599)

void ncb_init( ncb_t * ncb, enum proto_type_t proto_type )
{
	if ( ncb ) {
		memset( ncb, 0, sizeof( ncb_t ) );
		ncb->sockfd = INVALID_SOCKET;
		ncb->proto_type = proto_type;
	}
}

int ncb_mark_lb( ncb_t *ncb, int cb, int current_size, void * source )
{
	if (!ncb || (cb < current_size)) {
		return -1;
	}

	ncb->lb_length_ = cb;
	ncb->lb_data_ = ( char * ) malloc( ncb->lb_length_ );
	if ( !ncb->lb_data_ ) {
		mxx_call_ecr("fail to allocate memory for ncb->lb_data_, request size=%u", cb);
		return -1;
	}

	ncb->lb_cpy_offset_ = current_size;
	if (0 == current_size) {
		return 0;
	}

	/* 申请的同时有数据需要拷贝 */
	memcpy( ncb->lb_data_, source, ncb->lb_cpy_offset_ );
	return 0;
}

void ncb_unmark_lb( ncb_t *ncb )
{
	if ( ncb ) {
		if ( ncb->lb_data_) {
			assert( ncb->lb_length_ > 0 );
			if ( ncb->lb_length_ > 0 ) {
				free( ncb->lb_data_ );
				ncb->lb_data_ = NULL;
			}
			ncb->lb_cpy_offset_ = 0;
		}

		ncb->lb_length_ = 0;
	}
}

int ncb_set_rcvtimeo(ncb_t *ncb, struct timeval *timeo)
{
    if (ncb && timeo > 0){
        return setsockopt(ncb->sockfd, SOL_SOCKET, SO_RCVTIMEO, (const void *)timeo, sizeof(struct timeval));
    }
    return -EINVAL;
}

int ncb_get_rcvtimeo(ncb_t *ncb)
{
    if (ncb){
         socklen_t optlen =sizeof(ncb->rcvtimeo);
        return getsockopt(ncb->sockfd, SOL_SOCKET, SO_RCVTIMEO, (void *__restrict)&ncb->rcvtimeo, &optlen);
    }
    return -EINVAL;
}

int ncb_set_sndtimeo(ncb_t *ncb, struct timeval *timeo)
{
    if (ncb && timeo > 0){
        return setsockopt(ncb->sockfd, SOL_SOCKET, SO_SNDTIMEO, (const void *)timeo, sizeof(struct timeval));
    }
    return -EINVAL;
}

int ncb_get_sndtimeo(ncb_t *ncb){
    if (ncb){
        socklen_t optlen =sizeof(ncb->sndtimeo);
        return getsockopt(ncb->sockfd, SOL_SOCKET, SO_SNDTIMEO, (void *__restrict)&ncb->sndtimeo, &optlen);
    }
    return -EINVAL;
}

int ncb_set_iptos(ncb_t *ncb, int tos)
{
    unsigned char type_of_service = (unsigned char )tos;
    if (ncb && type_of_service){
        return setsockopt(ncb->sockfd, IPPROTO_IP, IP_TOS, (const void *)&type_of_service, sizeof(type_of_service));
    }
    return -EINVAL;
}

int ncb_get_iptos(ncb_t *ncb)
{
    if (ncb){
        socklen_t optlen =sizeof(ncb->iptos);
        return getsockopt(ncb->sockfd, IPPROTO_IP, IP_TOS, (void *__restrict)&ncb->iptos, &optlen);
    }
    return -EINVAL;
}

int ncb_set_window_size(ncb_t *ncb, int dir, int size)
{
    if (ncb){
        return setsockopt(ncb->sockfd, SOL_SOCKET, dir, (const void *)&size, sizeof(size));
    }

     return -EINVAL;
}

int ncb_get_window_size(ncb_t *ncb, int dir, int *size)
{
    if (ncb && size){
        socklen_t optlen = sizeof(int);
        if (getsockopt(ncb->sockfd, SOL_SOCKET, dir, (void *__restrict)size, &optlen) < 0){
            return -1;
        }
    }

     return -EINVAL;
}

int ncb_set_linger(ncb_t *ncb, int onoff, int lin)
{
    struct linger lgr;

    if (!ncb){
        return -EINVAL;
    }

    lgr.l_onoff = onoff;
    lgr.l_linger = lin;
    return setsockopt(ncb->sockfd, SOL_SOCKET, SO_LINGER, (char *) &lgr, sizeof ( struct linger));
}

int ncb_get_linger(ncb_t *ncb, int *onoff, int *lin) 
{
    struct linger lgr;
    socklen_t optlen = sizeof (lgr);

    if (!ncb) {
        return -EINVAL;
    }

    if (getsockopt(ncb->sockfd, SOL_SOCKET, SO_KEEPALIVE, (void *__restrict) & lgr, &optlen) < 0) {
        return -1;
    }

    if (onoff){
        *onoff = lgr.l_onoff;
    }

    if (lin){
        *lin = lgr.l_linger;
    }

    return 0;
}

int ncb_set_keepalive(ncb_t *ncb, int enable) 
{
    if (ncb) {
        return setsockopt(ncb->sockfd, SOL_SOCKET, SO_KEEPALIVE, (const char *) &enable, sizeof ( enable));
    }
    return -EINVAL;
}

int ncb_get_keepalive(ncb_t *ncb, int *enabled)
{
    if (ncb && enabled) {
        socklen_t optlen = sizeof(int);
        return getsockopt(ncb->sockfd, SOL_SOCKET, SO_KEEPALIVE, (void *__restrict)enabled, &optlen);
    }
    return -EINVAL;
}

void ncb_post_preclose(const ncb_t *ncb) 
{
	nis_event_t c_event;
	tcp_data_t c_data;

	if (ncb) {
		if (ncb->nis_callback) {
			c_event.Ln.Tcp.Link = ncb->hld;
			c_event.Event = EVT_PRE_CLOSE;
			c_data.e.PreClose.Context = ncb->context;
			ncb->nis_callback(&c_event, &c_data);
		}
	}
}

void ncb_post_close(const ncb_t *ncb) 
{
	nis_event_t c_event;

	if (ncb) {
		if (ncb->nis_callback) {
			c_event.Ln.Tcp.Link = ncb->hld;
			c_event.Event = EVT_CLOSED;
			ncb->nis_callback(&c_event, NULL);
		}
	}
}

void ncb_post_recvdata(const ncb_t *ncb, int cb, const unsigned char *data)
{
	nis_event_t c_event;
	tcp_data_t c_data;

	if (ncb) {
		if (ncb->nis_callback) {
			c_event.Ln.Tcp.Link = (HTCPLINK)ncb->hld;
			c_event.Event = EVT_RECEIVEDATA;
			c_data.e.Packet.Size = cb;
			c_data.e.Packet.Data = data;
			ncb->nis_callback(&c_event, &c_data);
		}
	}
}

void ncb_post_pipedata(const ncb_t *ncb, int cb, const unsigned char *data)
{
	nis_event_t c_event;
	tcp_data_t c_data;

	if (ncb) {
		if (ncb->nis_callback) {
			c_event.Ln.Tcp.Link = (HTCPLINK)ncb->hld;
			c_event.Event = EVT_PIPEDATA;
			c_data.e.Packet.Size = cb;
			c_data.e.Packet.Data = data;
			ncb->nis_callback(&c_event, &c_data);
		}
	}
}

void ncb_post_accepted(const ncb_t *ncb, HTCPLINK link) 
{
	nis_event_t c_event;
	tcp_data_t c_data;

	if (ncb) {
		if (ncb->nis_callback) {
			c_event.Event = EVT_TCP_ACCEPTED;
			c_event.Ln.Tcp.Link = ncb->hld;
			c_data.e.Accept.AcceptLink = link;
			ncb->nis_callback(&c_event, &c_data);
		}
	}
}

void ncb_post_connected(const ncb_t *ncb) 
{
	nis_event_t c_event;

	if (ncb) {
		if (ncb->nis_callback) {
			c_event.Event = EVT_TCP_CONNECTED;
			c_event.Ln.Tcp.Link = ncb->hld;
			ncb->nis_callback(&c_event, NULL);
		}
	}
}