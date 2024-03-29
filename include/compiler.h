﻿#ifndef _TOOLS_LINUX_COMPILER_H_
#define _TOOLS_LINUX_COMPILER_H_

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <math.h>
#include <stddef.h>
#include <assert.h>
#include <time.h>

/* windows cl compiler macros:
_M_IX86 : 32bit processor
_M_AMD64 : 64bit AMD processor (befor VC2008)
_M_X64 : 64bit AMD and Intel processor(after VC2008)
_M_IX64 : 64bit Itanium processor
_WIN32 : Defined for both 32bit and 64bit processor
_WIN64 : Defined for 64bit processor
*/

typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef unsigned int u32;

#if __WORDSIZE == 64
    typedef long i64;
    typedef unsigned long u64;
#else
    typedef long long i64;
    typedef unsigned long long u64;
#endif

/* string format style in both os and system-width */
#if __WORDSIZE == 64
    #define LARGE_INTEGER_STRFMT    ("%ld")
    #define LARGE_UINTEGER_STRFMT   ("%lu")
#else
    #define LARGE_INTEGER_STRFMT    ("%lld")
    #define LARGE_UINTEGER_STRFMT   ("%llu")
#endif

#if _WIN32
    #define UINT64_STRFMT "%I64u"
    #define INT64_STRFMT "%I64d"
    #define POSIX__EOL              "\r\n"
    #define POSIX__DIR_SYMBOL       '\\'
    #define POSIX__DIR_SYMBOL_STR   "\\"
#else
    #if __WORDSIZE == 64
        #define UINT64_STRFMT "%lu"
        #define INT64_STRFMT "%ld"
    #else
        #define UINT64_STRFMT "%llu"
        #define INT64_STRFMT "%lld"
    #endif
    #define POSIX__EOL              "\n"
    #define POSIX__DIR_SYMBOL       '/'
    #define POSIX__DIR_SYMBOL_STR   "/"
#endif

/* builtin expection */
#if !defined likely
    #if _WIN32
        #define likely(x)   x
    #else
        #define likely(x)   __builtin_expect(!!(x), 1)
    #endif
#endif

#if !defined unlikely
    #if _WIN32
        #define unlikely(x)     x
    #else
        #define unlikely(x)   __builtin_expect(!!(x), 0)
    #endif
#endif

/* global status and error definition */
typedef long nsp_status_t;

#if !defined NSP_STATUS_SUCCESSFUL
    #define NSP_STATUS_SUCCESSFUL          (0L)
#endif

#if !defined NSP_STATUS_FATAL
    #define NSP_STATUS_FATAL                ((nsp_status_t)(~0))
#endif

#if !defined NSP_SUCCESS
    #define NSP_SUCCESS(status)             ((status) >= 0L)
#endif

/* status is error and code equivalent to e */
#define NSP_FAILED_AND_ERROR_EQUAL(status, e)   (((status) >= 0L) ? nsp_false : (((nsp_status_t)(~((nsp_status_t)(status)) + 1)) == (nsp_status_t)(e)))
/* status is successful or error but code equivalent to e */
#define NSP_SUCCESS_OR_ERROR_EQUAL(status, e)   (((status) >= 0L) ? nsp_true : (((nsp_status_t)(~((nsp_status_t)(status)) + 1)) == (nsp_status_t)(e)))

#define posix__makeerror(e)                 (((nsp_status_t)(e)) <= 0 ? ((nsp_status_t)e) : (nsp_status_t)(~((nsp_status_t)(e)) + 1))
#define posix__status_to_errno(status)      (((status) < 0) ? (int)((~status) + 1) : (int)status)

/* compatible definition for stdcall */
#if !defined STDCALL
    #if _WIN32 && _M_X64
        #define STDCALL __stdcall
    #else
        #define STDCALL
    #endif
#endif
#if !defined STD_CALL /* compatible with nshost 9.8 */
    #define STD_CALL STDCALL
#endif

/* nsp boolean definition */
typedef int nsp_boolean_t;
#if !defined __true__
    #define __true__ (1)
    #define YES     ((nsp_boolean_t)__true__)
    #define nsp_true ((nsp_boolean_t)__true__)
