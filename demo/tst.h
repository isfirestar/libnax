#if !defined NSPTST_HEAD
#define NSPTST_HEAD

#include "compiler.h"

#include <string.h>

#pragma pack(push, 1)

typedef struct {
	uint32_t op_;
	uint32_t cb_;
} nsp__tst_head_t;

#pragma pack(pop)

extern nsp_status_t STDCALL nsp__tst_parser(void *dat, int cb, int *pkt_cb);
extern nsp_status_t STDCALL nsp__tst_builder(void *dat, int cb);

#endif
