#if !defined (NETWORK_COMMON_LIBRARY_20120803_1)
#define NETWORK_COMMON_LIBRARY_20120803_1

#include "os.h"

//////////////////////////////////////////////////// 套接字对象(so, socket object) 相关接口/////////////////////////////////////////////////////////////////////////////
enum proto_type_t {
	kProto_Unknown = -1,
	kProto_IP,
	kProto_UDP,
	kProto_TCP,
	kProto_PIPE,
	kProto_MaximumId
};

extern
int so_init( enum proto_type_t proto_type, int th_cnt );
extern
void so_uninit( enum proto_type_t ProtoType );
extern
SOCKET so_create( int type, int protocol );
extern
int so_asio_count();
extern
int so_bind( SOCKET s, uint32_t ipv4, uint16_t Port);
extern
void so_dispatch_io_event( OVERLAPPED *o, int size_for_translation );

#endif
