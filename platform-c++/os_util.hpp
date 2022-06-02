#if !defined OS_UTIL_HEADER_02160616
#define OS_UTIL_HEADER_02160616

#include "compiler.h"
#include "ifos.h"
#include "threading.h"

#include <atomic>
#include <string>

#if !_WIN32

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
		nsp_status_t rmfile( const std::basic_string<T> &target );
		template<class T>
		nsp_status_t mkdir( const std::basic_string<T> &dir );
		template <class T>
		nsp_status_t mkdir_s( const std::basic_string<T> &dir );
		template<class T>
		bool is_dir( const std::basic_string<T> &file );
		template<class T>
		nsp_status_t rmdir_s( const std::basic_string<T> &dir ); // dir 传入不带最后的'/'

		pid_t gettid();
		pid_t getpid();

		int getnprocs();

		nsp_status_t getsysmem( uint64_t &total, uint64_t &free, uint64_t &total_swap, uint64_t &free_swap);

		class waitable_handle {
			lwp_event_t *posix_waiter_ = nullptr;
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
			nsp_status_t wait( uint32_t timeo = 0xFFFFFFFF );
			void sig();
			void reset();
		};

		void pshang();

		// template<class T>
		// void attempt_syslog( const std::basic_string<T> &msg, uint32_t err );


		// 获取系统启动到目前时间节点流逝的tick
		// gettick			返回应用层滴答精度， 精度单位为  ms,
		// clock_gettime	返回内核级滴答精度， 单位为     100ns
		// clock_epoch		返回EPOCH时间， 单位为 100ns
		uint64_t gettick();
		uint64_t clock_realtime();
		uint64_t clock_epoch();
		uint64_t clock_monotonic();

		// 动态库加载例程 gcc -ldl
		void *dlopen( const char *file );
		void* dlsym( void* handle, const char* symbol );
		void dlclose( void *handle );
	}
}

#if !defined PAGE_SIZE
#define PAGE_SIZE nsp::os::get_pagesize()
#endif

#endif
