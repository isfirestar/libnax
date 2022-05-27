#include "print.h"

#include <stdarg.h>         /* va_arg */
#include <string.h>         /* snprintf */
#include <stdio.h>          /* printf */
#include <errno.h>          /* EINVAL */
#include <stdlib.h>         /* atoi */

void generical_print(const char *file, int line, const char *func, const char *fmt, ...)
{
    char buffer[1024];
    int pos;
    va_list ap;
    int written;

    pos = 0;
    if (file && (pos < sizeof(buffer))) {
        written = snprintf(buffer + pos, sizeof(buffer) - pos, "[%s:%d]", file, line);
        if (written >= (sizeof(buffer) - pos)) {
            return;
        }
        pos += written;
    }

    if (func && (pos < sizeof(buffer))) {
        written = snprintf(buffer + pos, sizeof(buffer) - pos, "[%s] ", func);
        if (written >= (sizeof(buffer) - pos)) {
            return;
        }
        pos += written;
    }

    if (fmt && (pos < sizeof(buffer) - 4)) {
        va_start(ap, fmt);
        written = vsnprintf(buffer + pos, sizeof(buffer) - pos - 4, fmt, ap);
        if (written >= (sizeof(buffer) - pos - 4)) {
            return;
        }
        pos += written;
        va_end(ap);
    }

    printf("%s\n", buffer);
}

void strdumphex(const char *message, ssize_t msgsize)
{
    ssize_t i, n, off;
    char showmsg[4196];
    int acculen, acquirelen;

    i = n = off = 0;

    /* echo entire message body */
    for (i = 0; i < msgsize; i++) {
        if ( (n = strsafeformat(&showmsg[off], sizeof(showmsg) - off, &acculen, &acquirelen, " \\x%02x ", message[i])) < 0 ) {
            break;
        }
        off += n;
    }
    print("%s", showmsg);
}

int strsafeformat(char *target, size_t remainlen, int *acculen, int *acquirelen, const char *format, ...)
{
    int fmtlen;
    va_list ap;

    va_start(ap, format);
    fmtlen = vsnprintf(target, remainlen, format, ap);
    va_end(ap);

    if (fmtlen >= remainlen) {
        if (acquirelen) {
            *acquirelen = fmtlen;
        }
        return -1;
    }

    if (acculen) {
        *acculen += fmtlen;
    }
    return 0;
}

int strsplit_endpoint(const char *endpoint, abuff_ddn_ipv4_t *ipaddr, unsigned short *port)
{
    char *chr;

    if (!endpoint || !ipaddr || !port) {
        return -EINVAL;
    }

    /* prototype like : "" */
    if (0 == endpoint) {
        abuff_strcpy(ipaddr, "0.0.0.0");
        *port = 0;
        return 0;
    }

    chr = strchr(endpoint, ':');
    if (!chr) {
        abuff_strcpy(ipaddr, endpoint);
        *port = 0;
    } else {
        *port = atoi(chr + 1);
        /* prototype like : ":12345" */
        if (chr == endpoint) {
            abuff_strcpy(ipaddr, "0.0.0.0");
        } else {
            abuff_strncpy(ipaddr, endpoint, chr - endpoint);
        }
    }
    return 0;
}
