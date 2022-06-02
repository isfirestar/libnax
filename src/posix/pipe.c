#include "pipe.h"

#include <fcntl.h>
#include <limits.h>

#include "mxx.h"
#include "atom.h"
#include "io.h"
#include "zmalloc.h"

static int _pipe_initialize(void *udata, const void *ctx, int ctxcb)
{
	return 0;
}

static void _pipe_unloader(objhld_t hld, void *udata)
{
	ncb_t *ncb;

	ncb = (ncb_t *)udata;
	if (ncb->sockfd) {
		close(ncb->sockfd);
		ncb->sockfd = 0;
	}
}

static nsp_status_t _pipe_rx(ncb_t *ncb)
{
	unsigned char pipebuf[PIPE_BUF];
	int n;
	struct pipe_package_head *pipepkt;
	ncb_t *ncb_link;
	int offset, remain;
	int m;

	while (1) {
		n = read(ncb->sockfd, pipebuf, sizeof(pipebuf));
		if (n <= 0) {
			if ( (0 == errno) || (EAGAIN == errno) || (EWOULDBLOCK == errno) ) {
				return 0;
			}

			/* system interrupted */
	        if (EINTR == errno) {
	        	continue;
	        }

			return NSP_STATUS_FATAL;
		}

		offset = 0;
		remain = n;
		while (remain > sizeof(struct pipe_package_head) ) {
			pipepkt = (struct pipe_package_head *)&pipebuf[offset];
			m = pipepkt->length + sizeof(struct pipe_package_head);
			if ( remain < m) {
				break;
			}

			ncb_link = objrefr(pipepkt->link);
			if (ncb_link) {
				ncb_post_pipedata(ncb_link, pipepkt->length, pipepkt->pipedata);
				objdefr(pipepkt->link);
			}
			remain -= m;
			offset += m;
		}
	}

	return NSP_STATUS_SUCCESSFUL;
}

static nsp_status_t _pipe_tx(ncb_t *ncb)
{
	return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t pipe_create(int protocol, int *fdw)
{
	int pipefd[2];
	objhld_t hld;
	ncb_t *ncb;
	struct objcreator creator;
    nsp_status_t status;

	if (0 != pipe2(pipefd, O_DIRECT | O_NONBLOCK | O_CLOEXEC)) {
		if (errno != EINVAL) {
			return posix__makeerror(errno);
		}

		if (0 != pipe2(pipefd, O_NONBLOCK)) {
			return posix__makeerror(errno);
		}

        status = io_setfl(pipefd[0], O_DIRECT);
		if ( !NSP_SUCCESS(status) ) {
			mxx_call_ecr("Failed on io_setfl for pipe[0]");
		}
        status = io_setfl(pipefd[1], O_DIRECT);
		if ( !NSP_SUCCESS(status) ) {
			mxx_call_ecr("Failed on io_setfl for pipe[1]");
		}
	}

	creator.known = INVALID_OBJHLD;
	creator.size = sizeof(ncb_t);
	creator.initializer = &_pipe_initialize;
	creator.unloader = &_pipe_unloader;
	creator.context = NULL;
	creator.ctxsize = 0;
    hld = objallo3(&creator);
    if (hld < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return NSP_STATUS_FATAL;
    }

    ncb = (ncb_t *) objrefr(hld);
    ncb->nis_callback = NULL;

    /* the read file-descriptor of pipe */
    ncb->sockfd = pipefd[0];
    ncb->hld = hld;
    ncb->protocol = protocol;

    /* set data handler function pointer for Rx/Tx */
    atom_set(&ncb->ncb_read, &_pipe_rx);
    atom_set(&ncb->ncb_write, &_pipe_tx);

    /* attach to epoll */
    if (!NSP_SUCCESS(io_attach(ncb, EPOLLIN))) {
        close(pipefd[1]);
        objdefr(hld);
        objclos(hld);
        return NSP_STATUS_FATAL;
    }

    /* pipe create successful */
    objdefr(hld);
    *fdw = pipefd[1];
    return NSP_STATUS_SUCCESSFUL;
}

nsp_status_t pipe_write_message(ncb_t *ncb, const unsigned char *data, unsigned int cb)
{
	struct pipe_package_head *pipemsg;
	int pipefd;
	int n;
	nsp_status_t status;

	if ( unlikely(cb >= (PIPE_BUF - sizeof(struct pipe_package_head)) || !data || 0 == cb) ) {
		return posix__makeerror(EINVAL);
	}

	status = io_pipefd(ncb, &pipefd);
	if ( unlikely(!NSP_SUCCESS(status)) ) {
		return NSP_STATUS_FATAL;
	}

	n = sizeof(struct pipe_package_head) + cb;
	pipemsg = (struct pipe_package_head *)ztrymalloc(n);
	if (!pipemsg) {
		return posix__makeerror(ENOMEM);
	}

	memcpy(pipemsg->pipedata, data, cb);
	pipemsg->length = cb;
	pipemsg->link = ncb->hld;

	if ( -1 == write(pipefd, pipemsg, n) ) {
        status = posix__makeerror(errno);
    }
	zfree(pipemsg);
	return status;
}
