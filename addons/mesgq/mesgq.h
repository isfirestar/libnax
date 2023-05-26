#if !defined MESSAGE_QUEUE_WRAPPER_H
#define MESSAGE_QUEUE_WRAPPER_H

#if _WIN32
#pragma error("mesgq is not supported on Windows platform.")
#endif

#include "compiler.h"
#include <mqueue.h>

enum mesgq_open_method
{
    MESGQ_UNKNOWN = 0,
    MESGQ_OPEN_EXISTING,
    MESGQ_CREATE_NEW,
    MESGQ_CREATE_ALWAYS
};

nsp_status_t mesgq_open(const char *name, enum mesgq_open_method method, long maxmsg, long msgsize, mqd_t *mqfd);
void mesgq_close(mqd_t mqfd);

nsp_status_t mesgq_set_nonblocking(mqd_t mqfd);
nsp_status_t mesgq_cancel_nonblocking(mqd_t mqfd);

/* update size parameters of message queue which definied in /proc/sys/fs/mqueue/**** 
    calling thread can specify either @maxmsg or @msgsize to a negative number or 0 to keep the current settings */
nsp_status_t mesgq_setattr(mqd_t mqfd, long maxmsg, long msgsize);
/* calling thread shall specify a null pointer to ignore the return.
    @curmsgs use to check the current message count in mq, zero indicate a empty queue */
nsp_status_t mesgq_getattr(mqd_t mqfd, long *maxmsg, long *msgsize, long *curmsgs);

/* timeout unit is milliseconds
    calling thread can specify 0 or negative value to timeout to ignore this option */
int mesgq_sendmsg(mqd_t mqfd, const char *msg, size_t msg_len, unsigned int prio, int timeout);
int mesgq_recvmsg(mqd_t mqfd, char *msg, size_t msg_len, unsigned int *prio, int timeout);

#endif // MESSAGE_QUEUE_WRAPPER_H
