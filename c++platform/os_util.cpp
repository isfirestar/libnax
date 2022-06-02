#include "os_util.hpp"

#include "icom/posix_ifos.h"
#include "icom/posix_string.h"
#include "icom/posix_wait.h"
#include "icom/clock.h"

#if _WIN32
#include <Windows.h>
#pragma comment(lib, "Advapi32.lib")
#include <Shlobj.h>
#pragma comment( lib, "shell32.lib")
#endif

#include <stack>

namespace nsp {
    namespace os {

        uint32_t get_pagesize() {
            return ::posix__getpagesize();
        }

        template<>
        std::basic_string<char> get_module_fullpath<char>() {
            char fullpath[MAXPATH], *p;
            p = ::posix__fullpath_current2(fullpath, sizeof(fullpath));
            return std::basic_string<char>(p);
        }

        template<>
        std::basic_string<char> get_module_directory<char>() {
            char pedir[MAXPATH], *p;
            p = posix__getpedir2(pedir, sizeof(pedir));
            return std::basic_string<char>(p);
        }

        template<>
        std::basic_string<char> get_module_filename<char>() {
            char pename[MAXPATH], *p;
            p = ::posix__getpename2(pename, sizeof(pename));
            return std::basic_string<char>(p);
        }

        template<>
        uint64_t get_filesize<char>(const std::basic_string<char> &path) {
            return ::posix__file_getsize(path.data());
        }

        uint64_t fget_filesize(const file_descriptor_t fd) {
            return ::posix__file_fgetsize(fd);
        }
#if _WIN32

        template<>
        std::basic_string<char> get_sysdir<char>() {
            char buffer[MAX_PATH];
            if (!::GetSystemDirectoryA(buffer, cchof(buffer))) {
                return "";
            }
            return buffer;
        }

        template<>
        std::basic_string<wchar_t> get_sysdir() {
            wchar_t buffer[MAX_PATH];
            if (!::GetSystemDirectoryW(buffer, cchof(buffer))) {
                return L"";
            }
            return buffer;
        }

        template<>
        std::basic_string<char> get_appdata_dir() {
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
        std::basic_string<wchar_t> get_appdata_dir() {
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
        std::basic_string<char> get_tmpdir<char>() {
            return ::posix__gettmpdir();
        }

        template<>
        int rmfile<char>(const std::basic_string<char> &target) {
            return ::posix__rm(target.c_str());
        }

        template<>
        int mkdir<char>(const std::basic_string<char> &dir) {
            return ::posix__mkdir(dir.c_str());
        }

        template <>
        int mkdir_s<char>(const std::basic_string<char> &dir) {
            return ::posix__pmkdir(dir.c_str());
        }

        template<>
        int is_dir<char>(const std::basic_string<char> &file) {
            return ::posix__isdir(file.c_str());
        }

        template<>
        int rmdir_s<char>(const std::basic_string<char> &dir) {
            return ::posix__rm(dir.c_str());
        }

        long gettid() {
            return ::posix__gettid();
        }

        int getpid() {
            return ::posix__getpid();
        }

        int getnprocs() {
            return ::posix__getnprocs();
        }

        int getsysmem(uint64_t &total, uint64_t &free, uint64_t &total_swap, uint64_t &free_swap) {
            sys_memory_t sysmem;
            if (::posix__getsysmem(&sysmem) < 0) {
                return -1;
            }
            total = sysmem.totalram;
            free = sysmem.freeram;
            total_swap = sysmem.totalswap;
            free_swap = sysmem.freeswap;
            return 0;
        }

        waitable_handle::waitable_handle(int sync) {
            posix_waiter_ = new posix__waitable_handle_t;
            if (sync) {
                ::posix__init_synchronous_waitable_handle((posix__waitable_handle_t *) posix_waiter_);
            } else {
                ::posix__init_notification_waitable_handle((posix__waitable_handle_t *) posix_waiter_);
            }
        }

        waitable_handle::~waitable_handle() {
            if (posix_waiter_) {
                ::posix__uninit_waitable_handle((posix__waitable_handle_t *) posix_waiter_);
                delete (posix__waitable_handle_t *)posix_waiter_;
                posix_waiter_ = nullptr;
            }

        }

        int waitable_handle::wait(uint32_t tsc) {
            return ::posix__waitfor_waitable_handle((posix__waitable_handle_t *) posix_waiter_, tsc);
        }

        void waitable_handle::sig() {
            ::posix__sig_waitable_handle((posix__waitable_handle_t *) posix_waiter_);
        }

        void waitable_handle::reset() {
            ::posix__block_waitable_handle((posix__waitable_handle_t *) posix_waiter_);
        }

        template<>
        void attempt_syslog(const std::basic_string<char> &msg, uint32_t err) {
            ::posix__syslog(msg.c_str());
        }

        void pshang() {
            waitable_handle().wait();
        }

        uint64_t clock_gettime() {
            return ::posix__clock_gettime();
        }

        uint64_t clock_epoch() {
            return ::posix__clock_epoch();
        }

        uint64_t clock_monotonic()
        {
            return ::posix__clock_monotonic();
        }

        uint64_t gettick() {
            return ::posix__gettick();
        }

        void *dlopen(const char *file) {
            return ::posix__dlopen(file);
        }

        void* dlsym(void* handle, const char* symbol) {
            return ::posix__dlsym(handle, symbol);
        }

        int dlclose(void *handle) {
            return ::posix__dlclose(handle);
        }
    } // os
} // nsp
