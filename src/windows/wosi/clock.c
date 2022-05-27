#include "clock.h"

#include <Windows.h>

/* NT FILETIME 到 Epoch 时间的差距， 单位100ns(NT FILETIME采用1640年记时)
  使用ULL强制限制数据类型， 避免 warning: this decimal constant is unsigned only in ISO C90 警告 */
static const uint64_t NT_EPOCH_ESCAPE = (uint64_t)((uint64_t)((uint64_t)27111902ULL << 32) | 3577643008ULL);
/* { .dwLowDateTime = 3577643008, .dwHighDateTime = 27111902 }; */

/* convert to 100ns */
static const uint64_t ET_METHOD_NTKRNL = ((uint64_t)((uint64_t)1000 * 1000 * 1000));

PORTABLEIMPL(int) clock_systime(datetime_t* systime)
{
    uint64_t nt_filetime;
    FILETIME file_now, local_file_now;
    SYSTEMTIME sys_now;

    nt_filetime = systime->epoch + NT_EPOCH_ESCAPE;

    file_now.dwLowDateTime = nt_filetime & 0xFFFFFFFF;
    file_now.dwHighDateTime = (nt_filetime >> 32) & 0xFFFFFFFF;
    FileTimeToLocalFileTime(&file_now, &local_file_now);

    FileTimeToSystemTime(&local_file_now, &sys_now);

    systime->year = sys_now.wYear;
    systime->month = sys_now.wMonth;
    systime->day = sys_now.wDay;
    systime->hour = sys_now.wHour;
    systime->minute = sys_now.wMinute;
    systime->second = sys_now.wSecond;
    systime->low = systime->epoch % ET_METHOD_NTKRNL;

    return 0;
}

PORTABLEIMPL(uint64_t) clock_epoch()
{
    FILETIME file_time;
    uint64_t epoch;

    /* this is the interface to obtain high resolution epcoh as file-time */
    GetSystemTimeAsFileTime(&file_time);
    epoch = (uint64_t)file_time.dwHighDateTime << 32;
    epoch |= file_time.dwLowDateTime;
    epoch -= NT_EPOCH_ESCAPE;
    return epoch;
}

PORTABLEIMPL(uint64_t) clock_monotonic_raw()
{
    LARGE_INTEGER counter;
    static LARGE_INTEGER frequency = { 0 };

    if (0 == frequency.QuadPart) {
        if (!QueryPerformanceFrequency(&frequency)) {
            return 0;
        }
    }

    if (QueryPerformanceCounter(&counter)) {
        return (uint64_t)(ET_METHOD_NTKRNL * ((double)counter.QuadPart / frequency.QuadPart));
    }
    return 0;
}

PORTABLEIMPL(uint64_t) clock_monotonic()
{
    return clock_monotonic_raw();
}