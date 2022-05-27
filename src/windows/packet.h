#if !defined (NETWORK_BASICE_PACKET_HEADER_20120824_1)
#define NETWORK_BASICE_PACKET_HEADER_20120824_1

#include "object.h"

#if !defined USES_LOCAL_ROUTE_TABLE
#define USES_LOCAL_ROUTE_TABLE									(1)		// 开关, 经过本地路由表
#endif

#define MAXIMUM_TRANSMIT_BYTES_PER_GROUP						(0x7FFFFFFE)				// 单次组发送最大操作字节数
#define MAXIMUM_GPM_ITEM_COUNT									(32)						// GPM方式最大单包个数

enum pkt_type_t {
	kSend = 1,
	kRecv,
	kSyn,
	kConnect,
	kPipe,
	kUnknown,
};

enum page_style_t {
	kNoAccess = 0,
	kVirtualHeap,
	kNonPagedPool,
};

enum page_phase_t
{
	kPagePhase_InUse = 0,
	kPagePhase_ReleaseWait,
	kPagePhase_CanbeFree,
};

#define IOR_SHUTDOWN	(-1)
#define IOR_NORMAL		(0)
#define IOR_CAN_FREE	(1)
#define IOR_SUCCESS(ior)	((ior) >= 0)

typedef struct _NCC_PACKET {
	OVERLAPPED overlapped_;
	enum pkt_type_t type_;		// 区别收包发包
	enum proto_type_t proto_type;// 协议类型
	enum page_style_t page_style_;				// 缓冲区页属性
	uint32_t flag_;				// 保存异步过程中的flag
	int from_length_;				// 保存fromlen
	objhld_t link;					// 控制块的句柄
	objhld_t accepted_link;			// 用于 tcp accept 的对端对象句柄
	struct sockaddr_in remote_addr;	/* 对端地址结构 */
	struct sockaddr_in local_addr;	/* 本地地址结构 */
	struct list_head pkt_lst_entry_;	// 对 TCP 发送对象的 ncb_t::tcp_waitting_list_head_ 钩链(每个包都是一个节点)
	int size_for_req_;				// 投递请求前的， 缓冲区长度
	int size_for_translation_;		// 交换字节数
	int size_completion_;			// 投递给系统用于接收完成长度的字段， 区别于 size_for_translation_, 此字段并不建议使用
	int analyzed_offset_;			// 保存 TCP 解包过程中的原始地址偏移解析
	void *ori_buffer_;				// 原始数据指针， 因为irp_字段可能因为投递给系统的指针移动而变化， 因此原始地址需要记录
	PTRANSMIT_PACKETS_ELEMENT grp_packets_; // 当使用 grp 方式进行发送操作， 则缓冲区位于此数组内, 但是packet模块不负责这些具体内存的管理工作
	int grp_packets_cnt_;
	void *irp_;			// 用户数据指针, 实际的IRP内存地址
	enum page_phase_t iopp_;  /* using this field to control the packet free timepoint, 
								it MUST guarantee that IOCP asynchronous invocation completed before packet buffer free  */
	WSABUF wsb[1];
}packet_t;

int allocate_packet( objhld_t h, enum proto_type_t proto_type, enum pkt_type_t type, int cbSize, enum page_style_t page_style, packet_t **output_packet );
void freepkt( packet_t * packet );

int asio_udp_recv( packet_t * packet );
int syio_udp_send( packet_t * packet, const char *r_ipstr, uint16_t r_port );

int syio_v_connect( ncb_t * ncb, const struct sockaddr_in *r_addr );
int syio_v_disconnect( ncb_t * ncb );
int syio_grp_send( packet_t * packet );

int asio_tcp_send( packet_t * packet );
int asio_tcp_accept( packet_t * packet );
int asio_tcp_recv( packet_t * packet );
int asio_tcp_connect(packet_t *packet);

#endif
