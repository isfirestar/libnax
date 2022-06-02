#ifndef POSIX_IFOS_H
#define POSIX_IFOS_H

#include "compiler.h"
#include "abuff.h"       /* abuff */

typedef abuff_type(255)   ifos_path_buffer_t;

#if _WIN32
	#include <Windows.h>
    typedef DWORD pid_t;

    typedef HANDLE file_descriptor_t;
    #define INVALID_FILE_DESCRIPTOR     ((file_descriptor_t)INVALID_HANDLE_VALUE)

    #if !defined STDIN_FILENO
        #define STDIN_FILENO        (file_descriptor_t)GetStdHandle(STD_INPUT_HANDLE)
    #endif

    #if !defined STDOUT_FILENO
        #define STDOUT_FILENO       (file_descriptor_t)GetStdHandle(STD_OUTPUT_HANDLE)
    #endif

    #if !defined STDERR_FILENO
        #define STDERR_FILENO       (file_descriptor_t)GetStdHandle(STD_ERROR_HANDLE)
    #endif
#else
	#include <unistd.h>
	#include <fcntl.h>

    typedef int file_descriptor_t;
    #define INVALID_FILE_DESCRIPTOR     ((file_descriptor_t)-1)
#endif

/* lowest 1 bit to describe open access mode, 0 means read only */
#define FF_RDACCESS         (0)
#define FF_WRACCESS         (1)
/* next 3 bit to describe open method */
#define FF_OPEN_EXISTING    (2)     /* failed on file NOT existed */
#define FF_OPEN_ALWAYS      (4)     /* create a new file when file NOT existed, otherwise open existing */
#define FF_CREATE_NEWONE    (6)     /* failed on file existed  */
#define FF_CREATE_ALWAYS    (8)     /* truncate and open file when it is existed, otherwise create new one with zero size */

/* ifos-ps */
PORTABLEAPI(pid_t) ifos_gettid();
PORTABLEAPI(pid_t) ifos_getpid();
PORTABLEAPI(pid_t) ifos_getppid();
PORTABLEAPI(void) ifos_sleep(uint64_t milliseconds);

/* ifos-dl */
PORTABLEAPI(void *) ifos_dlopen(const char *file);
PORTABLEAPI(void *) ifos_dlopen2(const char *file, int flags);
/* in POSIX, "RTLD_NEXT" and "RTLD_DEFAULT" are now supported. define RTLD_NEXT ((void *) -1l), define RTLD_DEFAULT ((void *)0L)*/
PORTABLEAPI(void *) ifos_dlsym(void* handle, const char* symbol);
PORTABLEAPI(void) ifos_dlclose(void *handle);
PORTABLEAPI(const char * ) DEPRECATED("retrun pointer unsafe") ifos_dlerror();
PORTABLEAPI(const char * ) ifos_dlerror2(abuff_128_t *estr);

/* ifos-dir */
PORTABLEAPI(nsp_status_t) ifos_mkdir(const char *const dir);
PORTABLEAPI(nsp_status_t) ifos_pmkdir(const char *const dir);
PORTABLEAPI(nsp_status_t) ifos_rm(const char *const target); /* if @target is a directory, this method is the same as rm -fr */
/* inner syscall failed, function return -1, not a dir return 0, is dir, return 0x4000 on linux 0x10 on win32 */
PORTABLEAPI(nsp_boolean_t) ifos_isdir(const char *const file);

/* ifos-path */
PORTABLEAPI(nsp_status_t) ifos_fullpath_current(ifos_path_buffer_t *holder); /* obtain the fully path of current execute file(ELF/PE) */
PORTABLEAPI(nsp_status_t) ifos_getpedir(ifos_path_buffer_t *holder);  /* obtain the directory contain current execute file(ELF/PE) */
PORTABLEAPI(nsp_status_t) ifos_getpename(ifos_path_buffer_t *holder);
PORTABLEAPI(nsp_status_t) ifos_getelfname(ifos_path_buffer_t *holder);
PORTABLEAPI(nsp_status_t) ifos_gettmpdir(ifos_path_buffer_t *holder);

/*ifos-ps*/
/* obtain or adjust the priority of process
 * support 5,0,-5,-10 priority level on Linux,
 * corresponding to IDLE_PRIORITY_CLASS NORMAL_PRIORITY_CLASS HIGH_PRIORITY_CLASS REALTIME_PRIORITY_CLASS on MS-API */
PORTABLEAPI(nsp_status_t) ifos_getpriority_process(int *priority);
PORTABLEAPI(nsp_status_t) ifos_setpriority_process(int priority);
PORTABLEAPI(nsp_status_t) ifos_nice(int inc, int *newinc);
/* obtain or adjust the affinity of process and CPU core.
 * notes that : MS-API use bit mask to describe the affinity attribute,but Linux without it.
 *	for portable reason, using bit-mask here unified */
PORTABLEAPI(nsp_status_t) ifos_setaffinity_process(int mask);
PORTABLEAPI(nsp_status_t) ifos_getaffinity_process(int *mask);
/* obtain the CPU core-count in this machine */
PORTABLEAPI(int) ifos_getnprocs();

/* ifos-mm */
typedef struct {
    uint64_t totalram;
    uint64_t freeram;
    uint64_t totalswap;
    uint64_t freeswap;
} sys_memory_t;
PORTABLEAPI(nsp_status_t) ifos_getsysmem(sys_memory_t *sysmem);
PORTABLEAPI(uint32_t) ifos_getpagesize(); /* get the system memory page size */

/* wirte syslog */
PORTABLEAPI(void) ifos_syslog(const char *const logmsg );

/* ifos-calc */
/*  Generate random numbers in the half-closed interval
 *  [range_min, range_max). In other words,
 *  range_min <= random number < range_max */
PORTABLEAPI(int) ifos_random(const int range_min, const int range_max);
PORTABLEAPI(int) ifos_random_block(unsigned char *buffer, int size);

/* ifos-fd */
/* windows application ignore @mode parameter
   @descriptor return the file-descriptor/file-handle when all syscall successed */
PORTABLEAPI(nsp_status_t) ifos_file_open(const char *path, int flag, int mode, file_descriptor_t *descriptor);
PORTABLEAPI(int64_t) ifos_file_fgetsize(file_descriptor_t fd);
PORTABLEAPI(int64_t) ifos_file_getsize(const char *path);
PORTABLEAPI(nsp_status_t) ifos_file_seek(file_descriptor_t fd, uint64_t offset);
/* return value is the bytes of data read/write from/to the specify file
 * negative indicate a error */
PORTABLEAPI(int) ifos_file_read(file_descriptor_t fd, void *buffer, int size);
PORTABLEAPI(int) ifos_file_write(file_descriptor_t fd, const void *buffer, int size);
PORTABLEAPI(void) ifos_file_close(file_descriptor_t fd);
PORTABLEAPI(nsp_status_t) ifos_file_flush(file_descriptor_t fd);

#if !defined EBADFD
#define EBADFD	77
#endif

#endif /* POSIX_IFOS_H */
