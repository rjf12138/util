#include "util.h"
#include "basic/logger.h"

using namespace basic;
using namespace util;

void* timer_callback(void* arg);

int main(int argc, char **argv)
{
    int num = 0;
    Timer timer;
    TimerEvent_t timer_event;

    timer_event.wait_time = 2000;
    timer_event.TimeEvent_arg = &num;
    timer_event.TimeEvent_callback = timer_callback;
    int tid = timer.add(timer_event);

    while (true) {
        char ch = getchar();
        if (ch == 'q') {
            break;
        } else if (ch == 's') {
            timer.cancel(tid);
            LOG_GLOBAL_INFO("Cancel timer[id: %d]", tid);
        }

    }

    return 0;
}

void* timer_callback(void* arg)
{
    if (arg == nullptr) {
        return nullptr;
    }

    int *num = (int*)arg;
    LOG_GLOBAL_INFO("Num: %d", (*num)++);

    return nullptr;
}