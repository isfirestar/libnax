#include "ifos.h"
#include "abuff.h"
#include "threading.h"
#include "zmalloc.h"

#include <time.h>

#pragma comment(lib, "Advapi32.lib")

static
int _ifos_rmdir(const char* dir)
{
    char all_file[MAX_PATH];
    HANDLE find;
    WIN32_FIND_DATAA wfd;

    if (!dir) {
        return -EINVAL;
    }

    if (ifos_isdir(dir) <= 0) {
        return -1;
    }

    crt_sprintf(all_file, cchof(all_file), "%s\\*.*", dir);

    find = FindFirstFileA(all_file, &wfd);
    if (INVALID_HANDLE_VALUE == find) {
        return -1;
    }
    while (FindNextFileA(find, &wfd)) {
        char target_file[MAX_PATH];
        crt_sprintf(target_file, cchof(target_file), "%s\\%s", dir, wfd.cFileName);
        if (wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (0 == crt_strcmp(".", wfd.cFileName) || 0 == crt_strcmp("..", wfd.cFileName)) {
                continue;
            }
            if (_ifos_rmdir(target_file) < 0) {
                break;
            }
        }
        else {
            if (ifos_rm(target_file) < 0) {
                break;
            }
        }
    }
    FindClose(find);
    return ((RemoveDirectoryA(dir) > 0) ? (0) : (-1));
}

PORTABLEIMPL(pid_t) ifos_gettid()
{
    return GetCurrentThreadId();
}

PORTABLEIMPL(pid_t) ifos_getpid()
{
    return GetCurrentProcessId();
}

PORTABLEIMPL(pid_t) ifos_getppid()
{
    return 0;
}

typedef enum enumSYSTEM_INFORMATION_CLASS
{
	SystemBasicInformation,
	SystemProcessorInformation,
	SystemPerformanceInformation,
	SystemTimeOfDayInformation,
}SYSTEM_INFORMATION_CLASS;

typedef struct tagPROCESS_BASIC_INFORMATION
{
    DWORD ExitStatus;
    DWORD PebBaseAddress;
    DWORD AffinityMask;
    DWORD BasePriority;
    ULONG UniqueProcessId;
    ULONG InheritedFromUniqueProcessId;
}PROCESS_BASIC_INFORMATION;

typedef LONG (WINAPI *PNTQUERYINFORMATIONPROCESS)(HANDLE,UINT,PVOID,ULONG,PULONG)

PORTABLEIMPL(long) ifos_getppid()
{
	static PNTQUERYINFORMATIONPROCESS	NtQueryInformationProcess = NULL;
    LONG                      			status;
    DWORD                     			dwParentPID = 0;
    HANDLE                    			hProcess;
    PROCESS_BASIC_INFORMATION 			pbi;

	if (!NtQueryInformationProcess) {
		NtQueryInformationProcess = (PNTQUERYINFORMATIONPROCESS)GetProcAddress(GetModuleHandle("ntdll"),"NtQueryInformationProcess");
	}

	if (!NtQueryInformationProcess) {
		return -1;
	}

    hProcess = OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,dwId);
    if(!hProcess) {
		return -1;
	}

    status = NtQueryInformationProcess(hProcess,SystemBasicInformation,(PVOID)&pbi,sizeof(PROCESS_BASIC_INFORMATION),NULL);
    if(0 == status) {
        dwParentPID = pbi.InheritedFromUniqueProcessId;
	}

    CloseHandle (hProcess);
	return dwParentPID;
}

PORTABLEIMPL(int) ifos_syslogin(const char* user, const char* key)
{
    return -1;
}

PORTABLEIMPL(void) ifos_sleep(uint64_t ms)
{
    Sleep(MAXDWORD & ms);
}

PORTABLEIMPL(void*) ifos_dlopen(const char* file)
{
    HMODULE mod;
    mod = LoadLibraryA(file);
    return (void*)mod;
}

PORTABLEIMPL(void*) ifos_dlsym(void* handle, const char* symbol)
{
    if (!handle || !symbol) {
        return NULL;
    }
    return (void*)GetProcAddress(handle, symbol);
}

