#include "abuff.h"

PORTABLEIMPL(size_t) rtl_strlen(const char *s)
{
    size_t n;

    n = 0;
    while (*s++) {
        n++;
    }

    return n;
}

PORTABLEIMPL(char *) rtl_strcpy(char *dest, const char *src)
{
    size_t i;

    for (i = 0; src[i] != '\0'; i++) {
        dest[i] = src[i];
    }

    dest[i] = '\0';
    return dest;
}

PORTABLEIMPL(char *) rtl_strncpy(char *dest, const char *src, size_t n)
{
    size_t i;

   for (i = 0; i < n && src[i] != '\0'; i++) {
       dest[i] = src[i];
   }

   for ( ; i < n; i++) {
       dest[i] = '\0';
   }

   return dest;
}

PORTABLEIMPL(int) rtl_strcmp(const char *s1, const char *s2)
{
    while (*s1 && *s2) {
        if (*s1 < *s2) {
            return -1;
        }

        if (*s1 > *s2) {
            return 1;
        }

        s1++;
        s2++;
    }

    return ( (*s1 < *s2) ? -1 : ((*s1 > *s2) ? 1 : 0) );
}

PORTABLEIMPL(int) rtl_strncmp(const char *s1, const char *s2, size_t n)
{
    size_t i;

    i = 0;
    while (*s1 && *s2 && i < n) {
        if (*s1 < *s2) {
            return -1;
        }

        if (*s1 > *s2) {
            return 1;
        }

        s1++;
        s2++;
        i++;
    }

    return ( (*s1 < *s2) ? -1 : ((*s1 > *s2) ? 1 : 0) );
}

PORTABLEIMPL(int) rtl_strcasecmp(const char *s1, const char *s2)
{
    char c1, c2;

    while (*s1 && *s2) {
        c1 = *s1;
        c2 = *s2;

        if (c1 >= 'A' && c1 <= 'Z') {
            c1 += 0x20;
        }

        if (c2 >= 'A' && c2 <= 'Z') {
            c2 += 0x20;
        }

        if (c1 < c2) {
            return -1;
        }

        if (c1 > c2) {
            return 1;
        }

        s1++;
        s2++;
    }

    return ( (*s1 < *s2) ? -1 : ((*s1 > *s2) ? 1 : 0) );
}

PORTABLEIMPL(int) rtl_strncasecmp(const char *s1, const char *s2, size_t n)
{
    char c1, c2;
    size_t i;

    i = 0;
    while (*s1 && *s2 && i < n) {
        c1 = *s1;
        c2 = *s2;

        if (c1 >= 'A' && c1 <= 'Z') {
            c1 += 0x20;
        }

        if (c2 >= 'A' && c2 <= 'Z') {
            c2 += 0x20;
        }

        if (c1 < c2) {
            return -1;
        }

        if (c1 > c2) {
            return 1;
        }

        s1++;
        s2++;
        i++;
    }

    return 0;
}

PORTABLEIMPL(char *) rtl_strdup(const char *s, void *(*alloc)(size_t))
{
    char *dup;
    size_t n;

    n = rtl_strlen(s);
    dup = (char *)alloc(n + 1);
    if (dup) {
        rtl_strcpy(dup, s);
    }

    return dup;
}

PORTABLEIMPL(char *) rtl_strndup(const char *s, size_t n, void *(*alloc)(size_t))
{
    char *dup;
    size_t k, l;

    l = rtl_strlen(s);
    k = l > n ? n : l;

    dup = (char *)alloc(k + 1);
    if (dup) {
        rtl_strcpy(dup, s);
    }

    return dup;
}

PORTABLEIMPL(void *) rtl_memcpy(void *dest, const void *src, size_t n)
{
    size_t r;
    long *pld, *pls;
    unsigned char *pbd, *pbs;

    r = n;
    pld = (long *)dest;
    pls = (long *)src;
    while (r > sizeof(long)) {
        *pld = *pls;
        ++pld;
        ++pls;
        r -= sizeof(long);
    }

    pbd = (unsigned char *)pld;
    pbs = (unsigned char *)pls;
    while (r > 0) {
        *pbd = *pbs;
        ++pbd;
        ++pbs;
        --r;
    }

    return dest;
}
