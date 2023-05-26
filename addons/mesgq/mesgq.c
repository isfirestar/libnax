#include "mesgq.h"

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

nsp_status_t mesgq_open(const char *name, enum mesgq_open_method method, long maxmsg, long msgsize, mqd_t *mqfd)
{
    struct mq_attr attr;

    if (!name || !mqfd ) {
        return posix__makeerror(EINVAL);
    }

    if (MESGQ_OPEN_EXISTING != method) {
        if (maxmsg <= 0) {
            maxmsg = 10;
        }

        if (msgsize <= 0) {
            msgsize = 1024;
        }

        attr.mq_maxmsg = maxmsg;
        attr.mq_msgsize = msgsize;
    }

    switch (method) {
        case MESGQ_OPEN_EXISTING:
            *mqfd = mq_open(name, O_RDWR);
            break;
        case MESGQ_CREATE_NEW:
            *mqfd = mq_open(name, O_RDWR | O_CREAT | O_EXCL, 0600, &attr);
            break;
        case MESGQ_CREATE_ALWAYS:
            *mqfd = mq_open(name, O_RDWR | O_CREAT | O_TRUNC, 0600, &attr);
            break;
        default:
            return posix__makeerror(EINVAL);
    }

    if (*mqfd == (mqd_t)-1) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

void mesgq_close(mqd_t mqfd)
{
    if (mqfd != (mqd_t)-1) {
        mq_close(mqfd);
    }
}

/* set the O_NONBLOCK flag of the message queue descriptor */
nsp_status_t mesgq_set_nonblocking(mqd_t mqfd)
{
    struct mq_attr attr;

    if (mq_getattr(mqfd, &attr) == -1) {
        return posix__makeerror(errno);
    }

    attr.mq_flags |= O_NONBLOCK;
    if (mq_setattr(mqfd, &attr, NULL) == -1) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t mesgq_cancel_nonblocking(mqd_t mqfd)
{
    struct mq_attr attr;

    if (mq_getattr(mqfd, &attr) == -1) {
        return posix__makeerror(errno);
    }

    attr.mq_flags &= ~O_NONBLOCK;
    if (mq_setattr(mqfd, &attr, NULL) == -1) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

/* update size parameters of message queue which definied in /proc/sys/fs/mqueue/**** 
    calling thread can specify either @maxmsg or @msgsize to a negative number or zero to keep the current settings */
nsp_status_t mesgq_setattr(mqd_t mqfd, long maxmsg, long msgsize)
{
    struct mq_attr attr;

    if (mq_getattr(mqfd, &attr) == -1) {
        return posix__makeerror(errno);
    }

    if (maxmsg > 0) {
        attr.mq_maxmsg = maxmsg;
    }

    if (msgsize > 0) {
        attr.mq_msgsize = msgsize;
    }

    if (mq_setattr(mqfd, &attr, NULL) == -1) {
        return posix__makeerror(errno);
    }

    return NSP_STATUS_SUCCESSFUL;
}

/* calling thread shall specify a null pointer to ignore the return */
nsp_status_t mesgq_getattr(mqd_t mqfd, long *maxmsg, long *msgsize, long *curmsgs)
{
    struct mq_attr attr;

    if (mq_getattr(mqfd, &attr) == -1) {
        return posix__makeerror(errno);
    }

    if (maxmsg) {
        *maxmsg = attr.mq_maxmsg;
    }

    if (msgsize) {
        *msgsize = attr.mq_msgsize;
    }

    if (curmsgs) {
        *curmsgs = attr.mq_curmsgs;
    }

    return NSP_STATUS_SUCCESSFUL;
}

/* timeout unit is milliseconds
    calling thread can specify 0 or negative value to timeout to ignore this option */
int mesgq_sendmsg(mqd_t mqfd, const char *msg, size_t msg_len, unsigned int prio, int timeout)
{
    int sent;
    struct timespec ts;

    if (!msg || !msg_len) {
        return posix__makeerror(EINVAL);
    }

    if (timeout > 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout / 1000;
        ts.tv_nsec += (timeout % 1000) * 1000000;
        sent = mq_timedsend(mqfd, msg, msg_len, prio, &ts);
    } else {
        sent = mq_send(mqfd, msg, msg_len, prio);
    }

    if (sent == -1) {
        if (EAGAIN == errno) {
            return EAGAIN;
        }
        return posix__makeerror(errno);
    }

    return sent;
}

int mesgq_recvmsg(mqd_t mqfd, char *msg, size_t msg_len, unsigned int *prio, int timeout)
{
    int received;
    struct timespec ts;

    if (!msg || !msg_len) {
        return posix__makeerror(EINVAL);
    }

    if (timeout > 0) {
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout / 1000;
        ts.tv_nsec += (timeout % 1000) * 1000000;
        received = mq_timedreceive(mqfd, msg, msg_len, prio, &ts);
    } else {
        received = mq_receive(mqfd, msg, msg_len, prio);
    }
    
    if (received == -1) {
        if (EAGAIN == errno) {
            return EAGAIN;
        }
        return posix__makeerror(errno);
    }

    return received;
}
