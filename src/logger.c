#include "logger.h"

#include <stdarg.h>

#include "clist.h"
#include "threading.h"
#include "clock.h"
#include "ifos.h"
#include "atom.h"
#include "abuff.h"
#include "zmalloc.h"

/* the upper limit of the number of rows in a log file  */
#define  MAXIMUM_LOGFILE_LINE    (5000)

/* maximum asynchronous cache */
#define MAXIMUM_LOGSAVE_COUNT       (500)
#define SAFE_LOGSAVE_COUNT          (100)
#define WARNING_LOGSAVE_COUNT       (400)

#define LOGSAVE_PENDING_SAFE     (0)
#define LOGSAVE_PENDING_DANGER   (1)

static const char *LOG__LEVEL_TXT[] = {
    "info", "warning", "error", "fatal", "trace"
};

struct log_file_descriptor {
    struct list_head file_link;
    file_descriptor_t fd;
    datetime_t timestamp;
    int line_count_;
    char module[LOG_MODULE_NAME_LEN];
} ;

static LIST_HEAD(__log__file_head); /* list<struct log_file_descriptor> */
static lwp_mutex_t __log_file_lock;
static ifos_path_buffer_t __log_root_directory = { 0 };

struct log_async_node {
    struct list_head link;
    char logstr[MAXIMUM_LOG_BUFFER_SIZE];
    int target;
    datetime_t timestamp;
    enum log_levels level;
    char module[LOG_MODULE_NAME_LEN];
};

struct log_async_context {
    int pending;
    int warning;  /* log save pending warning */
    struct list_head idle;
    struct list_head busy;
    lwp_mutex_t lock;
    lwp_t async_thread;
    lwp_event_t notify;
    struct log_async_node *misc_memory;
};

static struct log_async_context __log_async;

static void __close_log_file(struct log_file_descriptor *file)
{
    ifos_file_close(file->fd);
    file->fd = INVALID_FILE_DESCRIPTOR;
}

static int __write_log_file(struct log_file_descriptor *file, const void *buf, int count)
{
    int retval;

    retval = ifos_file_write(file->fd, buf, count);
    if ( retval > 0) { /* need posix__file_flush(file->fd) ? */
        file->line_count_++;
    }

    return retval;
}

static struct log_file_descriptor *__attach_log_file(const datetime_t *currst, const char *module)
{
    char name[128], path[600];
    ifos_path_buffer_t pename;
    int retval;
    struct list_head *pos;
    struct log_file_descriptor *file;

    file = NULL;

    list_for_each(pos, &__log__file_head) {
        file = containing_record(pos, struct log_file_descriptor, file_link);
        if (0 == crt_strcasecmp(module, file->module)) {
            break;
        }
        file = NULL;
    }

    do {
        /* empty object, it means this module is a new one */
        if (!file) {
            if (NULL == (file = ztrymalloc(sizeof ( struct log_file_descriptor)))) {
                return NULL;
            }
            memset(file, 0, sizeof ( struct log_file_descriptor));
            file->fd = INVALID_FILE_DESCRIPTOR;
            list_add_tail(&file->file_link, &__log__file_head);
            break;
        }

        if ((int) file->fd < 0) {
            break;
        }

        /* If no date switch occurs and the file content record does not exceed the limit number of rows,
            the file is reused directly.  */
        if (file->timestamp.day == currst->day &&
                file->timestamp.month == currst->month &&
                file->timestamp.day == currst->day &&
                file->line_count_ < MAXIMUM_LOGFILE_LINE) {
            return file;
        }

        /* Switching the log file does not reclaim the log object pointer, just closing the file descriptor  */
        __close_log_file(file);
    } while (0);

    ifos_getpename(&pename);

    /* New or any form of file switching occurs in log posts  */
    crt_sprintf(name, cchof(name), "%s_%04u%02u%02u_%02u%02u%02u.log",
		module, currst->year, currst->month, currst->day, currst->hour, currst->minute, currst->second);
    crt_sprintf(path, cchof(path), "%s"POSIX__DIR_SYMBOL_STR"log"POSIX__DIR_SYMBOL_STR"%s"POSIX__DIR_SYMBOL_STR,
        abuff_raw(&__log_root_directory), abuff_raw(&pename) );
    ifos_pmkdir(path);
    crt_sprintf(path, cchof(path), "%s"POSIX__DIR_SYMBOL_STR"log"POSIX__DIR_SYMBOL_STR"%s"POSIX__DIR_SYMBOL_STR"%s",
        abuff_raw(&__log_root_directory), abuff_raw(&pename), name);
    retval = ifos_file_open(path, FF_RDACCESS | FF_WRACCESS | FF_CREATE_ALWAYS, 0644, &file->fd);
    if (retval >= 0) {
        memcpy(&file->timestamp, currst, sizeof ( datetime_t));
        crt_strcpy(file->module, cchof(file->module), module);
        file->line_count_ = 0;
    } else {
        /* If file creation fails, the linked list node needs to be removed  */
        list_del_init(&file->file_link);
        zfree(file);
        file = NULL;
    }

    return file;
}

