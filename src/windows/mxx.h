#if !defined MXX_HEAD
#define MXX_HEAD

#include "nis.h"

#include <stdarg.h>

extern
void nis_call_ecr(const char *fmt,...);
#define mxx_call_ecr( fmt, ...) nis_call_ecr( "[%s] "fmt, __FUNCTION__, ##__VA_ARGS__)

#endif
