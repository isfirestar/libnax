#include "network.h"
#include "ncb.h"
#include "packet.h"
#include "io.h"
#include "mxx.h"

#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")

#define ETH_P_IP	0x0800

static int __arprefr(objhld_t hld, ncb_t **ncb)
{
	if (hld < 0 || !ncb) {
		return -EINVAL;
	}

	*ncb = objrefr(hld);
	if (NULL != (*ncb)) {
		if ((*ncb)->proto_type == kProto_IP) {
			return 0;
		}

		objdefr(hld);
		*ncb = NULL;
		return -EPROTOTYPE;
	}

	return -ENOENT;
}

static void arp_unload(objhld_t h, void * user_buffer)
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
	if (ncb->ncb_ctx_ && 0 != ncb->ncb_ctx_size_) {
		free(ncb->ncb_ctx_);
	}

	mxx_call_ecr("object:%I64d finalization released", ncb->hld);
	ncb_post_close(ncb);
}

int get_eth_MAC(const char *ip, unsigned char mac[6])
{
	int retval;
	ULONG ulSize;
	PIP_ADAPTER_INFO pInfo = NULL;
	int temp = GetAdaptersInfo(pInfo, &ulSize);//第一处调用，获取缓冲区大小
	pInfo = (PIP_ADAPTER_INFO)malloc(ulSize);
	temp = GetAdaptersInfo(pInfo, &ulSize);

	retval = -1;

	while (pInfo) {
		if (0 == strcmp(pInfo->IpAddressList.IpAddress.String, ip)) {
			memcpy(mac, pInfo->Address, 6);
			retval = 0;
			break;
		}
		pInfo = pInfo->Next;
	}

	return -1;
}

PORTABLEIMPL(HARPLINK) arp_create(arp_io_callback_t callback, const char *source)
{
	SOCKET fd;
	HARPLINK hld;
	ncb_t *ncb;

	fd = so_create(SOCK_RAW, IPPROTO_IP);
	if (fd < 0) {
		mxx_call_ecr("fatal error occurred syscall socket(2), error:%d", errno);
		return -1;
	}

	hld = objallo(sizeof (ncb_t), NULL, &arp_unload, NULL, 0);
	if (hld < 0) {
		mxx_call_ecr("insufficient resource for allocate inner object");
		closesocket(fd);
		return -1;
	}
	ncb = (ncb_t *)objrefr(hld);
	assert(ncb);

	ncb_init(ncb, kProto_IP);
	ncb->nis_callback = callback;
	ncb->sockfd = fd;
	ncb->hld = hld;
	/*ncb->local_addr.sin_addr.S_un.S_addr = inet_addr(source);*/
	inet_pton(AF_INET, source, &ncb->local_addr.sin_addr.S_un.S_addr);
	get_eth_MAC(source, ncb->source_mac_);

	objdefr(hld);
	return hld;
}

PORTABLEIMPL(void) arp_destroy(HARPLINK link)
{
	ncb_t *ncb;

	/* it should be the last reference operation of this object no matter how many ref-count now. */
	ncb = objreff(link);
	if (ncb) {
		mxx_call_ecr("link:%lld order to destroy", ncb->hld);
		ioclose(ncb);
		objdefr(link);
	}
}

PORTABLEIMPL(int) arp_nrequest(HARPLINK link, uint32_t target)
{
	int retval;
	ncb_t *ncb;
	ULONG PhyAddrLen;
	BYTE MacAddr[8];
	arp_data_t c_data;
	nis_event_t c_event;

	retval = __arprefr(link, &ncb);
	if (retval < 0) {
		return retval;
	}

	PhyAddrLen = 8;
	retval = SendARP(target, ncb->local_addr.sin_addr.S_un.S_addr, MacAddr, &PhyAddrLen);
	if (NO_ERROR == (DWORD)retval) {
		c_event.Ln.Udp.Link = ncb->hld;
		c_event.Event = EVT_RECEIVEDATA;

		/* keep little-endian, keep compatibility */
		c_data.e.Packet.Arp_Hardware_Type = 1;
		c_data.e.Packet.Arp_Protocol_Type = ETH_P_IP;
		c_data.e.Packet.Arp_Hardware_Size = 6;
		c_data.e.Packet.Arp_Protocol_Size = 4;
		c_data.e.Packet.Arp_Op_Code = ntohs(2);
		memcpy(c_data.e.Packet.Arp_Sender_Mac, MacAddr, 6);
		c_data.e.Packet.Arp_Sender_Ip = ntohl(target); 
		memcpy(c_data.e.Packet.Arp_Target_Mac, ncb->source_mac_, sizeof(ncb->source_mac_));
		c_data.e.Packet.Arp_Target_Ip = ntohl(ncb->local_addr.sin_addr.S_un.S_addr);

		/* callback to arp reply */
		ncb->nis_callback(&c_event, &c_data);
	}

	objdefr(link);
	return posix__makeerror(retval);
}

PORTABLEIMPL(int) arp_request(HARPLINK link, const char *target)
{
	/* return arp_nrequest(link, inet_addr(target)); */
	return -1;
}