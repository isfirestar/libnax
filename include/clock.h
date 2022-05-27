#ifndef POSIX_TIME_H
#define POSIX_TIME_H

#include "compiler.h"

struct __datetime_t {
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    uint64_t low; /* 100ns */
    uint64_t epoch;
} __POSIX_TYPE_ALIGNED__;

typedef struct __datetime_t datetime_t;

/* obtain the wall clock on current system, in 100ns */
PORTABLEAPI(uint64_t) clock_monotonic_raw();
PORTABLEAPI(uint64_t) clock_monotonic();
/* obtain absolute time elapse from 1970-01-01 00:00:00 000 to the time point of function invoked, in 100ns */
PORTABLEAPI(uint64_t) clock_epoch();

/* obtain the current wall clock and systime */
PORTABLEAPI(nsp_status_t) clock_systime(datetime_t *systime);

#endif /* POSIX_TIME_H */