PORTABLEIMPL(int) ifos_dlclose(void* handle)
{
    if (!handle) {
        return -1;
    }

    if (FreeLibrary((HMODULE)handle)) {
        return 0;
    }

    return -1;
}

PORTABLEIMPL(const char*) ifos_dlerror()
{
    return NULL;
}

PORTABLEIMPL(int) ifos_mkdir(const char* const dir)
{
    if (!dir) {
        return -1;
    }

    if (CreateDirectoryA(dir, NULL)) {
        return 0;
    }

    if (ERROR_ALREADY_EXISTS == GetLastError()) {
        return 0;
    }

    return posix__makeerror(GetLastError());
}

PORTABLEIMPL(int) ifos_pmkdir(const char* const dir)
{
    char* dup, * rchr;
    int retval;

    dup = zstrdup(dir);
    retval = ifos_mkdir(dup);

    do {
        if (retval >= 0) {
            break;
        }

        if ((-1 * ERROR_PATH_NOT_FOUND) != retval) {
            break;
        }

        rchr = strrchr(dup, '\\');
        if (!rchr) {
            retval = -1;
            break;
        }

        *rchr = 0;
        retval = ifos_pmkdir(dup);
        if (retval < 0) {
            break;
        }

        retval = ifos_mkdir(dir);
    } while (0);

    zfree(dup);
    return retval;
}

PORTABLEIMPL(int) ifos_rm(const char* const target)
{
    if (!target) {
        return -1;
    }

    if (1 == ifos_isdir(target)) {
        return _ifos_rmdir(target);
    }
    else {
        if (!DeleteFileA(target)) {
            return -1 * GetLastError();
        }
        return 0;
    }
}

PORTABLEIMPL(const char*) ifos_fullpath_current()
{
    static char fullpath[MAXPATH];
    uint32_t length;

    fullpath[0] = 0;

    length = GetModuleFileNameA(NULL, fullpath, sizeof(fullpath) / sizeof(fullpath[0]));
    if (0 != length) {
        return fullpath;
    }
    else {
        return NULL;
    }
}

PORTABLEIMPL(char*) ifos_fullpath_current2(char* holder, int cb)
{
    if (!holder || cb <= 0) {
        return NULL;
    }

    memset(holder, 0, cb);

    uint32_t length;
    length = GetModuleFileNameA(NULL, holder, cb);
    if (0 == length) {
        return NULL;
    }

    return holder;
}

PORTABLEIMPL(const char*) ifos_gettmpdir()
{
    static char buffer[MAXPATH];
    if (0 == GetTempPathA(_countof(buffer), buffer)) {
        return buffer;
    }
    return NULL;
}

PORTABLEIMPL(char*) ifos_gettmpdir2(char* holder, int cb)
{
    if (!holder || cb <= 0) {
        return NULL;
    }

    if (0 == GetTempPathA(cb, holder)) {
        return holder;
    }
    return NULL;
}

PORTABLEIMPL(int) ifos_isdir(const char* const file)
{
    unsigned long attr;

    if (!file) {
        return -1;
    }

    attr = GetFileAttributesA(file);
    if (INVALID_FILE_ATTRIBUTES != attr) {
        return attr & FILE_ATTRIBUTE_DIRECTORY;
    }

    return -1;
}

PORTABLEIMPL(int) ifos_getpriority(int* priority)
{
    DWORD retval;

    if (!priority) {
        return -EINVAL;
    }

    retval = GetPriorityClass(GetCurrentProcess());
    if (0 == retval) {
        return -1;
    }

    *priority = retval;
    return 0;
}

PORTABLEIMPL(int) ifos_setpriority_below()
{
    if (!SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS)) {
        return posix__makeerror(GetLastError());
    }
    return 0;
}

PORTABLEIMPL(int) ifos_setpriority_normal()
{
    if (!SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS)) {
        return posix__makeerror(GetLastError());
    }
    return 0;
}

PORTABLEIMPL(int) ifos_setpriority_critical()
{
    if (!SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
        return posix__makeerror(GetLastError());
    }
    return 0;
}

PORTABLEIMPL(int) ifos_setpriority_realtime()
{
    if (!SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS)) {
        return posix__makeerror(GetLastError());
    }
    return 0;
}

