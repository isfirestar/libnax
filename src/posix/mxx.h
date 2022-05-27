#if !defined MXX_HEAD
#define MXX_HEAD

#include "ifos.h"

extern void nis_call_ecr(const char *fmt,...);
/*
#define mxx_call_ecr( fmt, arg...) nis_call_ecr( "[%s/%s] "fmt, __FILE__, __FUNCTION__, ##arg)
*/
#define mxx_call_ecr( fmt, arg...) nis_call_ecr( "[%d][%s] "fmt, ifos_gettid(), __FUNCTION__, ##arg)

#endif
