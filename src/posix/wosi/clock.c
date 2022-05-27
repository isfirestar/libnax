#include "clock.h"

/* convert to 100ns */
static const uint64_t ET_METHOD_NTKRNL = ((uint64_t) ((uint64_t) 1000 * 1000 * 10));

static void __build_wallclock_byepoch(datetime_t *systime)
{
    struct timeval tv_now;
    struct tm tm_now;

    tv_now.tv_sec = systime->epoch / ET_METHOD_NTKRNL;/* 10000000*/

    localtime_r(&tv_now.tv_sec, &tm_now);
    systime->year = tm_now.tm_year + 1900;
    systime->month = tm_now.tm_mon + 1;
    systime->day = tm_now.tm_mday;
    systime->hour = tm_now.tm_hour;
    systime->minute = tm_now.tm_min;
    systime->second = tm_now.tm_sec;
    systime->low = systime->epoch % ET_METHOD_NTKRNL;
}

static nsp_status_t __build_wallclock_bydate(datetime_t *systime)
{
    struct tm timem;
    uint64_t epoch;

    timem.tm_year = systime->year - 1900;
    timem.tm_mon = systime->month - 1;
    timem.tm_mday = systime->day;
    timem.tm_hour = systime->hour;
    timem.tm_min = systime->minute;
    timem.tm_sec = systime->second;

    epoch = (uint64_t) mktime(&timem);
    if ( unlikely(epoch == (time_t)-1) ) {
        return posix__makeerror(errno);
    }

    systime->epoch = epoch; /* second */
    systime->epoch *= 10000000; /* 100ns */
    systime->epoch += systime->low; /* ms->100ns */

    return NSP_STATUS_SUCCESSFUL;
}

PORTABLEIMPL(uint64_t) clock_epoch()
{
    /* gcc -lrt */
    struct timespec tsc;
    uint64_t tick;
    int fr;

    fr = clock_gettime(CLOCK_REALTIME, &tsc);
    if ( unlikely( -1 == fr ) ) {
        return 0;
    }

    /* force format to 10,000,000 aligned */
    tick = (uint64_t) tsc.tv_sec * ET_METHOD_NTKRNL + tsc.tv_nsec / 100;
    return  tick;
}

PORTABLEIMPL(uint64_t) clock_monotonic()
{
    /* gcc -lrt */
    struct timespec tsc;
    uint64_t tick;
    int fr;

    fr = clock_gettime(CLOCK_MONOTONIC, &tsc);
    if ( unlikely( -1 == fr ) ) {
        return 0;
    }

    /* force format to 10,000,000 aligned */
    tick = (uint64_t) tsc.tv_sec * ET_METHOD_NTKRNL + tsc.tv_nsec / 100;
    return  tick;
}

PORTABLEIMPL(uint64_t) clock_monotonic_raw()
{
    /* gcc -lrt */
    struct timespec tsc;
    uint64_t tick;
    int fr;

    fr = clock_gettime(CLOCK_MONOTONIC_RAW, &tsc);
    if ( unlikely( -1 == fr ) ) {
        return 0;
    }

    /* force format to 10,000,000 aligned */
    tick = (uint64_t) tsc.tv_sec * ET_METHOD_NTKRNL + tsc.tv_nsec / 100;
    return  tick;
}

PORTABLEIMPL(nsp_status_t) clock_systime(datetime_t *systime)
{
    if ( unlikely(!systime) ) {
        return posix__makeerror(EINVAL);
    }

    systime->epoch = clock_epoch();
    if ( likely(0 != systime->epoch) ) {
        __build_wallclock_byepoch(systime);
        return NSP_STATUS_SUCCESSFUL;
    }

    return NSP_STATUS_FATAL;
}