#endif
#if !defined __false__
    #define __false__ (0)
    #define NO    ((nsp_boolean_t)__false__)
    #define nsp_false ((nsp_boolean_t)__false__)
#endif

/* nsp export and global function declar header */
#if !defined __extern__
    #if defined __cplusplus
        #define __extern__  extern "C"
    #else
        #define __extern__  extern
    #endif /*__cplusplus */
#endif

#if !defined __export__
    #if _WIN32
        #define __export__ __declspec (dllexport)
    #else
        #define __export__ __attribute__((visibility("default")))
    #endif
#endif

#define SYSCALL_WHILE_EINTR(res, sys)    do {    \
    (res) = (sys); \
} while (res < 0 && EINTR == errno)

#define SYSCALL_ZERO_SUCCESS_CHECK(sys) ((0 == (sys)) ? NSP_STATUS_SUCCESSFUL : posix__makeerror(errno))

#define NSPCALL_FATAL_CHECK(res, fun) do {    \
    (res) = (fun); \
    if (res < 0) return res;    \
} while (0)

#define NSPCALL_FATAL_STOP(res, fun) do {    \
    (res) = (fun); \
    if (res < 0) return;    \
} while (0)

#define ILLEGAL_PARAMETER_CHECK(expr)   do {    \
    if (unlikely(!(expr))) {    \
        return -EINVAL;    \
    }   \
} while (0)

#define ILLEGAL_PARAMETER_STOP(expr)   do {    \
    if (unlikely(!(expr))) {    \
        return;    \
    }   \
} while (0)

#define PORTABLEAPI(__type)  __extern__ __export__ __type STDCALL
#define PORTABLEIMPL(__type)   __type STDCALL

#if !defined INET_ADDRSTRLEN
    #define INET_ADDRSTRLEN     (16)   /* netinet/in.h */
#endif

#if !defined INET6_ADDRSTRLEN
    #define INET6_ADDRSTRLEN    (46)
#endif

/* dotted decimal notation declare for IPv4 text-string */
#define DDN_IPV4(name)  char name[INET_ADDRSTRLEN]
#define DDN_IPV6(name)  char name[INET6_ADDRSTRLEN]

#if _WIN32
    #if !defined NT_SUCCESS
        #define NT_SUCCESS(Status) ((NTSTATUS)(Status) >= 0)
    #endif
#endif /* _WIN32 */

#if !defined __always_inline
    #if _WIN32
        #define __always_inline __forceinline
    #else
        #define __always_inline __inline__ __attribute__((__always_inline__))
    #endif
#endif

#if !defined NULL
    #define NULL ((void *)0)
#endif

#if !defined MAXPATH
    #define MAXPATH (0x7f)
#endif

/* use std::min std::max instead it */
#ifndef __cplusplus
    #if !defined max
        #define max(a,b)    (((a) > (b)) ? (a) : (b))
    #endif
    #if !defined min
        #define min(a,b)    (((a) < (b)) ? (a) : (b))
    #endif
#endif

/* determine whether x is a positive integral power of 2 */
#if !defined is_powerof_2
    #define is_powerof_2(x) ((x) != 0 && (((x) & ((x) - 1)) == 0))
#endif

/* aligned definition */
#if !defined __POSIX_TYPE_ALIGNED__
    #if _WIN32
        #define __POSIX_TYPE_ALIGNED__
    #else
        #define __POSIX_TYPE_ALIGNED__ __attribute__((aligned(sizeof(long))))
    #endif
#endif
#define nsp_normal_align_up(size, align) ((0 == align) ? 0 : (((size) + ((align) - 1)) / (align)) * (align))
#define nsp_align_up(size, align) (is_powerof_2(align) ? (((size) + (align) - 1) & (~((align) - 1))) : nsp_normal_align_up(size, align))
#define nsp_align_down(size, align) ((0 == align) ? 0 : ((size) / (align)) * (align))
#define nsp_align_long(size)	nsp_align_up(size, sizeof(void *))
#define nsp_align_binary(size)	nsp_align_up(size, 2)

static __always_inline int logarithm2(int x)
{
	int n;

	if (1 == x) {
		return 0;
	}

	if (!is_powerof_2(x)) {
		return -1;
	}

	n = 0;
	while (x) {
		x >>= 1;
		++n;
	}
	return n;
}

