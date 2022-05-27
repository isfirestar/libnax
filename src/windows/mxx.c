#include "network.h"
#include "ncb.h"
#include "packet.h"
#include "mxx.h"
#include "sstr.h"
#include "atom.h"

#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")

extern int tcp_settst_r(HTCPLINK link, tst_t *tst);
int tcp_gettst_r(HTCPLINK link, tst_t *tst, tst_t *previous);

/*++
	For a connectionless socket (for example, type SOCK_DGRAM),
	the operation performed by WSAConnect is merely to establish a default destination address so that the socket can be used on subsequent connection-oriented send and receive operations
	(send, WSASend, recv, and WSARecv). Any datagrams received from an address other than the destination address specified will be discarded. If the entire name structure is all zeros
	(not just the address parameter of the name structure), then the socket will be disconnected. Then, the default remote address will be indeterminate, so send, WSASend, recv,
	and WSARecv calls will return the error code WSAENOTCONN. However, sendto, WSASendTo, recvfrom, and WSARecvFrom can still be used.
	The default destination can be changed by simply calling WSAConnect again, even if the socket is already connected.
	Any datagrams queued for receipt are discarded if name is different from the previous WSAConnect.
	--*/
PORTABLEIMPL(int) nis_setctx( HLNK lnk, void * ncb_ctx, int ncb_ctx_size )
{
	ncb_t *ncb;
	int retval;
	char *ctx;

	ncb = objrefr( lnk );
	if ( !ncb ) {
		mxx_call_ecr("reference NCB object failed,link=%I64d", lnk);
		return -1;
	}

	do {
		retval = 0;

		/* clear context when using NULL pointer paramter */
		if ( !ncb_ctx || 0 == ncb_ctx_size ) {
			if (ncb->ncb_ctx_) {
				free(ncb->ncb_ctx_);
			}
			ncb->ncb_ctx_size_ = 0;
			break;
		}

		/* convert current context when acquire length not equal to present */
		if ( ncb_ctx_size != ncb->ncb_ctx_size_ ) {
			if (ncb->ncb_ctx_) {
				free(ncb->ncb_ctx_);
			}
			ncb->ncb_ctx_size_ = ncb_ctx_size;
			ctx = ( char * ) malloc( ncb_ctx_size );
			if ( !ctx ) {
				mxx_call_ecr("fail to allocate memory for ncb context, size=%u", ncb_ctx_size);
				retval = -1;
				break;
			}
			ncb->ncb_ctx_ = ctx;
		}
		memcpy( ncb->ncb_ctx_, ncb_ctx, ncb->ncb_ctx_size_ );
	} while ( FALSE );

	objdefr(ncb->hld);
	return retval;
}

PORTABLEIMPL(int) nis_getctx( HLNK lnk, void * user_context, int *user_context_size )
{
	ncb_t * ncb;

	if (!user_context) {
		return -1;
	}

	ncb = objrefr( lnk );
	if ( !ncb ) {
		mxx_call_ecr("reference NCB object failed,link=%I64d", lnk);
		return -1;
	}

	if ( ncb->ncb_ctx_ && 0 != ncb->ncb_ctx_size_ ) memcpy( user_context, ncb->ncb_ctx_, ncb->ncb_ctx_size_ );
	if ( user_context_size ) *user_context_size = ncb->ncb_ctx_size_;
	objdefr(ncb->hld);
	return 0;
}

PORTABLEIMPL(void *) nis_refctx( HLNK lnk, int *user_context_size )
{
	ncb_t * ncb;
	void *ctxptr = NULL;

	ncb = objrefr( lnk );
	if ( ncb ) {
		if ( ncb->ncb_ctx_ && 0 != ncb->ncb_ctx_size_ ) {
			ctxptr = ncb->ncb_ctx_;
		}
		if ( user_context_size ) {
			*user_context_size = ncb->ncb_ctx_size_;
		}
		objdefr(ncb->hld);
	}

	return ctxptr;
}

PORTABLEIMPL(int) nis_ctxsize( HLNK lnk )
{
	ncb_t * ncb;
	long size;

	ncb = objrefr( lnk );
	if ( !ncb ) {
		mxx_call_ecr("reference NCB object failed,link=%I64d", lnk);
		return -1;
	}
	size = ncb->ncb_ctx_size_;
	objdefr(ncb->hld);
	return size;
}

PORTABLEIMPL(int) nis_getver( swnet_version_t  *version )
{
	return -1;
}

