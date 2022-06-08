#if !defined POSIX_STRING_H
#define POSIX_STRING_H

#include "compiler.h"

#include <stdarg.h>

/* [man 3 strncpy] The strncpy() function is similar, except that at most n bytes of src are copied.
	Warning: If there is no null byte among the first n bytes of src, the string placed in dest will not be null-terminated.

	[C standard]  If count is reached before the entire array src was copied, the resulting character array is not null-terminated.
 If, after copying the terminating null character from src, count is not reached,
 additional null characters are written to dest until the total of count characters have been written.*/

#if _WIN32

#define crt_strncpy(dest, maxlen, src, n) strncpy_s(dest, maxlen, src, n)
#define crt_wcsncpy(dest, maxlen, src, n) wcsncpy_s(dest, maxlen, src, n)
#define crt_strcpy(dest, maxlen, src)	strcpy_s(dest, maxlen, src)
#define crt_wcscpy(dest, maxlen, src)	wcscpy_s(dest, maxlen, src)
#define crt_strcat(dest, maxlen, src)	strcat_s(dest, maxlen, src)
#define crt_wcscat(dest, maxlen, src)	wcscat_s(dest, maxlen, src)
#define crt_vsnprintf(str, maxlen, format, ap)	vsnprintf_s(str, maxlen, _TRUNCATE, format, ap);
#define crt_vsnwprintf(wcs, maxlen, format, arg) _vsnwprintf_s(wcs, maxlen, _TRUNCATE, format, arg);
#define crt_vsprintf(str, maxlen, format, ap)	vsprintf_s(str, maxlen, format, ap)
#define crt_vswprintf(wcs, maxlen, format, arg) vswprintf_s(wcs, maxlen, format, arg)
#define crt_sprintf(str, maxlen, format, ...) sprintf_s(str, maxlen, format, ##__VA_ARGS__)
#define crt_swprintf(wcs, maxlen, format, ...) swprintf_s(wcs, maxlen, format, ##__VA_ARGS__)
#define crt_strcasecmp(s1, s2) _stricmp(s1, s2)
#define crt_wcscasecmp(s1, s2) _wcsicmp(s1, s2)
#define crt_strncasecmp(s1, s2, n) _strnicmp(s1, s2, n)
#define crt_wcsncasecmp(s1, s2, n) _wcsnicmp(s1, s2, n)
#define crt_strtok(str, delim, saveptr) strtok_s(str, delim, saveptr)
#define crt_wcstok(wcs, delim, saveptr) wcstok_s(wcs, delim, saveptr)
#define crt_strdup(s) _strdup(s)
#define crt_wcsdup(s) _wcsdup(s)
#define crt_strrev(src) _strrev(src)

#else

#define crt_strncpy(dest, maxlen, src, n) strncpy(dest, src, n)
#define crt_wcsncpy(dest, maxlen, src, n) wcsncpy(dest, src, n)
#define crt_strcpy(dest, maxlen, src)	do { \
    dest[(maxlen) - 1] = 0;   \
    strncpy(dest, src, (maxlen) - 1); } while (0)
#define crt_wcscpy(dest, maxlen, src)	wcscpy(dest, src)
#define crt_strcat(dest, maxlen, src)	strcat(dest, src)
#define crt_wcscat(dest, maxlen, src)	wcscat(dest, src)
#define crt_vsnprintf(str, maxlen, format, ap)	vsnprintf(str, maxlen, format, ap)
#define crt_vsnwprintf(wcs, maxlen, format, arg) vswprintf(wcs, maxlen, format, arg)
#define crt_vsprintf(str, maxlen, format, ap)	vsnprintf(str, maxlen, format, ap)
#define crt_vswprintf(wcs, maxlen, format, arg) vswprintf(wcs, maxlen, format, arg)
#define crt_sprintf(str, maxlen, format, arg...) snprintf(str, maxlen - 1, format, ##arg)
#define crt_swprintf(wcs, maxlen, format, arg...) swprintf(wcs, maxlen, format, ##arg)
#define crt_strcasecmp(s1, s2) strcasecmp(s1, s2)
#define crt_wcscasecmp(s1, s2) wcscasecmp(s1, s2)
#define crt_strncasecmp(s1, s2, n) strncasecmp(s1, s2, n)
#define crt_wcsncasecmp(s1, s2, n) wcsncasecmp(s1, s2, n)
#define crt_strtok(str, delim, saveptr) strtok_r(str, delim, saveptr)
#define crt_wcstok(wcs, delim, saveptr) wcstok(wcs, delim, saveptr)
#define crt_strdup(s) strdup(s) /* -D_POSIX_C_SOURCE >= 200809L */
#define crt_wcsdup(s) wcsdup(s)
PORTABLEAPI(char *) crt_strrev(char *src);

