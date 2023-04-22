#ifndef IO_H_20170118
#define IO_H_20170118

#include "compiler.h"

/*
 *  Kernel IO Events and internal scheduling
 *  related to EPOLL publication and notification of its concerns
 *  Neo.Anderson 2017-01-18
 */

extern
nsp_status_t io_init(int protocol, int nprocs);
extern
nsp_status_t io_setfl(int fd, int test);
extern
nsp_status_t io_set_cloexec(int fd);
extern
nsp_status_t io_set_nonblock(int fd, int set);
extern
void io_uninit(int protocol);
extern
nsp_status_t io_attach(void *ncbptr, int mask);
extern
nsp_status_t io_modify(void *ncbptr, int mask );
extern
void io_detach(void *ncbptr);
extern
void io_close(void *ncbptr);
extern
nsp_status_t io_pipefd(void *ncbptr, int *pipefd);
extern
nsp_status_t io_shutdown(void *ncbptr, int how);

#endif /* IO_H */
