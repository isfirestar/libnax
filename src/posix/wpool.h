#if !defined KE_H_20170118
#define KE_H_20170118

#include "compiler.h"

extern
nsp_status_t wp_init(int protocol);
extern
void wp_uninit(int protocol);
extern
nsp_status_t wp_queued(void *ncbptr);

#endif
