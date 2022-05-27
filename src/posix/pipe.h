#ifndef PIPE_H_20170118
#define PIPE_H_20170118

#include "ncb.h"

struct pipe_package_head
{
    unsigned int length;
    objhld_t link;
    unsigned char pipedata[0];
};

extern
nsp_status_t pipe_create(int protocol, int *fdw);
extern
nsp_status_t pipe_write_message(ncb_t *ncb, const unsigned char *data, unsigned int cb);

#endif
