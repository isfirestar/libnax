#if !defined LOGGER_H
#define LOGGER_H

#include "compiler.h"

/*
 *	bug:
 *	1. Log file switching rows are 5000 lines with a minimum switching interval of 1 second.
 *		If more than 5000 rows of logs are continuously output in one second, the log information will be saved in the same log file
 */

enum log_levels {
    kLogLevel_Info = 0,
    kLogLevel_Warning,
    kLogLevel_Error,
    kLogLevel_Fatal,
    kLogLevel_Trace,
    kLogLevel_Maximum,
};

#define kLogTarget_Filesystem   (1)
#define kLogTarget_Stdout       (2)
#define	kLogTarget_Sysmesg      (4)

PORTABLEAPI(void) log_init();
PORTABLEAPI(void) log_init2(const char *rootdir);
PORTABLEAPI(void) log_write(const char *module, enum log_levels level, int target, const char *format, ...);
PORTABLEAPI(void) log_save(const char *module, enum log_levels level, int target, const char *format, ...);
PORTABLEAPI(void) log_flush();
PORTABLEIMPL(void) log_generical_print(const char *file, int line, const char *func, const char *fmt, ...);

#if _WIN32
#define nprint(fmt, arg...) log_generical_print(__FILE__, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__)
#else
#define nprint(fmt, arg...) log_generical_print(__FILE__, __FUNCTION__, __LINE__, fmt, ##arg)
#endif

/* Maximum allowable specified log module name length */
#define  LOG_MODULE_NAME_LEN   (128)

/* Maximum allowable single log write data length  */
#define  MAXIMUM_LOG_BUFFER_SIZE  (2048)

#if _WIN32
#define log_info(module, fmt, ...) log_save(module, kLogLevel_Info, kLogTarget_Stdout | kLogTarget_Filesystem, fmt, ##__VA_ARGS__)
#define log_alert(module, fmt, ...) log_save(module, kLogLevel_Warning, kLogTarget_Stdout | kLogTarget_Filesystem, fmt, ##__VA_ARGS__)
#define log_error(module, fmt, ...) log_save(module, kLogLevel_Error, kLogTarget_Stdout | kLogTarget_Filesystem, fmt, ##__VA_ARGS__)
#define log_trace(module, fmt, ...) log_save(module, kLogLevel_Error, kLogTarget_Filesystem, fmt, ##__VA_ARGS__)
#define nprint(fmt, ...) log_write(NULL,  kLogLevel_Info, kLogTarget_Stdout, fmt, ##__VA_ARGS__)
#else
#define log_info(module, fmt, arg...) log_save(module, kLogLevel_Info, kLogTarget_Stdout | kLogTarget_Filesystem, fmt, ##arg)
#define log_alert(module, fmt, arg...) log_save(module, kLogLevel_Warning, kLogTarget_Stdout | kLogTarget_Filesystem, fmt, ##arg)
#define log_error(module, fmt, arg...) log_save(module, kLogLevel_Error, kLogTarget_Stdout | kLogTarget_Filesystem, fmt, ##arg)
#define log_trace(module, fmt, arg...) log_save(module, kLogLevel_Trace, kLogTarget_Filesystem, fmt, ##arg)
#endif

#endif
