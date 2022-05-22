//
// 测试定时器定时以及执行的误差
// 1. 定时任务越多带来的误差越大
// 2. 定时时间越接近误差越大
// 3. 目前是单线程的一次触发太多任务会等待

// 定时任务建议在几秒中及以上，单个定时器不建议添加太多任务。
#include "util.h"
#include "basic/logger.h"

using namespace basic;
using namespace util;

os::mtime_t deviation = 50; // 测试定时器允许误差(单位: ms)，具体取决与回调函数执行时间
struct TimerArg {
    uint32_t num;
    os::mtime_t start_times;
    os::mtime_t time_gap;
};

void* timer_callback(void* arg);
int main(int argc, char **argv)
{
    Timer timer;
    TimerEvent_t timer_event;

    uint32_t timer_task_num = 50;
    std::vector<TimerArg*> times;
    std::vector<int> time_gap = {20, 1000, 2000, 4000, 8000, 1600, 3200, 6400};


    for (uint32_t i = 0; i < timer_task_num; ++i) {
        TimerArg *arg_ptr = new TimerArg;
        times.push_back(arg_ptr);
        arg_ptr->num = i;
        arg_ptr->start_times = os::Time::now();
        arg_ptr->time_gap = time_gap[(uint32_t)rand() % time_gap.size()];

        timer_event.wait_time = arg_ptr->time_gap;
        timer_event.TimeEvent_arg = arg_ptr;
        timer_event.TimeEvent_callback = timer_callback;
        timer_event.attr = TimerEventAttr_ReAdd;
        int tid = timer.add(timer_event);
    }

    while (true) {
        char ch = getchar();
        if (ch == 'q') {
            break;
        } else if (ch == 's') {
            // timer.cancel(tid);
            // LOG_GLOBAL_INFO("Cancel timer[id: %d]", tid);
        }

    }

    for (int i = 0; i <times.size(); ++i) {
        delete times[i];
    }

    return 0;
}

void* timer_callback(void* arg)
{
    if (arg == nullptr) {
        return nullptr;
    }

    TimerArg *num = (TimerArg*)arg;
    os::mtime_t curr_time = os::Time::now();
    os::mtime_t time_gap = num->start_times + num->time_gap;
    int dev = (curr_time > time_gap ? curr_time - time_gap : time_gap - curr_time);
    if (dev > deviation) {
        LOG_GLOBAL_INFO("Timed tasks are too slow[num: %d, time_gap: %d, target_deviation: %d, real_deviation: %d]", num->num, num->time_gap, deviation, dev);
    }
    num->start_times = os::Time::now();

    return nullptr;
}