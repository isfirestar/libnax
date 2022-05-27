#if !defined DEMO_H
#define DEMO_H

#include "compiler.h"

#include "args.h"
#include "nis.h"
#include "threading.h"

extern nsp_status_t start_server(const struct argument *parameter, lwp_event_t *exit);
extern nsp_status_t start_client(const struct argument *parameter, lwp_event_t *exit);

extern void display(HTCPLINK link, const unsigned char *data, int size);

extern const tst_t *gettst();

#endif
