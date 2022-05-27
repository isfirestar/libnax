#if !defined (NETWORK_CONTROL_BLOCK_HEADER_20120824_1)
#define NETWORK_CONTROL_BLOCK_HEADER_20120824_1

#include "object.h"
#include "clist.h"
#include "nis.h"

#include <ws2ipdef.h>

typedef int( *object_entry_t )( objhld_t h, void * user_buffer, void * ncb_ctx );
typedef void( *object_unload_t )( objhld_t h, void * user_buffer );

typedef struct _NCC_NETWORK_BASIC_CONTROL_BLCOK
{
	objhld_t					hld;
	SOCKET						sockfd;
	enum proto_type_t			proto_type;

	struct sockaddr_in			local_addr;				// 本地地址信息记录
	struct sockaddr_in			remote_addr;				// 对端地址信息记录
	nis_callback_t				nis_callback;

	int							flag_;						// 标记， 目前TCP未使用， UDP可以指定为 UDP_FLAG_BROADCAST
	int							connected_;					// 是否已经建立连接
	IP_MREQ					   *mreq;						// UDP 多播设定
    struct timeval				rcvtimeo;					// 接收超时
    struct timeval				sndtimeo;					// 发送超时

	/* IP头的 tos 项
     * Differentiated Services Field: Dirrerentiated Services Codepoint/Explicit Congestion Not fication 指定TOS段
     *  */
    int iptos;

	int attr;

	void *context;
	void *prcontext;

	struct {												// 大包解读(大于64KB但是不足50MB的TCP数据包)
		char*					lb_data_;					// large block data
		int						lb_cpy_offset_;				// 当前已经赋值的大包数据段偏移
		int						lb_length_;					// 当前大包缓冲区长度
	};
	struct {
		struct list_head		tcp_sender_cache_head_;		/* TCP sender control and traffic manager */
		int						tcp_sender_list_size_;	/* the count of packet pending in @tcp_sender_cache_head_ */
		CRITICAL_SECTION		tcp_sender_locker_;			/* lock element: @tcp_sender_cache_head_ and @tcp_sender_list_size_*/
		int						tcp_write_pending_;	/* the total pending count on this link */
		tst_t					tcp_tst_;					/* protocol template on this link */
		int						mss;						/* MSS of tcp link */
	};
	struct {
		void *					ncb_ctx_;					// 用户上下文
		int						ncb_ctx_size_;				// 用户上下文长度
	};

	unsigned char source_mac_[6];

}ncb_t;

void ncb_init( ncb_t * ncb, enum proto_type_t proto_type );

#define ncb_set_callback(ncb, fn)		( ncb->nis_callback = ( nis_callback_t )( void * )fn )
#define ncb_lb_marked(ncb)	((ncb) ? ((NULL != ncb->lb_data_) && (ncb->lb_length_ > 0)) : (FALSE))

extern
int ncb_mark_lb( ncb_t *ncb, int Size, int CurrentSize, void * SourceData );
extern
void ncb_unmark_lb( ncb_t *ncb );


extern
int ncb_set_rcvtimeo(ncb_t *ncb, struct timeval *timeo);
extern
int ncb_get_rcvtimeo(ncb_t *ncb);
extern
int ncb_set_sndtimeo(ncb_t *ncb, struct timeval *timeo);
extern
int ncb_get_sndtimeo(ncb_t *ncb);

extern
int ncb_set_iptos(ncb_t *ncb, int tos);
extern
int ncb_get_iptos(ncb_t *ncb);

extern
int ncb_set_window_size(ncb_t *ncb, int dir, int size);
extern
int ncb_get_window_size(ncb_t *ncb, int dir, int *size);

extern
int ncb_set_linger(ncb_t *ncb, int onoff, int lin);
extern
int ncb_get_linger(ncb_t *ncb, int *onoff, int *lin);

extern
int ncb_set_keepalive(ncb_t *ncb, int enable);
extern
int ncb_get_keepalive(ncb_t *ncb, int *enabled);

extern
void ncb_post_preclose(const ncb_t *ncb);
extern
void ncb_post_close(const ncb_t *ncb);
extern
void ncb_post_recvdata(const ncb_t *ncb, int cb, const unsigned char *data);
extern
void ncb_post_pipedata(const ncb_t *ncb, int cb, const unsigned char *data);
extern
void ncb_post_accepted(const ncb_t *ncb, HTCPLINK link);
extern
void ncb_post_connected(const ncb_t *ncb);


#endif