PORTABLEIMPL(int) nis_gethost( const char *name, uint32_t *ipv4 )
{
	struct hostent *remote;
    struct in_addr addr;
	/*struct addrinfo hits, *pres;*/

	if (!name || !ipv4) {
		return -1;
	}

	so_init( kProto_Unknown, -1 );

	*ipv4 = 0;

	 if (isalpha(name[0])) {        /* host address is a name */
        remote = gethostbyname(name);
		/*getaddrinfo(name, NULL, &hits, &pres);*/
	 } else {
		 /*addr.s_addr = inet_addr( name );*/
		 inet_pton(AF_INET, name, &addr.S_un.S_addr);
		 if ( INADDR_NONE == addr.s_addr ) {
			 return -1;
		 } else {
			 remote = gethostbyaddr( ( char * ) &addr, 4, AF_INET );
		 }
	 }

    if (!remote) {
		return -1;
    }

	/* IPv4 only */
	if ( AF_INET != remote->h_addrtype ) {
		return -1;
	}

	if ( remote->h_length < sizeof( uint32_t ) ) {
        return -1;
    }

    addr.s_addr = *((uint32_t *) remote->h_addr_list[0]);
    *ipv4 = ntohl(addr.s_addr);
	return 0;
}

PORTABLEIMPL(char *) nis_lgethost(char *name, int cb)
{
	if ( !name || cb <= 0 ) {
		return NULL;
	}

	if ( 0 != gethostname( name, cb ) ) {
		return NULL;
	}
	return name;
}

/* manage ECR and it's calling */
static nis_event_callback_t current_ecr = NULL;

PORTABLEIMPL(nis_event_callback_t) nis_checr(const nis_event_callback_t ecr)
{
	if ( !ecr ) {
		InterlockedExchangePointer( (volatile PVOID *)&current_ecr, NULL );
		return NULL;
	}
	return InterlockedExchangePointer( ( volatile PVOID * )&current_ecr, ecr );
}

void nis_call_ecr( const char *fmt, ... )
{
	nis_event_callback_t ecr = NULL, old;
	va_list ap;
	char logstr[1280];
	int retval;

	if ( !current_ecr ) {
		return;
	}

	va_start( ap, fmt );
	retval = portable_vsprintf( logstr, cchof( logstr ), fmt, ap );
	va_end( ap );
	if ( retval <= 0 ) {
		return;
	}
	logstr[retval] = 0;

	old = InterlockedExchangePointer( ( volatile PVOID * ) &ecr, current_ecr );
	if ( ecr && !old ) {
		ecr( logstr, NULL, 0 );
	}
}

PORTABLEIMPL(int) nis_setmask(HTCPLINK lnk, int mask)
{
	ncb_t *ncb;
	objhld_t hld = (objhld_t)lnk;

	ncb = objrefr(hld);
	if (!ncb) {
		return -EEXIST;
	}

	ncb->attr = mask;

	objdefr(hld);
	return 0;
}

PORTABLEIMPL(int) nis_getmask(HTCPLINK lnk, int *mask)
{
	ncb_t *ncb;
	objhld_t hld = (objhld_t)lnk;

	ncb = objrefr(hld);
	if (!ncb) {
		return -1;
	}

	if (mask) {
		*mask = ncb->attr;
	}

	objdefr(hld);
	return 0;
}

PORTABLEIMPL(int) nis_getifmisc(ifmisc_t *ifv, int *cbifv)
{
	ULONG dwRetVal, outBufLen;
	PIP_ADAPTER_INFO pCurrAddresses, pAddresses;
	int i;
	int cbacquire;

	if (!cbifv) {
		return -EINVAL;
	}

	if (*cbifv > 0 && !ifv) {
		return -EINVAL;
	}

	outBufLen = 0;
	i = 0;
	pAddresses = NULL;

	dwRetVal = GetAdaptersInfo(NULL, &outBufLen);
	while (ERROR_BUFFER_OVERFLOW == dwRetVal && i++ < 10) {
		if (pAddresses) {
			free(pAddresses);
		}
		pAddresses = (PIP_ADAPTER_INFO)malloc(outBufLen);
		if (!pAddresses) {
			return -ENOMEM;
		}
		dwRetVal = GetAdaptersInfo(pAddresses, &outBufLen);
	}

	if (0 != dwRetVal) {
		if (pAddresses) {
			free(pAddresses);
		}
		return posix__makeerror(dwRetVal);
	}

	i = 0;
	pCurrAddresses = pAddresses;
	while (pCurrAddresses) {
		++i;
		pCurrAddresses = pCurrAddresses->Next;
	}

	cbacquire = i * sizeof(ifmisc_t);
	if (!ifv || *cbifv < cbacquire) {
		*cbifv = cbacquire;
		return -EAGAIN;
	}

	i = 0;
	pCurrAddresses = pAddresses;
	while (pCurrAddresses) {
		strncpy_s(ifv[i].eth, sizeof(ifv[i].eth) - 1, pCurrAddresses->Description, sizeof(ifv[i].eth) - 1);
		inet_pton(AF_INET, pCurrAddresses->IpAddressList.IpAddress.String, &ifv[i].inet);
		inet_pton(AF_INET, pCurrAddresses->IpAddressList.IpMask.String, &ifv[i].mask);
		/*
		ifv[i].inet = inet_addr(pCurrAddresses->IpAddressList.IpAddress.String);
		ifv[i].mask = inet_addr(pCurrAddresses->IpAddressList.IpMask.String);*/
		ifv[i].boardcast = 0;
		pCurrAddresses = pCurrAddresses->Next;
		i++;
	}

	if (pAddresses) {
		free(pAddresses);
	}
	return 0;
}


