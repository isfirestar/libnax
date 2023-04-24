#include "compiler.h"

#include "abuff.h"
#include "ifos.h"
#include "clist.h"
#include "zmalloc.h"

/* #define _GNU_SOURCE 1 */
#include <sched.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <iconv.h>
#include <locale.h>
#include <unistd.h>
#include <shadow.h>
#include <pwd.h>

#include <sys/types.h>
#include <syscall.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/syslog.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>

/* -lcrypt */
#include <crypt.h>

static nsp_status_t _ifos_rmdir(const char *dir)
{
    /* > rm -rf dir */
    struct dirent *ent;
    DIR *dirp;
    char filename[512];
    nsp_status_t status;

    dirp = opendir(dir);
    if (!dirp) {
        return posix__makeerror(errno);
    }

    status = NSP_STATUS_SUCCESSFUL;
    while (NULL != (ent = readdir(dirp))) {
        if (0 == crt_strcmp(ent->d_name, ".") || 0 == crt_strcmp(ent->d_name, "..")) {
            continue;
        }

        crt_sprintf(filename, sizeof_array(filename), "%s/%s", dir, ent->d_name);

        if (ifos_isdir(filename)) {
            status = _ifos_rmdir(filename);
        } else {
            status = ifos_rm(filename);
            if (!NSP_SUCCESS(status)) {
                break;
            }
        }
    }

    remove(dir);
    closedir(dirp);
    return status;
}

pid_t ifos_gettid()
{
    return syscall(SYS_gettid);
}

pid_t ifos_getpid()
{
    return syscall(SYS_getpid);
}

pid_t ifos_getppid()
{
	return syscall(SYS_getppid);
}

void ifos_sleep(uint64_t ms)
{
    usleep(ms * 1000);
}

void *ifos_dlopen(const char *file)
{
    return dlopen(file, /*RTLD_LAZY*/RTLD_NOW);
}

void *ifos_dlopen2(const char *file, int flags)
{
    return dlopen(file, flags);
}

void *ifos_dlsym(void *handle, const char *symbol)
{
    return dlsym(handle, symbol);
}

void ifos_dlclose(void *handle)
{
    dlclose(handle);
}

const char *ifos_dlerror()
{
    return dlerror();
}

const char *ifos_dlerror2(abuff_128_t *estr)
{
    const char *p;

    if (unlikely(!estr)) {
        return NULL;
    }

    /* dlerror() returns NULL if no errors have occurred since initialization or since it was last called. */
    p = dlerror();
    if (p) {
        abuff_strcpy(estr, p);
        return estr->st;
    }

    return NULL;
}

nsp_status_t ifos_mkdir(const char *const dir)
{
    if (unlikely(!dir)) {
        return posix__makeerror(EINVAL);
    }

    if (0 == mkdir(dir, 0755)) {
        return NSP_STATUS_SUCCESSFUL;
    }

    if (EEXIST == errno) {
        return NSP_STATUS_SUCCESSFUL;
    }

    return posix__makeerror(errno);
}

nsp_status_t ifos_pmkdir(const char *const dir)
{
    char *dup, *rchr;
    nsp_status_t status;

    if (unlikely(!dir)) {
        return posix__makeerror(EINVAL);
    }

    dup = zstrdup(dir);
    status = ifos_mkdir(dup);

    do {
        if ( NSP_SUCCESS(status) ) {
            break;
        }

        if ( !NSP_FAILED_AND_ERROR_EQUAL(status, ENOENT) ) {
            break;
        }

        rchr = strrchr(dup, '/');
        if (!rchr) {
            status = NSP_STATUS_FATAL;
            break;
        }

        *rchr = 0;
        status = ifos_pmkdir(dup);
        if ( !NSP_SUCCESS(status) ) {
            break;
        }

        status = ifos_mkdir(dir);
    } while(0);

    zfree(dup);
    return status;
}

