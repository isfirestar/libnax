#if !defined OSGISM_PRINT_H
#define OSGISM_PRINT_H

#include <ctype.h>          /* size_t */
#include <sys/types.h>      /* size_t */

#include "nisdef.h"

extern void generical_print(const char *file, int line, const char *func, const char *fmt, ...);

/* everywhere are very welcome to use this macro to instead of printf(3) */
// #if !DEBUG
//     #define print(fmt, arg...)
// #else
    #define print(fmt, arg...)  generical_print(__FILE__, __LINE__, __FUNCTION__, fmt, ##arg)
// #endif


#if 0
#include "logger.h"

#define STR(s)  #s

#if defined _MY_MOUDLE_NAME
#if defined print
#undef print
#endif

#define MODULE_NAME   STR(_MY_MOUDLE_NAME)
#define print(fmt, arg...) log_save(_MY_MOUDLE_NAME, kLogLevel_Info, kLogTarget_Stdout | kLogTarget_Filesystem, fmt, ##arg)
#endif
#endif

/* @strsafeformat proc using secure function vsnprintf(3) to format c-style string
 *  parameter @target indicate the target string which calling thread want to format.
 *  parameter @remainlen specified the remian length of @target, this length include null-terminte character '\0' in bytes
 *  parameter @acculen used to save acculative characters that have been copied into @target, it can be NULL
 *  parameter @acquire set on failure to indicate how much bytes of length that the input buffer should own. it can be NULL.
 *  function return 0 on success, -1 on otherwise */
extern int strsafeformat(char *target, size_t remainlen, int *acculen, int *acquirelen, const char *format, ...);
#define sprintf_s(target, bufflen, acculen, acqulen, fmt, args...)   strsafeformat(target, bufflen, acculen, acqulen, fmt, ##args)

extern int strsplit_endpoint(const char *endpoint, abuff_ddn_ipv4_t *ipaddr, unsigned short *port);
extern void strdumphex(const char *message, ssize_t msgsize);

#endif /* !OSGISM_PRINT_H */