PORTABLEIMPL(int) ifos_getnprocs()
{
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return (int)sysinfo.dwNumberOfProcessors;
}

PORTABLEIMPL(int) ifos_setaffinity_process(int mask)
{
    if (0 == mask) {
        return -1;
    }
    if (SetProcessAffinityMask(GetCurrentProcess(), mask)) {
        return 0;
    }
    return posix__makeerror(GetLastError());
}

PORTABLEIMPL(int) ifos_getaffinity_process(int* mask)
{
    DWORD_PTR ProcessAffinityMask, SystemAffinityMask;
    if (GetProcessAffinityMask(GetCurrentProcess(), &ProcessAffinityMask, &SystemAffinityMask)) {
        if (mask) {
            *mask = (int)ProcessAffinityMask;
        }
        return 0;
    }
    return posix__makeerror(GetLastError());
}

PORTABLEIMPL(int) ifos_getsysmem(sys_memory_t* sysmem)
{
    MEMORYSTATUSEX s_info;
    s_info.dwLength = sizeof(s_info);
    if (!GlobalMemoryStatusEx(&s_info)) {
        return posix__makeerror(GetLastError());
    }

    memset(sysmem, 0, sizeof(sys_memory_t));
    sysmem->totalram = s_info.ullTotalPhys;
    sysmem->freeram = s_info.ullAvailPhys;
    sysmem->totalswap = s_info.ullTotalPageFile;
    sysmem->freeswap = s_info.ullAvailPageFile;
    return 0;
}

PORTABLEIMPL(uint32_t) ifos_getpagesize()
{
    uint32_t ps = 0;
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    ps = sys_info.dwPageSize;
    return ps;
}

PORTABLEIMPL(void) ifos_syslog(const char* const logmsg)
{
    HANDLE shlog;
    const char* strerrs[1];

    if (!logmsg) {
        return;
    }

    shlog = RegisterEventSourceA(NULL, "Application");
    if (INVALID_HANDLE_VALUE == shlog) {
        return;
    }

    strerrs[0] = logmsg;
    BOOL b = ReportEventA(shlog, EVENTLOG_ERROR_TYPE, 0, 0xC0000001, NULL,
        1, 0, strerrs, NULL);

    DeregisterEventSource(shlog);
}

static
int _ifos_gb2312_to_uniocde(char** from, size_t input_bytes, char** to, size_t* output_bytes)
{
    int min;
    int need;

    if (!output_bytes) {
        return -EINVAL;
    }

    min = MultiByteToWideChar(CP_ACP, 0, *from, -1, NULL, 0);
    need = 2 * min;

    if (!to || *output_bytes < (size_t)need) {
        *output_bytes = need;
        return -EAGAIN;
    }

    return MultiByteToWideChar(CP_ACP, 0, *from, -1, (LPWSTR)*to, (int)*output_bytes);
}

static
int _ifos_unicode_to_gb2312(char** from, size_t input_bytes, char** to, size_t* output_bytes)
{
    int min;

    if (!output_bytes) {
        return -EINVAL;
    }

    min = WideCharToMultiByte(CP_OEMCP, 0, (LPCWCH)*from, -1, NULL, 0, NULL, FALSE);
    if (!to || *output_bytes < (size_t)min) {
        *output_bytes = min;
        return -EAGAIN;
    }

    return WideCharToMultiByte(CP_OEMCP, 0, (LPCWCH)*from, -1, *to, (int)*output_bytes, NULL, FALSE);
}

static void _ifos_random_init()
{
    srand((unsigned int)time(NULL));
}

/*  Generate random numbers in the half-closed interva
 *  [range_min, range_max). In other words,
 *  range_min <= random number < range_max
 */
PORTABLEIMPL(int) ifos_random(const int range_min, const int range_max)
{
    int u;
    int r;
    static lwp_once_t once = LWP_ONCE_INIT;

    lwp_once(&once, &_ifos_random_init);

    r = rand();
    if (range_min == range_max) {
        u = ((0 == range_min) ? r : range_min);
    }
    else {
        if (range_max < range_min) {
            u = r;
        }
        else {
            /* Interval difference greater than  7FFFH, If no adjustment is Then the value range is truncated to [min, min+7FFFH) */
            if (range_max - range_min > RAND_MAX) {
                u = (int)((double)rand() / (RAND_MAX + 1) * (range_max - range_min) + range_min);
            }
            else {
                u = (r % (range_max - range_min)) + range_min;
            }
        }
    }

    return u;
}

