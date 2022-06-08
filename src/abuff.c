#include "abuff.h"

#if !_WIN32

PORTABLEIMPL(char *) crt_strrev(char *src)
{
    char* h = src;
    char* t = src;
    char ch;

    if (unlikely(!src)) {
        return NULL;
    }

    while (*t++) {
        ;
    };

    t--;
    t--;

    while (h < t) {
        ch = *h;
        *h++ = *t;
        *t-- = ch;
    }
    return (src);
}
#endif
