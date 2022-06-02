#include "abuff.h"

PORTABLEIMPL(char *) crt_strrev(char *src)
{
#if _WIN32
    if (unlikely(!src)) {
        return NULL;
    }
    return _strrev(src);
#else
    /* h指向s的头部 */
    char* h = src;
    char* t = src;
    char ch;

    if (unlikely(!src)) {
        return NULL;
    }

    /* t指向s的尾部 */
    while (*t++) {
        ;
    };

    t--; /* 与t++抵消 */
    t--; /* 回跳过结束符'\0' */

    /* 当h和t未重合时，交换它们所指向的字符 */
    while (h < t) {
        ch = *h;
        *h++ = *t; /* h向尾部移动 */
        *t-- = ch; /* t向头部移动 */
    }
    return (src);
#endif
}
