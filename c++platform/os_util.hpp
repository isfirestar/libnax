#if !defined OS_UTIL_HEADER_02160616
#define OS_UTIL_HEADER_02160616

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <cstdarg>
#include <ctime>
#include <cstdint>
#include <atomic>

#include <string>

#if _WIN32

#include <stddef.h>
#include <Windows.h>

#else

#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <pthread.h>
#include <syscall.h>
#include <dirent.h>
#include <semaphore.h>

#include <signal.h>

#include <sys/time.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <linux/unistd.h>

#if !defined DUMMYSTRUCTNAME
#define DUMMYSTRUCTNAME
#endif

typedef union _LARGE_INTEGER {

	struct {
		uint32_t LowPart;
		int32_t HighPart;
	} DUMMYSTRUCTNAME;

	struct {
		uint32_t LowPart;
		int32_t HighPart;
	} u;
	int64_t QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {

	struct {
		uint32_t LowPart;
		uint32_t HighPart;
	} DUMMYSTRUCTNAME;

	struct {
		uint32_t LowPart;
		uint32_t HighPart;
	} u;
	int64_t QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;

#endif

#include "icom/compiler.h"
#include "icom/posix_ifos.h"

namespace nsp {
	namespace os {


		// 获取分页信息
		uint32_t get_pagesize();

		// 以下路径函数不带最后的 DIR_SYMBOL
		template<class T>
		std::basic_string<T> get_module_fullpath();
		template<class T>
		std::basic_string<T> get_module_directory();
		template<class T>
		std::basic_string<T> get_module_filename();

#define INVAILD_FILESIZE        ((uint64_t)(~0))
        template<class T>
        uint64_t get_filesize(const std::basic_string<T> &path);
        uint64_t fget_filesize(const file_descriptor_t fd);
#if _WIN32
		template<class T>
		std::basic_string<T> get_sysdir();
		template<class T>
		std::basic_string<T> get_appdata_dir();
#endif
		template <class T>
		std::basic_string<T> get_tmpdir();
		template <class T>
		int rmfile( const std::basic_string<T> &target );
		template<class T>
		int mkdir( const std::basic_string<T> &dir );
		template <class T>
		int mkdir_s( const std::basic_string<T> &dir );
		template<class T>
		int is_dir( const std::basic_string<T> &file ); // 返回>0 则是目录, 0则是文件， 负数则是错误
		template<class T>
		int rmdir_s( const std::basic_string<T> &dir ); // dir 传入不带最后的'/'

		long gettid();
		int getpid();

		int getnprocs();

		int getsysmem( uint64_t &total, uint64_t &free, uint64_t &total_swap, uint64_t &free_swap);

		class waitable_handle {
			void *posix_waiter_ = nullptr;
		public:
			waitable_handle( int sync = 1 );
			~waitable_handle();
			/* @timeo 指定等待方式，其中:
			 *  <0 表示无限等待
			 *  0  表示测试信号
			 *  >0  表示带超时等待
			 *
			 * 返回定义:
			 * 如果 tsc <= 0 则有:
			 * 0: 事件触发
			 * -1: 系统调用失败
			 *
			 * 如果 tsc > 0 则有：
			 * 0: 事件触发
			 * ETIMEOUT: 等待超时
			 * -1: 系统调用失败
			 */
			int wait( uint32_t timeo = 0xFFFFFFFF );
			void sig();
			void reset();
		};

		void pshang();

		template<class T>
		void attempt_syslog( const std::basic_string<T> &msg, uint32_t err );


		// 获取系统启动到目前时间节点流逝的tick
		// gettick			返回应用层滴答精度， 精度单位为  ms,
		// clock_gettime	返回内核级滴答精度， 单位为     100ns
		// clock_epoch		返回EPOCH时间， 单位为 100ns
		uint64_t gettick();
		uint64_t clock_gettime();
		uint64_t clock_epoch();
		uint64_t clock_monotonic();

		// 动态库加载例程 gcc -ldl
		void *dlopen( const char *file );
		void* dlsym( void* handle, const char* symbol );
		int dlclose( void *handle );
	}
}

#if !defined PAGE_SIZE
#define PAGE_SIZE nsp::os::get_pagesize()
#endif

#endif