#if _WIN32
#define DEPRECATED(s) __declspec(deprecated)
#else
#define DEPRECATED(s) __attribute__((deprecated(s)))
#endif

/* zero float accuracy */
#if defined EPSINON
    #undef EPSINON
#endif
#define EPSINON  0.000001
#define is_float_zero(x) (((x) < EPSINON) && ((x) > -EPSINON))
#define is_float_equal(n, m) ((fabs((n)-(m))) <= EPSINON )
#define is_float_larger_than(n, m)  (((n) - (m)) > EPSINON)
#define is_float_less_than(n, m)  is_float_larger_than(m, n)

/* looking for the offset/size/posision of any field of structure */
#if !defined containing_record
    #define containing_record(__address, __type, __field) ((__type *)( (char *)(__address) -  (char *)(&((__type *)0)->__field)))
#endif
#if !defined container_of
    #define container_of(__address, __type, __field) containing_record(__address, __type, __field)
#endif
#if !defined sizeof_array
    #define sizeof_array(__array)   (int)(sizeof(__array) / sizeof(__array[0]))
#endif
#if !defined offsetof
    #define offsetof(__type, __field)      (( unsigned long )(&((__type*)0)->__field))
#endif
#if !defined sizeof_field
    #define sizeof_field(__type, __field)      (sizeof(((__type*)0)->__field))
#endif

/* smb macro */
#if _WIN32
#if !defined smp_mb
    #define smp_mb() do {__asm { mfence } } while( 0 )
#endif
#if !defined smp_rmb
    #define smp_rmb() do {__asm { lfence } } while( 0 )
#endif
#if !defined smp_wmb
    #define smp_wmb() do {__asm { sfence } } while( 0 )
#endif
#else /* _GNU_ */
#if !defined smp_mb
    #define smp_mb()  do { asm volatile("mfence" ::: "memory"); } while(0)
#endif
#if !defined smp_rmb
    #define smp_rmb()  do { asm volatile("lfence" ::: "memory"); } while(0)
#endif
#if !defined smp_wmb
    #define smp_wmb()  do { asm volatile("sfence" ::: "memory"); } while(0)
#endif
#endif /* !_WIN32 */

/* Optimization barrier */
#if 0
#ifndef barrier
    #define barrier() __memory_barrier()
#endif

#ifndef barrier_data
    #define barrier_data(ptr) barrier()
#endif
#endif

#if 0
/**
 * fls - find last bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs:
 * - return 32..1 to indicate bit 31..0 most significant bit set
 * - return 0 to indicate no bits set
 */
#if _WIN32

static __always_inline int fls(int x) {
    int position;
    int i;
    if (0 != x) {
        for (i = (x >> 1), position = 0; i != 0; ++position) {
            i >>= 1;
        }
    } else {
        position = -1;
    }
    return position + 1;
}
#else

static __always_inline int fls(int x) {
    int r;
    __asm__( "bsrl %1,%0\n\t"
            "jnz 1f\n\t"
            "movl $-1,%0\n"
            "1:" : "=r" (r) : "rm" (x));
    return r + 1;
}
#endif

/**
 * fls64 - find last bit set in a 64-bit value
 * @n: the value to search
 *
 * This is defined the same way as ffs:
 * - return 64..1 to indicate bit 63..0 most significant bit set
 * - return 0 to indicate no bits set
 */
static __always_inline int fls64(uint64_t x) {
    uint32_t h = x >> 32;
    if (h)
        return fls(h) + 32;
    return fls((int) x);
}

static __always_inline unsigned long fls_long(unsigned long l) {
    if (sizeof ( void *) == 4)
        return fls(l);
    return fls64(l);
}

static __always_inline uint32_t roundup_pow_of_two(uint32_t x) {
    return 1UL << fls(x - 1);
}

static __always_inline uint64_t roundup_pow_of_two64(uint64_t x) {
    return (uint64_t) 1 << fls64(x - 1);
}
#endif

static __always_inline void __read_once_size(const volatile void *p, void *res, int size) {
    switch (size) {
        case 1: *(uint8_t *) res = *(volatile uint8_t *) p;
            break;
        case 2: *(uint16_t *) res = *(volatile uint16_t *) p;
            break;
        case 4: *(uint32_t *) res = *(volatile uint32_t *) p;
            break;
        case 8: *(uint64_t *) res = *(volatile uint64_t *) p;
            break;
        default: break;
    }
}

