#include "ae.h"

#include <stdio.h>
#include <string.h>

struct TimerArgs
{
    long long id;
    uint64_t timestamp;
    int times;
    char name[8];
    long long interval;
};

static int myTimeProc(struct aeEventLoop *eventLoop, long long id, void *clientData)
{
    struct TimerArgs *arg;

    arg = (struct TimerArgs *)clientData;

    printf("[%s] call times:%d, time deviation:%lu(us)\n", arg->name, arg->times, getMonotonicUs() - arg->timestamp - arg->interval * 1000);
    
    arg->timestamp = getMonotonicUs();
    arg->times++;
    return arg->interval;
}

int main(int argc, char *argv[])
{
    aeEventLoop *loop;
    struct TimerArgs arg1, arg2;
    
    loop = aeCreateEventLoop(10);
    if (!loop) {
        fprintf(stderr, "create event loop failed\n");
        return -1;
    }

    strcpy(arg1.name, "timer-1");
    arg1.timestamp = getMonotonicUs();
    arg1.times = 0;
    arg1.interval = 5000;
    arg1.id = aeCreateTimeEvent(loop, arg1.interval, &myTimeProc, &arg1, NULL);

    strcpy(arg2.name, "timer-2");
    arg2.timestamp = getMonotonicUs();
    arg2.times = 0;
    arg2.interval = 1000;
    arg2.id = aeCreateTimeEvent(loop, arg2.interval, &myTimeProc, &arg2, NULL);
    // aeProcessEvents(loop, AE_ALL_EVENTS);
    aeMain(loop);

    aeDeleteEventLoop(loop);
    return 0;
}
