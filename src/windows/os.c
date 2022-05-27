#include "os.h"
#include "mxx.h"

void *
os_allocate_block(
HANDLE handleProcess,
uint32_t blockSizeCb,
uint32_t Protect
)
/*
*	块内存的申请和释放
*	Size[_In_]		申请内存块大小
*
*	RET				返回保留并提交的虚拟内存地址指针
*      					函数操作失败返回NULL
*
*	调用要求申请内存长度为 PAGE_SIZE 对齐
*	函数适用于较大块的内存申请， 该块内存期望长度至少应该是 PAGE_SIZE 大小
*  如果期望长度和 PAGE_SIZE 不对齐， 可以采取对齐申请， 然后截留的措施
*	小块内存使用这个函数调用可能会影响性能
*	使用这个函数申请的内存块， 应该显式调用 PubFreeMemoryBlock 进行释放
*/
{
	void *		MemoryBlock;

	//
	// 必须保证申请长度是页对齐
	//
	if ( !os_is_page_aligned( blockSizeCb ) ) {
		return NULL;
	}

	if ( !handleProcess || 0 == blockSizeCb ) {
		return NULL;
	}

	__try {
		MemoryBlock = VirtualAllocEx(
			handleProcess,
			NULL,
			blockSizeCb,
			MEM_RESERVE | MEM_COMMIT,
			Protect
			);
		if ( NULL == MemoryBlock ) {
			mxx_call_ecr("syscall VirtualAllocEx failed, error:%u", GetLastError());
		}
	} __except ( EXCEPTION_EXECUTE_HANDLER ) {
		MemoryBlock = NULL;
		mxx_call_ecr("failed to allocate virtual memory block, process handle:0x%08X, size:%u, exception code:0%08X",
			handleProcess, blockSizeCb, GetExceptionCode() );
	}

	return MemoryBlock;
}

VOID os_free_memory_block( void * MemoryBlock ) 
{
	if ( NULL != MemoryBlock ) {
		__try {
			VirtualFree( MemoryBlock, 0, MEM_RELEASE );
		} __except ( EXCEPTION_EXECUTE_HANDLER ) {
			;
		}
	}
}

int os_unlock_and_free_virtual_pages( void * MemoryBlock, uint32_t Size)
{
	if ( NULL != MemoryBlock ) {
		VirtualUnlock( MemoryBlock, Size );
		os_free_memory_block( MemoryBlock );
		return TRUE;
	}

	return FALSE;
}

/*++
申请或者将指定内存块提升为非分也缓冲
pMemoryBlock[_In_]		需要提升为非分页池的虚拟内存地址, 如果参数为NULL, 则申请一片新的地址
Size[_In_]					需要提升或申请的内存块大小， 必须页对齐
RET						操作成功返回提升或申请后提升的缓冲区指针， 失败返回 NULL
--*/
void *os_lock_virtual_pages( void * MemoryBlock, uint32_t Size ) 
{
	SIZE_T MinimumWorkingSetSize;
	SIZE_T MaximumWorkingSetSize;
	int Successful;
	HANDLE handleProcess;
	uint32_t errorCode;
	int LoopFlag;

	handleProcess = INVALID_HANDLE_VALUE;
	LoopFlag = TRUE;

	//
	// 如果参数使用空块， 则是希望函数创建一个新块
	// 如果参数不使用空块， 则应该判断传入块的大小是否页对齐
	//
	if ( NULL == MemoryBlock ) {
		if ( NULL == ( MemoryBlock = os_allocate_block(GetCurrentProcess(), Size, PAGE_READWRITE) ) ) {
			return NULL;
		}
	} else {
		if ( !os_is_page_aligned( Size ) ) {
			return NULL;
		}
	}

	do {
		Successful = VirtualLock( MemoryBlock, Size );
		if ( Successful ) {
			break;
		}

		//
		// 如果是工作集配额不足导致的锁定虚拟内存到物理页失败
		// 此时应该调整进程的工作集空间设置
		// 如果是其他错误， 直接退出
		//
		errorCode = GetLastError();
		if ( ERROR_WORKING_SET_QUOTA != errorCode ) {
			//
			// 如果是无权限失败， 可能是 pMemoryBlock 由外部传入, 但是是保留的虚拟内存， 而且该片内存没有虚拟提交
			// 这里只需要重新提交该片虚拟内存， 然后重新执行即可
			// 为了保证不会发生死循环， 这样的操作只执行一次
			//
			if ( ERROR_NOACCESS == errorCode && LoopFlag ) {
				__try {
					MemoryBlock = VirtualAlloc(
						MemoryBlock,
						Size,
						MEM_COMMIT,
						PAGE_READWRITE
						);
					if ( NULL == MemoryBlock ) {
						mxx_call_ecr("syscall VirtualAlloc failed, code:0x%08X", GetLastError());
						break;
					}
				} __except ( EXCEPTION_EXECUTE_HANDLER ) {
					break;
				}

				LoopFlag = FALSE;
				continue;
			}

			break;
		}

		//
		// 如果已经调整过工作集空间， 仍然无法将内存锁定到非分也缓冲池
		// 此时可以直接失败
		// 函数使用当前进程句柄是否为有效， 作为判断单次调整工作集的条件
		//
		if ( INVALID_HANDLE_VALUE == handleProcess ) {

			//
			// 以 :
			// PROCESS_QUERY_INFORMATION(查询工作集和内存信息)
			// PROCESS_SET_QUOTA (设置工作集配额)
			// 的方式打开当前进程句柄
			//
			handleProcess = OpenProcess(
				PROCESS_QUERY_INFORMATION | PROCESS_SET_QUOTA | PROCESS_VM_READ,
				FALSE,
				GetCurrentProcessId()
				);
			if ( INVALID_HANDLE_VALUE == handleProcess ) {
				mxx_call_ecr("syscall OpenProcess failed, code:0x%08X", GetLastError());
				break;
			}
		}

		//
		// 这里首先需要查看当前进程的工作集大小， 用于确定是否可行
		//
		Successful = GetProcessWorkingSetSize(
			handleProcess,
			&MinimumWorkingSetSize,
			&MaximumWorkingSetSize
			);
		if ( !Successful ) {
			mxx_call_ecr("syscall GetProcessWorkingSetSize failed, code:0x%08X", GetLastError());
			break;
		}

		//
		// 调整工作集大小， 要求设置工作集大小为：
		// 本次申请块大小和现有工作集大小之和
		//
		MinimumWorkingSetSize += Size;
		MaximumWorkingSetSize += Size;

		Successful = SetProcessWorkingSetSize(
			handleProcess,
			MinimumWorkingSetSize,
			MaximumWorkingSetSize
			);
		if ( !Successful ) {
			mxx_call_ecr("syscall SetProcessWorkingSetSize failed, code:0x%08X", GetLastError());
			break;
		}

	} while ( TRUE );

	if ( INVALID_HANDLE_VALUE != handleProcess ) {
		CloseHandle( handleProcess );
		handleProcess = INVALID_HANDLE_VALUE;
	}

	if ( !Successful ) {
		__try {
			VirtualFree( MemoryBlock, 0, MEM_RELEASE );
		} __except ( EXCEPTION_EXECUTE_HANDLER ) {
			;
		}

		MemoryBlock = NULL;
	}

	return MemoryBlock;
}
