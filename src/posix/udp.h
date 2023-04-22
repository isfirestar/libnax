#if !defined UDP_H_20170121
#define UDP_H_20170121

#include "ncb.h"

#if !defined UDP_BUFFER_SIZE
#define UDP_BUFFER_SIZE          	(0xFFFF)
#endif


/* udp io */
extern
nsp_status_t udp_rx(ncb_t *ncb);
extern
nsp_status_t udp_txn(ncb_t *ncb, void *p);
extern
nsp_status_t udp_tx(ncb_t *ncb);
extern
nsp_status_t udp_set_boardcast(ncb_t *ncb, int enable);
extern
nsp_status_t udp_get_boardcast(ncb_t *ncb, int *enabled);

extern
void udp_setattr_r(ncb_t *ncb, int attr);

#endif
