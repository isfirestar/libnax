﻿/* -march=i686 */

#ifndef POSIX_ATOMIC_H
#define POSIX_ATOMIC_H

#if _WIN32

#include <Windows.h>

/* In windows platform, InterlockedExchangeAdd implement like i++ but  InterlockedAdd syntax like ++i */
#define atom_get(ptr)										InterlockedExchangeAdd((volatile LONG *)(ptr), 0)
#define atom_get64(ptr)										InterlockedExchangeAdd64((volatile LONG64 *)(ptr), 0)
#define atom_set(ptr, value)       							InterlockedExchange(( LONG volatile *)(ptr), (LONG)(value))
#define atom_set64(ptr, value)       						InterlockedExchange64(( LONG64 volatile *)(ptr), (LONG64)(value))

#define atom_addone(ptr)                  				    InterlockedIncrement(( LONG volatile *)(ptr))
#define atom_addone64(ptr)                				    InterlockedIncrement64(( LONG64 volatile *)(ptr))
#define atom_subone(ptr)                  				    InterlockedDecrement(( LONG volatile *)(ptr))
#define atom_subone64(ptr)                 				    InterlockedDecrement64(( LONG64 volatile *) (ptr))

#define atom_increase(ptr, value)                           InterlockedAdd(( LONG volatile *)(ptr), (value))
#define atom_increase64(ptr, value)                         InterlockedAdd64(( LONG64 volatile *)(ptr), (value))
#define atom_decrease(ptr, value)                           InterlockedAdd(( LONG volatile *)(ptr), (-(value)))
#define atom_decrease64(ptr, value)                         InterlockedAdd64(( LONG64 volatile *)(ptr), (-(value)))

#define atom_exchange(ptr, value)       					InterlockedExchange(( LONG volatile *)(ptr), (LONG)(value))
#define atom_exchange64(ptr, value)       					InterlockedExchange64(( LONG64 volatile *)(ptr), (LONG64)(value))
#define atom_exchange_pointer(ptr, value)                   InterlockedExchangePointer((PVOID volatile* )(ptr), (PVOID)(value))

#define atom_compare_exchange(ptr, oldval,  newval) 		InterlockedCompareExchange( ( LONG volatile *)(ptr), (LONG)(newval), (LONG)(oldval) )
#define atom_compare_exchange64(ptr, oldval,  newval) 		InterlockedCompareExchange64( ( LONG64 volatile *)(ptr), (LONG64)(newval), (LONG64)(oldval))
#define atom_compare_exchange_pointer(ptr, oldptr, newptr) 	InterlockedCompareExchangePointer((PVOID volatile*)(ptr), (PVOID)(newptr), (PVOID)(oldptr))

#define atom_compare_exchange_strong(ptr, expect, value)    (oldval == InterlockedCompareExchange( ( LONG volatile *)(ptr), (LONG)(newval), (LONG)(oldval) ))
#define atom_compare_exchange_weak(ptr, expect, value)      (oldval == InterlockedCompareExchange( ( LONG volatile *)(ptr), (LONG)(newval), (LONG)(oldval) ))

#else /* POSIX */

/* Reference GCC 9.3
 *
 * The following built-in functions approximately match the requirements for the C++11 memory model.
 * They are all identified by being prefixed with ‘__atomic’ and most are overloade so that they work with multiple types.
 *
 * These functions are intended to replace the legacy ‘__sync’ builtins.
 * The main difference is that the memory order that is requested is a parameter to the functions.
 * New code should always use the ‘__atomic’ builtins rather than the ‘__sync’ builtins
 *
 * type __atomic_load_n(type *ptr, memory_order order) {
 *      fence_before(order)
 *      return *ptr;
 * }
 *
 * void __atomic_store_n(type *ptr, type val, memory_order order) {
 *      *ptr = vale;
 *      fence_after(order);
 * }
 */
#define atom_get(ptr)										__atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define atom_get64(ptr)										__atomic_load_n((ptr), __ATOMIC_ACQUIRE)
#define atom_set(ptr,value) 								__atomic_store_n((ptr), (value), __ATOMIC_RELEASE)
#define atom_set64(ptr,value) 								__atomic_store_n((ptr), (value), __ATOMIC_RELEASE)

