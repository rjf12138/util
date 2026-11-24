//
// 测试定时器定时以及执行的误差
// 1. 定时任务越多带来的误差越大
// 2. 定时时间越接近误差越大
// 3. 目前是单线程的一次触发太多任务会等待

// 定时任务建议在几秒中及以上，单个定时器不建议添加太多任务。
#include "util.h"
#include "basic/logger.h"

#include <cstdlib>
#include <ctime>
#include <vector>

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
    (void)argc;
    (void)argv;

    std::srand(static_cast<unsigned int>(std::time(nullptr)));

    const uint32_t timer_task_num = 50;
    std::vector<TimerArg*> times;
    std::vector<os::mtime_t> time_gap = {20, 1000, 2000, 4000, 8000, 1600, 3200, 6400};

    {
        Timer timer;
        TimerEvent_t timer_event;

        for (uint32_t i = 0; i < timer_task_num; ++i) {
            TimerArg *arg_ptr = new TimerArg;
            arg_ptr->num = i;
            arg_ptr->start_times = os::Time::now();
            arg_ptr->time_gap = time_gap[static_cast<std::size_t>(std::rand()) % time_gap.size()];

            timer_event.wait_time = arg_ptr->time_gap;
            timer_event.TimeEvent_arg = arg_ptr;
            timer_event.TimeEvent_callback = timer_callback;
            timer_event.attr = TimerEventAttr_ReAdd;

            timer_id_t tid = timer.add(timer_event);
            if (tid == INVALID_TIMER_ID) {
                LOG_GLOBAL_ERROR("Add timer failed, num: %u", arg_ptr->num);
            }
            times.push_back(arg_ptr);
        }

        while (true) {
            int ch = std::getchar();
            if (ch == 'q' || ch == EOF) {
                break;
            }
        }
    } // Timer 先停止并 join，之后才能安全释放回调参数

    for (std::size_t i = 0; i < times.size(); ++i) {
        delete times[i];
    }

    return 0;
}

void* timer_callback(void* arg)
{
    if (arg == nullptr) {
        return nullptr;
    }

    TimerArg *num = static_cast<TimerArg*>(arg);
    os::mtime_t curr_time = os::Time::now();
    os::mtime_t target_time = num->start_times + num->time_gap;
    os::mtime_t dev = (curr_time > target_time ? curr_time - target_time : target_time - curr_time);
    if (dev > deviation) {
        LOG_GLOBAL_INFO("Timed tasks are too slow[num: %u, time_gap: %llu, target_deviation: %llu, real_deviation: %llu]",
                        num->num,
                        static_cast<unsigned long long>(num->time_gap),
                        static_cast<unsigned long long>(deviation),
                        static_cast<unsigned long long>(dev));
    }
    num->start_times = os::Time::now();

    return nullptr;
}