PORTABLEIMPL(int) ifos_random_block(unsigned char* buffer, int size)
{
    HCRYPTPROV hCryptProv;
    static LPCSTR UserName = "nshost";
    BOOL retval;

    hCryptProv = (HCRYPTPROV)NULL;

    do {
        if (CryptAcquireContextA((HCRYPTPROV*)&hCryptProv, UserName, NULL, PROV_RSA_FULL, 0)) {
            break;
        }

        if (GetLastError() == NTE_BAD_KEYSET) {
            if (CryptAcquireContextA(&hCryptProv, UserName, NULL, PROV_RSA_FULL, CRYPT_NEWKEYSET)) {
                break;
            }
        }

        return posix__makeerror(GetLastError());
    } while (0);

    retval = CryptGenRandom(hCryptProv, (DWORD)size, buffer);

    CryptReleaseContext(hCryptProv, 0);

    return retval ? size : -1;
}

PORTABLEIMPL(int) ifos_file_open(const char* path, int flag, int mode, file_descriptor_t* descriptor)
{
    HANDLE fd;
    DWORD dwDesiredAccess;
    DWORD dwCreationDisposition;

    if (!path || !descriptor) {
        return -EINVAL;
    }

    dwDesiredAccess = 0;
    if (flag & FF_WRACCESS) {
        dwDesiredAccess |= (GENERIC_READ | GENERIC_WRITE);
    }
    else {
        dwDesiredAccess |= GENERIC_READ;
    }

    dwCreationDisposition = 0;
    switch (flag & ~1) {
    case FF_OPEN_EXISTING:
        dwCreationDisposition = OPEN_EXISTING;
        break;
    case FF_OPEN_ALWAYS:
        dwCreationDisposition = OPEN_ALWAYS;
        break;
    case FF_CREATE_NEWONE:
        dwCreationDisposition = CREATE_NEW;
        break;
    case FF_CREATE_ALWAYS:
        dwCreationDisposition = CREATE_ALWAYS;
        break;
    default:
        return -EINVAL;
    }

    fd = CreateFileA(path, dwDesiredAccess, FILE_SHARE_READ, NULL, dwCreationDisposition, FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == fd) {
        return posix__makeerror(GetLastError());
    }
    *descriptor = fd;
    return 0;
}

PORTABLEIMPL(int) ifos_file_read(file_descriptor_t fd, unsigned char* buffer, int size)
{
    int offset, n;

    if (!buffer) {
        return -EINVAL;
    }

    if (INVALID_HANDLE_VALUE == fd) {
        return -EBADFD;
    }

    n = ReadFile(fd, buffer, (DWORD)size, (LPDWORD)&offset, NULL);
    if (!n) {
        return posix__makeerror(GetLastError());
    }

    return offset;
}

PORTABLEIMPL(int) ifos_file_write(file_descriptor_t fd, const unsigned char* buffer, int size)
{
    int offset, n;

    if (!buffer) {
        return -EINVAL;
    }

    if (INVALID_HANDLE_VALUE == fd) {
        return -EBADFD;
    }

    n = WriteFile(fd, buffer, (DWORD)size, (LPDWORD)&offset, NULL);
    if (!n) {
        return posix__makeerror(GetLastError());
    }

    return offset;
}

PORTABLEIMPL(void) ifos_file_close(file_descriptor_t fd)
{
    if (INVALID_HANDLE_VALUE != fd) {
        CloseHandle(fd);
    }
}

PORTABLEIMPL(int) ifos_file_flush(file_descriptor_t fd)
{
    if (INVALID_HANDLE_VALUE == fd) {
        return -EBADFD;
    }

    if (!FlushFileBuffers(fd)) {
        return (int)((int)GetLastError() * -1);
    }

    return 0;
}