/*
typedef struct _IP_ADAPTER_INFO {
struct _IP_ADAPTER_INFO *Next;
DWORD                   ComboIndex;
char                    AdapterName[MAX_ADAPTER_NAME_LENGTH + 4];
char                    Description[MAX_ADAPTER_DESCRIPTION_LENGTH + 4];
UINT                    AddressLength;
BYTE                    Address[MAX_ADAPTER_ADDRESS_LENGTH];
DWORD                   Index;
UINT                    Type;
UINT                    DhcpEnabled;
PIP_ADDR_STRING         CurrentIpAddress;
IP_ADDR_STRING          IpAddressList;
IP_ADDR_STRING          GatewayList;
IP_ADDR_STRING          DhcpServer;
BOOL                    HaveWins;
IP_ADDR_STRING          PrimaryWinsServer;
IP_ADDR_STRING          SecondaryWinsServer;
time_t                  LeaseObtained;
time_t                  LeaseExpires;
} IP_ADAPTER_INFO, *PIP_ADAPTER_INFO;
*/
PORTABLEIMPL(int) nis_getifmac(char *eth_name, unsigned char *pyhaddr)
{
	IP_ADAPTER_INFO adapter, *adapter_ptr, *cursor;
	ULONG outlen, retval;

	if (!eth_name || !pyhaddr) {
		return -EINVAL;
	}

	outlen = sizeof(adapter);
	adapter_ptr = &adapter;
	cursor = NULL;

	while (!cursor) {
		retval = GetAdaptersInfo(adapter_ptr, &outlen);
		switch (retval)
		{
		case ERROR_SUCCESS:
			cursor = adapter_ptr;
			break;
		case ERROR_BUFFER_OVERFLOW:
			if (adapter_ptr && adapter_ptr != &adapter) {
				free(adapter_ptr);
			}
			adapter_ptr = (IP_ADAPTER_INFO *)malloc(outlen);
			if (!adapter_ptr) {
				return -ENOMEM;
			}
			break;
		default:
			return retval * -1;
		}
	}

	while (cursor) {
		if (0 == strcmp(eth_name, cursor->Description)) {
			memcpy(pyhaddr, cursor->Address, 6);
			break;
		}
		cursor = cursor->Next;
	}

	if (adapter_ptr && adapter_ptr != &adapter) {
		free(adapter_ptr);
	}

	return cursor ? 0 : -ENOENT;
}

PORTABLEIMPL(int) nis_cntl(objhld_t link, int cmd, ...)
{
	ncb_t *ncb;
	int retval;
	va_list ap;
	void *context;

	ncb = objrefr(link);
	if (!ncb) {
		return -ENOENT;
	}

	retval = 0;

	va_start(ap, cmd);
	switch (cmd) {
	case NI_SETATTR:
		InterlockedExchange((volatile LONG *)&ncb->attr, va_arg(ap, int));
		break;
	case NI_GETATTR:
		retval = atom_get(&ncb->attr);
		break;
	case NI_SETCTX:
		atom_set(&context, va_arg(ap, void*));
		ncb->prcontext = InterlockedExchangePointer(&ncb->context, context);
		break;
	case NI_GETCTX:
		ncb->prcontext = InterlockedExchangePointer(&context, ncb->context);
		*(va_arg(ap, void**)) = context;
		break;
	case NI_SETTST:
		retval = tcp_settst_r(link, va_arg(ap, void *));
		break;
	case NI_GETTST:
		retval = tcp_gettst_r(link, va_arg(ap, void *), NULL);
		break;
	default:
		return -EINVAL;
	}
	va_end(ap);

	objdefr(link);
	return retval;

}