nsp_status_t ifos_rm(const char *const target)
{
    if ( unlikely(!target) ) {
        return posix__makeerror(EINVAL);
    }

    if (ifos_isdir(target)) {
        return _ifos_rmdir(target);
    } else {
        if (0 == remove(target)) {
            return NSP_STATUS_SUCCESSFUL;
        }
        return posix__makeerror(errno);
    }
}

nsp_status_t ifos_fullpath_current(ifos_path_buffer_t *holder)
{
    return readlink("/proc/self/exe", holder->st, abuff_size(holder)) < 0 ? posix__makeerror(errno) : NSP_STATUS_SUCCESSFUL;
}

nsp_status_t ifos_getpedir(ifos_path_buffer_t *holder)
{
    char *p;
    nsp_status_t status;
    ifos_path_buffer_t dir;

    if ( unlikely((!holder)) ) {
        return posix__makeerror(EINVAL);
    }

    status = ifos_fullpath_current(&dir);
    if ( unlikely(!NSP_SUCCESS(status)) ) {
        return status;
    }

    p = strrchr(dir.st, '/');
    if (!p) {
        return posix__makeerror(ENOTDIR);
    }

    *p = 0;
    abuff_strcpy(holder, dir.st);
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t ifos_getpename(ifos_path_buffer_t *holder)
{
    char *p;
    nsp_status_t status;
    ifos_path_buffer_t dir;

    if ( unlikely((!holder)) ) {
        return posix__makeerror(EINVAL);
    }

    status = ifos_fullpath_current(&dir);
    if ( unlikely(!NSP_SUCCESS(status)) ) {
        return status;
    }

    p = strrchr(dir.st, '/');
    if (!p) {
        return posix__makeerror(ENOTDIR);
    }

    abuff_strcpy(holder, p + 1);
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t ifos_getelfname(ifos_path_buffer_t *holder) {
    return ifos_getpename(holder);
}

nsp_status_t ifos_gettmpdir(ifos_path_buffer_t *holder)
{
    if ( unlikely((!holder)) ) {
        return posix__makeerror(EINVAL);
    }

    abuff_strcpy(holder, "/tmp");
    return NSP_STATUS_SUCCESSFUL;
}

nsp_boolean_t ifos_isdir(const char *const file)
{
    struct stat st;

    if ( unlikely(!file) ) {
        return NO;
    }

    if ( unlikely(stat(file, &st) < 0) ) {
        return NO;
    }

    /* if the target of symbolic link is a directory, @st.st_mode should be __S_IFDIR not be __S_IFLINK
     * __S_IFLINK only indicate a symbolic link which linked to a regular file.
     * using a relative path of symbolic link which linked to directory is effective, it can open file normal
     * for example:
     * /root/configures -> /etc
     * int fd = open("/root/configure/passwd", O_RDONLY); can be work normaly
     *
     * we can use shell command 'find . -type l' to search all symbolic links
     */
    if (st.st_mode & __S_IFDIR) {
        /* return S_IFDIR; */
        return YES;
    }

    return NO;
}

nsp_status_t ifos_getpriority_process(int *priority)
{
    int who;
    int retval;

    if ( unlikely(!priority) ) {
        return posix__makeerror(EINVAL);
    }

    who = 0;
    retval = getpriority(PRIO_PROCESS, who);
    if (-1 == retval) {
        return posix__makeerror(errno);
    }

    *priority = retval;
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t ifos_setpriority_process(int priority)
{
    int who;
    int retval;

    who = 0;
    retval = setpriority(PRIO_PROCESS, who, priority);
    if (-1 == retval) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t ifos_nice(int inc, int *newinc)
{
    int retval;

    if ( -1 == (retval = nice(inc)) ) {
        return posix__makeerror(errno);
    }

    if (newinc) {
        *newinc = retval;
    }

    return NSP_STATUS_SUCCESSFUL;
}

/*
int ifos_setpriority_below()
{
    return nice(5);
}

int ifos_setpriority_normal()
{
    return nice(0);
}

int ifos_setpriority_critical()
{
    return nice(-5);
}

int ifos_setpriority_realtime()
{
    return nice(-10);
} */

int ifos_getnprocs()
{
    return sysconf(_SC_NPROCESSORS_CONF);
}

nsp_status_t ifos_setaffinity_process(int mask)
{
    int i;
    cpu_set_t cpus;

    if ( unlikely(0 == mask) ) {
        return posix__makeerror(EINVAL);
    }

    CPU_ZERO(&cpus);

    for (i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            CPU_SET(i, &cpus);
        }
    }

    if (0 == sched_setaffinity(0, sizeof(cpu_set_t), &cpus)) {
        return NSP_STATUS_SUCCESSFUL;
    }

    return posix__makeerror(errno);
}

nsp_status_t ifos_getaffinity_process(int *mask)
{
    int i;
    cpu_set_t cpus;
    int n;

    n = 0;
    CPU_ZERO(&cpus);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &cpus) < 0) {
        return posix__makeerror(errno);
    }

    for (i = 0; i < 32; i++) {
        if(CPU_ISSET(i, &cpus)) {
            n |= (1 << i);
        }
    }

    if (mask) {
        *mask = n;
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t ifos_getsysmem(sys_memory_t *sysmem)
{
    struct sysinfo s_info;

    if ( unlikely(!sysmem) ) {
        return posix__makeerror(EINVAL);
    }

    if (sysinfo(&s_info) < 0) {
        return posix__makeerror(errno);
    }

    memset(sysmem, 0, sizeof ( sys_memory_t));

    /* in 32bit(arm?) version os, the s_info.*high will be some unknown data */
    if (s_info.totalhigh > 0 && sizeof(void *) > sizeof(uint32_t)) {
        sysmem->totalram = s_info.totalhigh;
        sysmem->totalram <<= 32;
    }
    sysmem->totalram |= s_info.totalram;

    if (s_info.freehigh > 0 && sizeof(void *) > sizeof(uint32_t)) {
        sysmem->freeram = s_info.freehigh;
        sysmem->freeram <<= 32;
    }
    sysmem->freeram |= s_info.freeram;
    sysmem->totalswap = s_info.totalswap;
    sysmem->freeswap = s_info.freeswap;
    /*  FILE *fp;
        char str[81];
        memset(str,0,81);
        fp=popen("cat /proc/meminfo | grep MemTotal:|sed -e 's/.*:[^0-9]//'","r");
        if(fp >= 0)
        {
            fgets(str,80,fp);
            fclose(fp);
        }
    */
    return NSP_STATUS_SUCCESSFUL;
}

uint32_t ifos_getpagesize()
{
    return sysconf(_SC_PAGE_SIZE);
}

void ifos_syslog(const char *const logmsg)
{
    /* cat /var/log/messages | tail -n1 */
    if ( unlikely(!logmsg) ) {
        return;
    }

    syslog(LOG_USER | LOG_ERR, "[%d]# %s", getpid(), logmsg);
}

/*  Generate random numbers in the half-closed interva
 *  [range_min, range_max). In other words,
 *  range_min <= random number < range_max
 */
int ifos_random(const int range_min, const int range_max)
{
    static int rand_begin = 0;
    int u;
    int r;

    if (1 == __atomic_add_fetch(&rand_begin, 1, __ATOMIC_SEQ_CST)) {
        srand((unsigned int) time(NULL));
    } else {
        __atomic_sub_fetch(&rand_begin, 1, __ATOMIC_SEQ_CST);
    }

    r = rand();

    if (range_min == range_max) {
        u = ((0 == range_min) ? r : range_min);
    } else {
        if (range_max < range_min) {
            u = r;
        } else {
            u = (r % (range_max - range_min)) + range_min;
        }
    }

    return u;
}

int ifos_random_block(unsigned char *buffer, int size)
{
    int fd;
    int offset;
    int n;

    fd = open("/dev/random", O_RDONLY);
    if ( unlikely(fd < 0) ) {
        return posix__makeerror(errno);
    }

    offset = 0;
    while (offset < size) {
        n = read(fd, buffer + offset, size - offset);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            return posix__makeerror(errno);
        }

        if (0 == n) {
            break;
        }

        offset += n;
    }

    close(fd);
    return offset;
}

nsp_status_t ifos_file_open(const char *path, int flag, int mode, file_descriptor_t *descriptor)
{
    int fflags;
    int fd;

    if ( unlikely((!path || !descriptor)) ) {
        return posix__makeerror(EINVAL);
    }

    fflags = 0;
    if (flag & FF_WRACCESS) {
        fflags |= O_RDWR;
    } else {
        fflags |= O_RDONLY;
    }

    switch(flag & ~1) {
        case FF_OPEN_EXISTING:
            fd = open(path, fflags);
            break;
        case FF_OPEN_ALWAYS:
            fd = open(path, fflags | O_CREAT, mode);
            break;
        case FF_CREATE_NEWONE:
            fd = open(path, fflags | O_CREAT | O_EXCL, mode);
            break;
        case FF_CREATE_ALWAYS:
            /* In order to maintain consistency with Windows API behavior, when a file exists, the file data is cleared directly.
                    do NOT use O_APPEND here*/
            fd = open(path, fflags | O_CREAT | O_TRUNC, mode);
            break;
        default:
            return posix__makeerror(EINVAL);
    }

    if (fd < 0) {
        *descriptor = INVALID_FILE_DESCRIPTOR;
        return posix__makeerror(errno);
    } else {
        *descriptor = fd;
        return NSP_STATUS_SUCCESSFUL;
    }
}

int ifos_file_read(file_descriptor_t fd, void *buffer, int size)
{
    int offset, n;
    unsigned char *p;

    if ( unlikely((!buffer || size <= 0)) ) {
        return posix__makeerror(EINVAL);
    }

    offset = 0;
    p = (unsigned char *)buffer;
    while (offset < size) {
        n = read(fd, p + offset, size - offset);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                return posix__makeerror(errno);
            }
        }

        if (0 == n) {
            break;
        }

        offset += n;
    }

    return offset;
}