PORTABLEIMPL(int64_t) ifos_file_fgetsize(file_descriptor_t fd)
{
    int64_t filesize = 1;
    LARGE_INTEGER size;

    if (INVALID_HANDLE_VALUE == fd) {
        return -EBADFD;
    }

    if (GetFileSizeEx(fd, &size)) {
        filesize = size.HighPart;
        filesize <<= 32;
        filesize |= size.LowPart;
    }
    else {
        return posix__makeerror(GetLastError());
    }
    return filesize;
}

PORTABLEIMPL(int64_t) ifos_file_getsize(const char* path)
{
    WIN32_FIND_DATAA wfd;
    HANDLE find;
    int64_t size;

    if (!path) {
        return -EINVAL;
    }

    find = FindFirstFileA(path, &wfd);
    if (INVALID_HANDLE_VALUE == find) {
        return (int64_t)INVALID_FILE_SIZE;
    }

    size = wfd.nFileSizeHigh;
    size <<= 32;
    size |= wfd.nFileSizeLow;

    FindClose(find);
    return size;
}

PORTABLEIMPL(int) ifos_file_seek(file_descriptor_t fd, uint64_t offset)
{
    LARGE_INTEGER move, pointer;

    if (INVALID_HANDLE_VALUE == fd) {
        return -EBADFD;
    }

    move.QuadPart = offset;
    if (!SetFilePointerEx(fd, move, &pointer, FILE_BEGIN)) {
        return posix__makeerror(GetLastError());
    }
    return 0;
}

PORTABLEIMPL(const char*) ifos_getpedir()
{
    char* p;
    static char dir[MAXPATH];
    const char* fullpath;

    fullpath = ifos_fullpath_current();
    if (!fullpath) {
        return NULL;
    }

    p = strrchr(fullpath, POSIX__DIR_SYMBOL);
    if (!p) {
        return NULL;
    }

    *p = 0;
    crt_strcpy(dir, cchof(dir), fullpath);
    return dir;
}

PORTABLEIMPL(char*) ifos_getpedir2(char* holder, int cb)
{
    char* p;
    char fullpath[MAXPATH];

    if (!holder || cb <= 0) {
        return NULL;
    }

    p = ifos_fullpath_current2(fullpath, cchof(fullpath));
    if (!p) {
        return NULL;
    }

    p = strrchr(fullpath, POSIX__DIR_SYMBOL);
    if (!p) {
        return NULL;
    }

    *p = 0;
    crt_strcpy(holder, cb, fullpath);
    return holder;
}

PORTABLEIMPL(const char*) ifos_getpename()
{
    char* p;
    static char name[MAXPATH];
    const char* fullpath;

    fullpath = ifos_fullpath_current();
    if (!fullpath) {
        return NULL;
    }

    p = strrchr(fullpath, POSIX__DIR_SYMBOL);
    if (!p) {
        return NULL;
    }

    crt_strcpy(name, cchof(name), p + 1);
    return &name[0];
}

PORTABLEIMPL(char*) ifos_getpename2(char* holder, int cb)
{
    char* p;
    char fullpath[MAXPATH];

    if (!holder || cb <= 0) {
        return NULL;
    }

    p = ifos_fullpath_current2(fullpath, sizeof(fullpath));
    if (!p) {
        return NULL;
    }

    p = strrchr(fullpath, POSIX__DIR_SYMBOL);
    if (!p) {
        return NULL;
    }

    crt_strcpy(holder, cb, p + 1);
    return holder;
}

PORTABLEIMPL(int) ifos_iconv(const char* from_encode, const char* to_encode, char** from, size_t from_bytes, char** to, size_t* to_bytes) {
    if (0 == crt_strcasecmp(from_encode, "gb2312") && 0 == crt_strcasecmp(to_encode, "unicode")) {
        return _ifos_gb2312_to_uniocde(from, from_bytes, to, to_bytes);
    }
    else if (0 == crt_strcasecmp(from_encode, "unicode") && 0 == crt_strcasecmp(to_encode, "gb2312")) {
        return _ifos_unicode_to_gb2312(from, from_bytes, to, to_bytes);
    }
    return EINVAL;
}

PORTABLEIMPL(const char*) ifos_getelfname() {
    return ifos_getpename();
}

PORTABLEIMPL(char*) ifos_getelfname2(char* holder, int cb) {
    return ifos_getpename2(holder, cb);
}