static __always_inline void __write_once_size(volatile void *p, void *res, int size) {
    switch (size) {
        case 1: *(volatile uint8_t *) p = *(uint8_t *) res;
            break;
        case 2: *(volatile uint16_t *) p = *(uint16_t *) res;
            break;
        case 4: *(volatile uint32_t *) p = *(uint32_t *) res;
            break;
        case 8: *(volatile uint64_t *) p = *(uint64_t *) res;
            break;
        default: break;
    }
}

#if !defined write_once
    #if _WIN32
        #define write_once(xtype, x, val) \
                { union { xtype __val; char __c[1]; } __u = { .__val = (val) }; __write_once_size(&(x), __u.__c, sizeof(x)); __u.__val; }
    #else
        #define write_once(xtype, x, val) \
                { union { xtype __val; char __c[1]; } __u = { .__val = (val) }; __write_once_size(&(x), __u.__c, sizeof(x)); __u.__val; }
        /*({ union { typeof(x) __val; char __c[1]; } __u = { .__val = (val) }; __write_once_size(&(x), __u.__c, sizeof(x)); __u.__val; })*/
    #endif
#endif

#if !defined read_once
    #if _WIN32
        #define read_once(xtype, x) \
                { union { xtype __val; char __c[1]; } __u; __read_once_size(&(x), __u.__c, sizeof(x)); __u.__val; }
    #else
        #define read_once(xtype, x) \
                { union { xtype __val; char __c[1]; } __u; __read_once_size(&(x), __u.__c, sizeof(x)); __u.__val; }
        /* ({ union { typeof(x) __val; char __c[1]; } __u; __read_once_size(&(x), __u.__c, sizeof(x)); __u.__val; }) */
    #endif
#endif

#if !defined access_once
    #if _WIN32
        #define access_once(__type, x) (*(volatile __type *)&(x))
    #else
        #define access_once(__type, x) (*(volatile typeof(x) *)&(x))
    #endif
#endif

#if defined BITS_P_BYTE
    #undef BITS_P_BYTE
#endif
#define BITS_P_BYTE     (8)

#if defined PI
    #undef PI
#endif
#define PI ((double)3.14159265359)

#define angle2radian(n) (((double)(n)) * PI / 180)
#define radian2angle(n) (((double)(n)) * 180 / PI)
#define A2R(a)      angle2radian(a)
#define R2A(r)      radian2angle(r)

enum byte_order_t {
    kByteOrder_LittleEndian = 0,
    kByteOrder_BigEndian,
};

#if !defined PAGE_SIZE
    #define PAGE_SIZE (4096)
#endif

#if !defined BYTES_PER_SECTOR
    #define BYTES_PER_SECTOR		(512)
#endif

/* the maximum of integer */
#define MAX_UINT_BIT(n) (pow(2,n) - 1)
#define MAX_INT_BIT(n)  (pow(2, (n - 1)) - 1)

#define MAX_UINT8       (0xFF)
#define MAX_INT8        (0x7f)

#define MAX_UINT16      (0xFFFF)
#define MAX_INT16       (0x7FFF)

#define MAX_UINT32      (0xFFFFFFFF)
#define MAX_INT32       (0x7FFFFFFF)

#define MAX_UINT64      (0xFFFFFFFFFFFFFFFF)
#define MAX_INT64       (0x7FFFFFFFFFFFFFFF)

#define asl(n, b)               ((n) << (b))
#define asl_set(n, b)           ((n) <<= (b))
#define asr(n, b)               ((n) >> (b))
#define asr_set(n, b)           ((n) >>= (b))
#define bit_set(n, b)           ((n) |= asl(1, b))
#define bit_unset(n, b)         ((n) &= ~asl(1, b))
#define is_bit_set(n, b)        ((n) & asl(1, b))
#define is_bit_unset(n, b)      ((~(n)) & asl(1, b))
#define bit_mask(n, m)          ((n) |= (m))
#define bit_unmask(n, m)        ((n) &= ~(m))

#endif