int ifos_file_write(file_descriptor_t fd, const void *buffer, int size)
{
    int offset, n;
    const unsigned char *p;

    if ( unlikely((!buffer || size <= 0)) ) {
        return posix__makeerror(EINVAL);
    }

    offset = 0;
    p = (const unsigned char *)buffer;
    while (offset < size) {
        n = write(fd, p + offset, size - offset);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }

            /* no space for write more data into hard disk,
                this NOT means a error, but MUST break now */
            if (errno == ENOSPC) {
                break;
            }

            return posix__makeerror(errno);
        }

        if (0 == n) {
            break;
        }

        offset += n;
    }

    return offset;
}

void ifos_file_close(file_descriptor_t fd)
{
    if ( unlikely(fd < 0) ) {
        return;
    }

    close(fd);
}

nsp_status_t ifos_file_flush(file_descriptor_t fd)
{
    if ( unlikely(fd < 0) ) {
        return posix__makeerror(EINVAL);
    }

    if ( fsync(fd) < 0 ) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

int64_t ifos_file_fgetsize(file_descriptor_t fd)
{
    int64_t filesize = -1;
    struct stat statbuf;

    if (fstat(fd, &statbuf) < 0) {
        return posix__makeerror(errno);
    } else {
        filesize = (int64_t)statbuf.st_size;
    }
    return filesize;
}

int64_t ifos_file_getsize(const char *path)
{
    int64_t filesize = -1;
    struct stat statbuf;

    if ( unlikely(!path) ) {
        return posix__makeerror(EINVAL);
    }

    if (stat(path, &statbuf) < 0) {
        return posix__makeerror(errno);
    } else {
        filesize = (int64_t)statbuf.st_size;
    }
    return filesize;
}

nsp_status_t ifos_file_seek(file_descriptor_t fd, uint64_t offset)
{
    __off_t newoff;

    newoff = lseek(fd, (__off_t) offset, SEEK_SET);
    if (newoff == (__off_t)-1) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}
