#if !defined MESSAGE_QUEUE_WRAPPER_H
#define MESSAGE_QUEUE_WRAPPER_H

#if _WIN32
#pragma error("mesgq is not supported on Windows platform.")
#endif

#include "compiler.h"
#include <mqueue.h>

/* mq_setattr(3)
 * The only attribute that can be modified is the setting of the  O_NONBLOCK  flag  in  mq_flags.
 * so, msgsize and maxmsg filed MUST be set when the first time open the MQ object
 */

enum mesgq_open_method
{
    MESGQ_UNKNOWN = 0,
    MESGQ_OPEN_EXISTING,
    MESGQ_OPEN_ALWAYS,
    MESGQ_CREATE_NEW,
    MESGQ_CREATE_ALWAYS
};

PORTABLEAPI(nsp_status_t) mesgq_open(const char *name, enum mesgq_open_method method, long maxmsg, long msgsize, mqd_t *mqfd);
PORTABLEAPI(void) mesgq_close(mqd_t mqfd);
PORTABLEAPI(void) mesgq_unlink(const char *name);

PORTABLEAPI(nsp_status_t) mesgq_set_nonblocking(mqd_t mqfd);
PORTABLEAPI(nsp_status_t) mesgq_cancel_nonblocking(mqd_t mqfd);

/* calling thread shall specify a null pointer to ignore the return.
    @curmsgs use to check the current message count in mq, zero indicate a empty queue */
PORTABLEAPI(nsp_status_t) mesgq_getattr(mqd_t mqfd, long *maxmsg, long *msgsize, long *curmsgs);

/* timeout unit is milliseconds
    calling thread can specify 0 or negative value to timeout to ignore this option */
PORTABLEAPI(nsp_status_t) mesgq_sendmsg(mqd_t mqfd, const char *msg, size_t msg_len, unsigned int prio, int timeout);
PORTABLEAPI(int) mesgq_recvmsg(mqd_t mqfd, char *msg, size_t msg_len, unsigned int *prio, int timeout);

#endif // MESSAGE_QUEUE_WRAPPER_H