/* for @__atomic_xxx_fetch, all memory orders are vaild */
#define atom_addone(ptr)                  				    __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
#define atom_subone(ptr)                  				    __atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST)
#define atom_addone64(ptr)                                  __atomic_add_fetch((ptr), 1, __ATOMIC_SEQ_CST)
#define atom_subone64(ptr)                                  __atomic_sub_fetch((ptr), 1, __ATOMIC_SEQ_CST)
#define atom_increase(ptr, value)                           __atomic_add_fetch((ptr), (value), __ATOMIC_SEQ_CST)
#define atom_increase64(ptr, value)                         __atomic_add_fetch((ptr), (value), __ATOMIC_SEQ_CST)
#define atom_decrease(ptr, value)                           __atomic_sub_fetch((ptr), (value), __ATOMIC_SEQ_CST)
#define atom_decrease64(ptr, value)                         __atomic_sub_fetch((ptr), (value), __ATOMIC_SEQ_CST)

/*
 *  type __atomic_exchange_n(type *ptr, type val, memory_order order) {
 *      fence_before(order);
 *      type n = *ptr;
 *      *ptr = val;
 *      fence_after(order);
 *      return n;
 * }
 */
#define atom_exchange(ptr, value)       				    __atomic_exchange_n((ptr), (value), __ATOMIC_ACQ_REL)
#define atom_exchange64(ptr, value)       					__atomic_exchange_n((ptr), (value), __ATOMIC_ACQ_REL)
#define atom_exchange_pointer(ptr, value)                   __atomic_exchange_n((ptr), (value), __ATOMIC_ACQ_REL)

/* these macros use to compatible with windows semantic */
#define atom_compare_exchange(ptr, oldval,  newval)         __sync_val_compare_and_swap((ptr), (oldval), (newval))
#define atom_compare_exchange64(ptr, oldval,  newval)       __sync_val_compare_and_swap((ptr), (oldval), (newval))
#define atom_compare_exchange_pointer(ptr, oldptr, newptr)  __sync_val_compare_and_swap((ptr), (oldptr), (newptr))

/*
 * bool atomic_compare_exchange(
 *   volatile type *ptr, type *expected, type desired, memory_order succ, memory_order fail)
 *   {
 *       fence_beofre(succ); // fence 1
 *       if (*ptr == *expected) {
 *           *ptr = desired;
 *           fence_after(succ);  // fence 2
 *           return true;
 *       } else {
 *           *expected = *ptr; // @strong guarantee this semantic shall be happen, @weak accorading to architecture platform
 *           fence_after(fail); // fence 3
 *           return false;
 *       }
 *   }
 *
 * fence semantic:
 * fence 1: @release before write
 * fence 2: meaningless without @__ATOMIC_SEQ_CST
 * fence 3: @acquire after read
 */
#define atom_compare_exchange_strong(ptr, expect, value)    __atomic_compare_exchange_n((ptr), (expect), (value), 0, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)
#define atom_compare_exchange_weak(ptr, expect, value)      __atomic_compare_exchange_n((ptr), (expect), (value), 1, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE)

/*
 * type __sync_lock_test_and_set (type *ptr, type value, ...)
 *          behavior: (type n = *ptr; *ptr = value; return n;)
 *
 * bool __sync_bool_compare_and_swap (type*ptr, type oldval, type newval, ...)
 *          behavior: if (*ptr == oldval) then *ptr = newval, return value is 1
 *                otherwise return value is 0, ptr and *ptr remain the same
 *
 * type __sync_val_compare_and_swap (type *ptr, type oldval,  type newval, ...)
 *          behavior: if (*ptr == oldval) then *ptr = newval, return value of *ptr before exchange
 *                otherwise return *ptr, ptr and *ptr remain the same
 *
 * void __sync_lock_release (type *ptr, ...)
 *          behavior: *ptr = 0
 *  */

#endif /* end POSIX */
#endif /* POSIX_ATOMIC_H */

