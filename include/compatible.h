#if !defined NIS_COMPATIBLE_H
#define NIS_COMPATIBLE_H

#include "compiler.h"

#if NISV < 991
#define PORTABLEAPI(__type__)  __extern__ __export__ __type__ STDCALL
#define PORTABLEIMPL(__type__)   __type__ STDCALL

#if _WIN32
#include <windows.h>
#define posix__atomic_get(ptr)					InterlockedExchangeAdd((volatile LONG *)(ptr), 0)
#define posix__atomic_get64(ptr)					InterlockedExchangeAdd64((volatile LONG64 *)(ptr), 0)
#define posix__atomic_set(ptr, value)       InterlockedExchange(( LONG volatile *)(ptr), (LONG)value)
#define posix__atomic_set64(ptr, value)       InterlockedExchange64(( LONG64 volatile *)(ptr), (LONG64)value)
#else
#define posix__atomic_get(ptr)					__atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define posix__atomic_get64(ptr)				__atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define posix__atomic_set(ptr,value) 			__atomic_store_n((ptr), value, __ATOMIC_RELEASE)
#define posix__atomic_set64(ptr,value) 			__atomic_store_n((ptr), value, __ATOMIC_RELEASE)
#endif

#if !defined DDN_IPV4
#define DDN_IPV4(name)  char name[INET_ADDRSTRLEN]
#endif

struct nis_endpoint_v4 {
    DDN_IPV4(host);
    uint32_t inet;
    uint16_t port;
};

#if !defined __export__
#if _WIN32
#define __export__ __declspec (dllexport)
#else
#define __export__ __attribute__((visibility("default")))
#endif
#endif /* !__export__ */

#endif /* NISV < 991 */

#endif /* !NIS_COMPATIBLE_H */
