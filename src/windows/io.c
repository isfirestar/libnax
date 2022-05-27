#include "network.h"
#include "io.h"
#include "mxx.h"
#include "ifos.h"
#include "ncb.h"
#include "atom.h"
#include "mthread.h"

#define IOCP_INVALID_SIZE_TRANSFER			(0xFFFFFFFF)
#define IOCP_INVALID_COMPLETION_KEY			((ULONG_PTR)(~0))
#define IOCP_INVALID_OVERLAPPED_PTR			((OVERLAPPED *)0)

struct epoll_object {
	HANDLE epfd;
	boolean_t actived;
	HANDLE thread;
	DWORD tid;
	int load; /* load of current thread */
};

struct epoll_object_manager {
	struct epoll_object *epos;
	int divisions;		/* count of epoll thread */
};

static struct epoll_object_manager epmgr;

static int iocp_complete_routine()
{
	return 0;
}

static DWORD WINAPI __iorun(LPVOID p)
{
	uint32_t bytes_transfered;
	ULONG_PTR completion_key;
	LPOVERLAPPED ovlp = NULL;
	BOOL successful;
	struct epoll_object *epos;

	epos = (struct epoll_object *)p;

	mxx_call_ecr("epfd:%d LWP:%u startup.", epos->epfd, ifos_gettid());

	while ( TRUE ) {
		ovlp = NULL;
		successful = GetQueuedCompletionStatus(epos->epfd, &bytes_transfered, &completion_key, &ovlp, INFINITE);

		if ( !successful ) {
			if ( ovlp ) {
				/*
				If *lpOverlapped is not NULL and the function dequeues a completion packet for a failed I/O operation from the completion port,
				the function stores information about the failed operation in the variables pointed to by lpNumberOfBytes, lpCompletionKey, and lpOverlapped.
				*/
				so_dispatch_io_event( ovlp, bytes_transfered );
			} else {
				/*
				If *lpOverlapped is NULL, the function did not dequeue a completion packet from the completion port
				In this case,
				the function does not store information in the variables pointed to by the lpNumberOfBytes and lpCompletionKey parameters, and their values are indeterminate.
				one time IO error does not represent an thread error, the IO thread not need to quit
				*/
				continue;
			}
		} else {
			/* ask IO thread exit initiative */
			if ( IOCP_INVALID_SIZE_TRANSFER == bytes_transfered ) {
				break;
			}
			so_dispatch_io_event( ovlp, bytes_transfered );
		}
	}

	mxx_call_ecr("epfd:%d LWP:%u terminated.", epos->epfd, ifos_gettid());
	return 0L;
}

static void *__epoll_proc(void *p)
{
	__iorun(p);
	return NULL;
}

int __ioinit()
{
	int i;

	epmgr.divisions = ifos_getnprocs();
	if (NULL == (epmgr.epos = (struct epoll_object *)malloc(sizeof(struct epoll_object) * epmgr.divisions))) {
		return -1;
	}

	for (i = 0; i < epmgr.divisions; i++) {
		epmgr.epos[i].load = 0;
		epmgr.epos[i].epfd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, (ULONG_PTR)&iocp_complete_routine, 1);
		if (epmgr.epos[i].epfd < 0) {
			mxx_call_ecr("file descriptor creat failed. error:%u", GetLastError());
			epmgr.epos[i].actived = NO;
			continue;
		}

		/* active field as a judge of operational effectiveness, as well as a control symbol of operation  */
		epmgr.epos[i].actived = YES;
		epmgr.epos[i].thread = CreateThread(NULL, 0, &__iorun, &epmgr.epos[i], 0, &epmgr.epos[i].tid);
		if (!epmgr.epos[i].thread) {
			mxx_call_ecr("io thread create failed. error:%u", GetLastError());
			epmgr.epos[i].actived = NO;
		}
	}

	return 0;
}

posix__atomic_initial_declare_variable(__inited__);

static lwp_once_t once = LWP_ONCE_INIT;
int ioinit()
{
	lwp_once(&once, &__ioinit);
	return 0;
}

int ioatth(void *ncbptr)
{
	ncb_t *ncb;
	struct epoll_object *epos;
	HANDLE bind_iocp;

	lwp_once(&once, &__ioinit);

	ncb = (ncb_t *)ncbptr;
	if (!ncb) {
		return -EINVAL;
	}

	epos = &epmgr.epos[ncb->hld % epmgr.divisions];
	bind_iocp = CreateIoCompletionPort((HANDLE)ncb->sockfd, epos->epfd, (ULONG_PTR)NULL, 0);
	if ((bind_iocp) && (bind_iocp == epos->epfd)) {
		mxx_call_ecr("success associate sockfd:%d with epfd:%d, link:%I64d", ncb->sockfd, epos->epfd, ncb->hld);
		return 0;
	}

	mxx_call_ecr("link:%I64u syscall CreateIoCompletionPort failed, error code=%u", ncb->hld, GetLastError());
	return -1;
}

void ioclose(void *ncbptr)
{
	ncb_t *ncb;
	SOCKET tmpfd;

	ncb = (ncb_t *)ncbptr;
	if (ncb) {
		tmpfd = ( (sizeof(SOCKET) == sizeof(__int64)) ? InterlockedExchange64((volatile LONG64 *)&ncb->sockfd, INVALID_SOCKET) :
			InterlockedExchange((volatile LONG *)&ncb->sockfd, INVALID_SOCKET) );
		if (INVALID_SOCKET != tmpfd) {
			shutdown(tmpfd, SD_BOTH);

			/*
			The closesocket function closes a socket.
			Use it to release the socket descriptor passed in the @s parameter.
			Note that the socket descriptor passed in the s parameter may immediately be reused by the system as soon as closesocket function is issued.
			As a result, it is not reliable to expect further references to the socket descriptor passed in the s parameter to fail with the error WSAENOTSOCK.
			A Winsock client must never issue closesocket on s concurrently with another Winsock function call.

			Any pending overlapped send and receive operations ( WSASend/ WSASendTo/ WSARecv/ WSARecvFrom with an overlapped socket)
			issued by any thread in this process are also canceled.
			Any event, completion routine, or completion port action specified for these overlapped operations is performed.
			The pending overlapped operations fail with the error status WSA_OPERATION_ABORTED. */
			closesocket(tmpfd);
		}
	}
}

void iouninit()
{
	int i;
	struct epoll_object *epos;

	lwp_once(&once, &__ioinit);

	if (!epmgr.epos) {
		return;
	}

	for (i = 0; i < epmgr.divisions; i++){
		epos = &epmgr.epos[i];
		if (YES == epmgr.epos[i].actived){
			atom_exchange(&epos->actived, NO);
			PostQueuedCompletionStatus(epos->epfd, IOCP_INVALID_SIZE_TRANSFER, IOCP_INVALID_COMPLETION_KEY, IOCP_INVALID_OVERLAPPED_PTR);
			WaitForSingleObject(epos->thread, INFINITE);
			CloseHandle(epos->thread);
		}

		if (epmgr.epos[i].epfd > 0){
			CloseHandle(epmgr.epos[i].epfd);
			epmgr.epos[i].epfd = INVALID_HANDLE_VALUE;
		}
	}

	free(epmgr.epos);
	epmgr.epos = NULL;
}

void *io_get_pipefd(void *ncbptr)
{
	return epmgr.epos[((ncb_t *)ncbptr)->hld % epmgr.divisions].epfd;
}