#endif /* _WIN32 */

#define crt_strcmp(s1, s2) strcmp(s1, s2)
#define crt_wcscmp(s1, s2) wcscmp(s1, s2)

#if __cplusplus
#define abuff_type(n) struct {   \
    union { \
        char st[n]; \
        unsigned char ust[n];   \
        const char cst[n];  \
        const unsigned char cust[n]; \
    } u = { 0 };  \
}
#else
/* below context are pre-define buffer with fix length for particular usage */
#define abuff_type(n) struct {   \
    union { \
        char st[n]; \
        unsigned char ust[n];   \
        const char cst[n];  \
        const unsigned char cust[n]; \
    };  \
} /* aligned_##n##bytes_t */
#endif

#define abuff_raw(abuff)    ((char *)(&(abuff)->st[0]))
#define abuff_craw(abuff)   ((const char *)(&(abuff)->cst[0]))
#define abuff_uchar(abuff)   ((unsigned char *)(&(abuff)->ust[0]))
#define abuff_cuchar(abuff)  ((const unsigned char *)(&(abuff)->cust[0]))
#define abuff_size(abuff)   (sizeof(*(abuff)))

#define abuff_strcpy(abuff, src) strncpy((abuff)->st, src, abuff_size((abuff)) - 1)
#define abuff_strncpy(abuff, src, n) strncpy((abuff)->st, src, min(n, abuff_size((abuff)) - 1))

#if _WIN32
#define abuff_sprintf(abuff, format, ...) snprintf((abuff)->st, (abuff_size((abuff)) - 1), format, ##__VA_ARGS__)
#define abuff_vsprintf(abuff, format, ap)  vsprintf_s((abuff)->st, (abuff_size((abuff)) - 1), format, ap)
#define abuff_vsnprintf(abuff, format, ap) vsnprintf_s((abuff)->st, (abuff_size((abuff)) - 1), _TRUNCATE, format, ap);
#else
#define abuff_sprintf(abuff, format, arg...) snprintf((abuff)->st, (abuff_size((abuff)) - 1), format, ##arg)
#define abuff_vsprintf(abuff, format, ap)  vsnprintf((abuff)->st, (abuff_size((abuff)) - 1), format, ap)
#define abuff_vsnprintf(abuff, format, ap) vsnprintf((abuff)->st, (abuff_size((abuff)) - 1), format, ap)
#endif

typedef abuff_type(8)     abuff_8_t;
typedef abuff_type(16)    abuff_16_t;
typedef abuff_type(32)    abuff_32_t;
typedef abuff_type(64)    abuff_64_t;
typedef abuff_type(128)   abuff_128_t;
typedef abuff_type(256)   abuff_256_t;
typedef abuff_type(512)   abuff_512_t;
typedef abuff_type(1024)  abuff_1024_t;
typedef abuff_type(2048)  abuff_2048_t;
typedef abuff_type(4096)  abuff_4096_t;
typedef abuff_type(8192)  abuff_8192_t;

/* raw string RunTime operation implementation without libc */
PORTABLEAPI(size_t) rtl_strlen(const char *s);
PORTABLEAPI(char *) rtl_strcpy(char *dest, const char *src);
PORTABLEAPI(char *) rtl_strncpy(char *dest, const char *src, size_t n);
//__attribute_pure__ __nonnull ((1, 2))
PORTABLEAPI(int) rtl_strcmp(const char *s1, const char *s2);
PORTABLEAPI(int) rtl_strncmp(const char *s1, const char *s2, size_t n);
PORTABLEAPI(int) rtl_strcasecmp(const char *s1, const char *s2);
PORTABLEAPI(int) rtl_strncasecmp(const char *s1, const char *s2, size_t n);
PORTABLEAPI(char *) rtl_strcat(char *dest, const char *src);
PORTABLEAPI(char *) rtl_strncat(char *dest, const char *src, size_t n);
PORTABLEAPI(char *) rtl_strtok(char *str, const char *delim, char **saveptr);
//__attribute_pure__ __nonnull ((1, 2))
PORTABLEAPI(char *) rtl_strdup(const char *s, void *(*alloc)(size_t));
//__attribute_pure__ __nonnull ((1, 2))
PORTABLEAPI(char *) rtl_strndup(const char *s, size_t n, void *(*alloc)(size_t));
PORTABLEAPI(void *) rtl_memcpy(void *dest, const void *src, size_t n);

#endif /* !POSIX_STRING_H */
