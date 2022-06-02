#include "os_util.hpp"

#include <new>

#include "abuff.h"
#include "clock.h"

#if _WIN32
#include <Windows.h>
#pragma comment(lib, "Advapi32.lib")
#include <Shlobj.h>
#pragma comment( lib, "shell32.lib")
#endif

#include <stack>

namespace nsp {
    namespace os {

        uint32_t get_pagesize()
        {
            return ::ifos_getpagesize();
        }

        template<>
        std::basic_string<char> get_module_fullpath<char>()
        {
            ifos_path_buffer_t holder;
            nsp_status_t status;
            status = ::ifos_fullpath_current(&holder);
            return NSP_SUCCESS(status) ? holder.u.st : "";
        }

        template<>
        std::basic_string<char> get_module_directory<char>()
        {
            ifos_path_buffer_t holder;
            nsp_status_t status;
            status = ifos_getpedir(&holder);
            return NSP_SUCCESS(status) ? holder.u.st : "";
        }

        template<>
        std::basic_string<char> get_module_filename<char>()
        {
            ifos_path_buffer_t holder;
            nsp_status_t status;
            status = ::ifos_getpename(&holder);
            return NSP_SUCCESS(status) ? holder.u.st : "";
        }

        template<>
        uint64_t get_filesize<char>(const std::basic_string<char> &path)
        {
            return ::ifos_file_getsize(path.data());
        }

        uint64_t fget_filesize(const file_descriptor_t fd)
        {
            return ::ifos_file_fgetsize(fd);
        }
#if _WIN32

        template<>
        std::basic_string<char> get_sysdir<char>()
        {
            char buffer[MAX_PATH];
            if (!::GetSystemDirectoryA(buffer, cchof(buffer))) {
                return "";
            }
            return buffer;
        }

        template<>
        std::basic_string<wchar_t> get_sysdir()
        {
            wchar_t buffer[MAX_PATH];
            if (!::GetSystemDirectoryW(buffer, cchof(buffer))) {
                return L"";
            }
            return buffer;
        }

        template<>
        std::basic_string<char> get_appdata_dir()
        {
            char document[MAX_PATH];
            char dir[MAX_PATH];

            LPITEMIDLIST idlst = NULL;
            HRESULT res = SHGetFolderLocation(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, &idlst);
            if (FAILED(res) || !idlst) {
                return "";
            }

            if (!SHGetPathFromIDListA(idlst, document)) {
                return "";
            }
            GetShortPathNameA(document, dir, cchof(dir));
            return dir;
        }

        template<>
        std::basic_string<wchar_t> get_appdata_dir()
        {
            wchar_t document[MAX_PATH];
            wchar_t dir[MAX_PATH];

            LPITEMIDLIST idlst = NULL;
            HRESULT res = SHGetFolderLocation(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, &idlst);
            if (FAILED(res) || !idlst) {
                return L"";
            }

            if (!SHGetPathFromIDListW(idlst, document)) {
                return L"";
            }
            GetShortPathNameW(document, dir, cchof(dir));
            return dir;
        }
#endif

        template <>
        std::basic_string<char> get_tmpdir<char>()
        {
            ifos_path_buffer_t holder;
            nsp_status_t status;
            status = ifos_gettmpdir(&holder);
            return NSP_SUCCESS(status) ? holder.u.st : "";
        }

        template<>
        nsp_status_t rmfile<char>(const std::basic_string<char> &target)
        {
            return ::ifos_rm(target.c_str());
        }

        template<>
        nsp_status_t mkdir<char>(const std::basic_string<char> &dir)
        {
            return ::ifos_mkdir(dir.c_str());
        }

        template <>
        nsp_status_t mkdir_s<char>(const std::basic_string<char> &dir)
        {
            return ::ifos_pmkdir(dir.c_str());
        }

        template<>
        bool is_dir<char>(const std::basic_string<char> &file)
        {
            return ::ifos_isdir(file.c_str());
        }

        template<>
        nsp_status_t rmdir_s<char>(const std::basic_string<char> &dir)
        {
            return ::ifos_rm(dir.c_str());
        }

        pid_t gettid()
        {
            return ::ifos_gettid();
        }

        pid_t getpid()
        {
            return ::ifos_getpid();
        }

        int getnprocs() {
            return ::ifos_getnprocs();
        }

        nsp_status_t getsysmem(uint64_t &total, uint64_t &free, uint64_t &total_swap, uint64_t &free_swap)
        {
            sys_memory_t sysmem;
            nsp_status_t staus = ::ifos_getsysmem(&sysmem);
            if (NSP_SUCCESS(staus)) {
                total = sysmem.totalram;
                free = sysmem.freeram;
                total_swap = sysmem.totalswap;
                free_swap = sysmem.freeswap;
            }

            return staus;
        }

        waitable_handle::waitable_handle(int sync)
        {
            posix_waiter_ = new (std::nothrow) lwp_event_t;
            if (!posix_waiter_) {
                return;
            }
            nsp_status_t status;
            if (sync) {
                lwp_create_synchronous_event(posix_waiter_, status);
            } else {
                lwp_create_notification_event(posix_waiter_, status);
            }

            if (!NSP_SUCCESS(status)) {
                delete posix_waiter_;
                posix_waiter_ = nullptr;
            }
        }

        waitable_handle::~waitable_handle()
        {
            if (posix_waiter_) {
                ::lwp_event_uninit(posix_waiter_);
                delete posix_waiter_;
                posix_waiter_ = nullptr;
            }

        }

        nsp_status_t waitable_handle::wait(uint32_t tsc)
        {
            return lwp_event_wait(posix_waiter_, tsc);
        }

        void waitable_handle::sig()
        {
            ::lwp_event_awaken(posix_waiter_);
        }

        void waitable_handle::reset()
        {
            ::lwp_event_block(posix_waiter_);
        }

        // template<>
        // void attempt_syslog(const std::basic_string<char> &msg, uint32_t err)
        // {
        //     ::posix__syslog(msg.c_str());
        // }

        void pshang()
        {
            ::lwp_hang();
        }

        uint64_t clock_realtime()
        {
            return ::clock_realtime();
        }

        uint64_t clock_epoch()
        {
            return ::clock_epoch();
        }

        uint64_t clock_monotonic()
        {
            return ::clock_monotonic();
        }

        uint64_t gettick()
        {
            return ::clock_boottime();
        }

        void *dlopen(const char *file)
        {
            return ::ifos_dlopen(file);
        }

        void* dlsym(void* handle, const char* symbol)
        {
            return ::ifos_dlsym(handle, symbol);
        }

        void dlclose(void *handle)
        {
            ::ifos_dlclose(handle);
        }
    } // os
} // nsp
