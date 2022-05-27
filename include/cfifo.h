#if !defined LIB_CKFIFO_H
#define LIB_CKFIFO_H

#include "compiler.h"
#include "threading.h"

typedef struct ckfifo *ckfifo_pt;

PORTABLEAPI(ckfifo_pt) ckfifo_init(void *buffer, uint32_t size);
PORTABLEAPI(void) ckfifo_uninit(ckfifo_pt ring);
PORTABLEAPI(uint32_t) ckfifo_len(ckfifo_pt ring);
/* on success, return value equal to @size, otherwise 0 indicate error occur maybe ring buffer is empty */
PORTABLEAPI(uint32_t) ckfifo_get(ckfifo_pt ring, void *buffer, uint32_t size);
/* on success, return value equal to @size, otherwise 0 indicate error occur maybe ring buffer is full */
PORTABLEAPI(uint32_t) ckfifo_put(ckfifo_pt ring, const void *buffer, uint32_t size);

#endif