static void __print_log_context(const char *module, enum log_levels level, int target, const datetime_t *currst, const char* logstr, int cb)
{
    struct log_file_descriptor *fileptr;

    if (target & kLogTarget_Filesystem) {
        lwp_mutex_lock(&__log_file_lock);
        fileptr = __attach_log_file(currst, module);
        lwp_mutex_unlock(&__log_file_lock);
        if (fileptr) {
            if (__write_log_file(fileptr, logstr, cb) < 0) {
                __close_log_file(fileptr);
            }
        }
    }

    if (target & kLogTarget_Stdout) {
        ifos_file_write(kLogLevel_Error == level ? STDERR_FILENO : STDOUT_FILENO, logstr, cb);
    }

    if (target & kLogTarget_Sysmesg) {
        ifos_syslog(logstr);
    }
}

static char *__format_log_string(enum log_levels level, int tid, const char *format, va_list ap, const datetime_t *currst, char *logstr, int cch)
{
    int pos, n, c;
    char *p;

    p = logstr;
    pos = 0;
	pos += crt_sprintf(&p[pos], cch - pos, "%02u:%02u:%02u %04u ", currst->hour, currst->minute, currst->second, (unsigned int)(currst->low / 10000));
    pos += crt_sprintf(&p[pos], cch - pos, "%s ", LOG__LEVEL_TXT[level]);
    pos += crt_sprintf(&p[pos], cch - pos, "%04X # ", tid);

    c = cch - pos - 1 - sizeof(POSIX__EOL);
	n = vsnprintf(&p[pos], c, format, ap);
#if _WIN32
	if (n < 0) {
#else
    if (n >= c) {
#endif
		pos = MAXIMUM_LOG_BUFFER_SIZE - 1 - sizeof(POSIX__EOL);
	} else {
		pos += n;
	}
    pos += crt_sprintf(&p[pos], cch - pos, "%s", POSIX__EOL);
	p[pos] = 0;

    return p;
}

static struct log_async_node *__get_log_cache()
{
    struct log_async_node *node;

    node = NULL;

    lwp_mutex_lock(&__log_async.lock);
    do {
        if (list_empty(&__log_async.idle)) {
            break;
        }

        node = list_first_entry(&__log_async.idle, struct log_async_node, link);
        assert(node);
        list_del_init(&node->link);
        /* do NOT add node into busy queue now, data are not ready.
        list_add_tail(&node->link, &__log_async.busy); */
    } while(0);
    lwp_mutex_unlock(&__log_async.lock);

    return node;
}

static void __recycle_log_cache(struct log_async_node *node)
{
    lwp_mutex_lock(&__log_async.lock);
    list_del_init(&node->link);
    list_add_tail(&node->link, &__log_async.idle);
    lwp_mutex_unlock(&__log_async.lock);
}

static void *__log_async_routine(void *argv)
{
    nsp_status_t status;

    while (1) {
        status = lwp_event_wait(&__log_async.notify, 10 * 1000);
        if (!NSP_SUCCESS_OR_ERROR_EQUAL(status, ETIMEDOUT)) {
            break;
        }
        log_flush();
    }

    ifos_syslog("nsplog asynchronous thread has been terminated.");
    return NULL;
}

static nsp_status_t __init_log_async_pattern()
{
    int misc_memory_size;
    int i;
    nsp_status_t status;

    __log_async.pending = 0;
    misc_memory_size = MAXIMUM_LOGSAVE_COUNT * sizeof(struct log_async_node);

    __log_async.misc_memory = (struct log_async_node *)ztrymalloc(misc_memory_size);
    if (unlikely(!__log_async.misc_memory)) {
        return posix__makeerror(ENOMEM);
    }
    memset(__log_async.misc_memory, 0, misc_memory_size);

    INIT_LIST_HEAD(&__log_async.idle);
    INIT_LIST_HEAD(&__log_async.busy);

    lwp_mutex_init(&__log_async.lock, YES);
    lwp_event_init(&__log_async.notify, LWPEC_NOTIFY);

    /* all miscellaneous memory nodes are inital adding to free list */
    for (i = 0; i < MAXIMUM_LOGSAVE_COUNT; i++) {
        list_add_tail(&__log_async.misc_memory[i].link, &__log_async.idle);
    }

    status = lwp_create(&__log_async.async_thread, 0, &__log_async_routine, NULL);
    if ( unlikely(!NSP_SUCCESS(status))) {
        lwp_mutex_uninit(&__log_async.lock);
        lwp_event_uninit(&__log_async.notify);
        zfree(__log_async.misc_memory);
        __log_async.misc_memory = NULL;
        return NSP_STATUS_FATAL;
    }

    return NSP_STATUS_SUCCESSFUL;
}

static void __change_log_dir(const char *rootdir)
{
	size_t pos, i;
    nsp_status_t status;

    if (0 != abuff_raw(&__log_root_directory)[0]) {
        return;
    }

    if (!rootdir) {
        ifos_getpedir(&__log_root_directory);
        return;
	}

    abuff_strcpy(&__log_root_directory, rootdir);
    pos = strlen(abuff_raw(&__log_root_directory));
    for (i = pos - 1; i >= 0; i--) {
        if (POSIX__DIR_SYMBOL == abuff_raw(&__log_root_directory)[i]) {
            abuff_raw(&__log_root_directory)[i] = 0;
        } else {
            break;
        }
    }

    status = ifos_pmkdir(abuff_raw(&__log_root_directory));
    if ( unlikely( !NSP_SUCCESS(status) ) ) {
        ifos_getpedir(&__log_root_directory);
    }
}

static void __log_init()
{
    nsp_status_t status;

    /* initial global context */
    lwp_mutex_init(&__log_file_lock, YES);

    /* allocate the async node pool */
    status = __init_log_async_pattern();
    if ( unlikely( !NSP_SUCCESS(status) ) ) {
        lwp_mutex_uninit(&__log_file_lock);
    }
}

static lwp_once_t once = LWP_ONCE_INIT;

PORTABLEIMPL(void) log_init()
{
	__change_log_dir(NULL);
    lwp_once(&once, __log_init);
}

PORTABLEIMPL(void) log_init2(const char *rootdir)
{
    __change_log_dir(rootdir);
	log_init();
}

PORTABLEIMPL(void) log_write(const char *module, enum log_levels level, int target, const char *format, ...)
{
    va_list ap;
    char logstr[MAXIMUM_LOG_BUFFER_SIZE];
    datetime_t currst;
    ifos_path_buffer_t pename;
    nsp_status_t status;

    lwp_once(&once, __log_init);

    if ( unlikely(!format || level >= kLogLevel_Maximum || level < 0) ) {
        return;
    }

    clock_systime(&currst);

    va_start(ap, format);
    __format_log_string(level, ifos_gettid(), format, ap, &currst, logstr, cchof(logstr));
    va_end(ap);

    if (!module) {
        status = ifos_getpename(&pename);
        if ( NSP_SUCCESS(status) ) {
            __print_log_context(abuff_raw(&pename), level, target, &currst, logstr, (int) strlen(logstr));
        }
    } else {
        __print_log_context(module, level, target, &currst, logstr, (int) strlen(logstr));
    }
}

PORTABLEIMPL(void) log_save(const char *module, enum log_levels level, int target, const char *format, ...)
{
    va_list ap;
    struct log_async_node *node;
    datetime_t currst;
    int pending;
    ifos_path_buffer_t pename;
    nsp_status_t status;

    lwp_once(&once, __log_init);

    if ( unlikely(!format || level >= kLogLevel_Maximum || level < 0) ) {
        return;
    }

    clock_systime(&currst);

    /* securt check for the maximum pending amount */
    pending = atom_addone(&__log_async.pending);
    if ( pending >= MAXIMUM_LOGSAVE_COUNT) {
        atom_subone(&__log_async.pending);
        return;
    }

    /* increase the warning status to @danger */
    if (pending >= WARNING_LOGSAVE_COUNT && LOGSAVE_PENDING_SAFE == atom_get(&__log_async.warning) ) {
        atom_set(&__log_async.warning, LOGSAVE_PENDING_DANGER);
        ifos_syslog("nshost asynchronous journal discard warning!");
    }

    /* hash node at index of memory block */
    if ( NULL == (node = __get_log_cache()) ) {
        return;
    }
    node->target = target;
    node->level = level;
    memcpy(&node->timestamp, &currst, sizeof(currst));

    if (module) {
        crt_strcpy(node->module, cchof(node->module), module);
    } else {
        status = ifos_getpename(&pename);
        if ( NSP_SUCCESS(status) ) {
            crt_strcpy(node->module, sizeof(node->module), abuff_raw(&pename));
        }
    }

    va_start(ap, format);
    __format_log_string(node->level, ifos_gettid(), format, ap, &node->timestamp, node->logstr, sizeof (node->logstr));
    va_end(ap);

    lwp_mutex_lock(&__log_async.lock);
    list_add_tail(&node->link, &__log_async.busy);
    lwp_mutex_unlock(&__log_async.lock);
    lwp_event_awaken(&__log_async.notify);
}

PORTABLEIMPL(void) log_flush()
{
    struct log_async_node *node;

    do {
        node = NULL;

        lwp_mutex_lock(&__log_async.lock);
        if (!list_empty(&__log_async.busy)) {
            node = list_first_entry(&__log_async.busy, struct log_async_node, link);
            assert(node);
        }
        lwp_mutex_unlock(&__log_async.lock);

        if (node) {
            __print_log_context(node->module, node->level, node->target, &node->timestamp, node->logstr, (int) strlen(node->logstr));
            /* recycle node from busy queue to idle list */
            __recycle_log_cache(node);
            /* reduce the pending count and deduce the warning statues to @safe*/
            if (atom_subone(&__log_async.pending) < SAFE_LOGSAVE_COUNT && LOGSAVE_PENDING_DANGER == atom_get(&__log_async.warning) ) {
                atom_set(&__log_async.warning, LOGSAVE_PENDING_SAFE);
                ifos_syslog("nshost asynchronous journal discard warning relieve");
            }
        }
    } while (node);
}
