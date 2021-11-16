#include "util.h"
#include "basic/logger.h"

using namespace basic;

namespace util {

Timer::Timer(void)
:state_(TimerState_Exit),
loop_gap_(5),
timer_id_(0)
{
    this->init();
    state_ = TimerState_Running;
}

Timer::~Timer(void)
{
    this->stop_handler();
}

int 
Timer::run_handler(void)
{
    while(state_ == TimerState_Running) {
        if (timer_heap_.size() > 0 && timer_heap_[0].expire_time < time_.now()) {
            TimerEvent_t event;
            mutex_.lock();
            int ret = timer_heap_.pop(event);
            mutex_.unlock();
            if (ret > 0) {
                if (event.TimeEvent_callback != nullptr) {
                    LOG_GLOBAL_INFO("timer id: %d", event.id);
                    event.TimeEvent_callback(event.TimeEvent_arg);
                    if (event.attr == TimerEventAttr_ReAdd) {
                        this->add(event);
                    }
                }
            }
        }
        os::Time::sleep(loop_gap_);
    }

    state_ = TimerState_Exit;
    return 0;
}

int 
Timer::stop_handler(void)
{
    state_ = TimerState_WaitExit;
    while (state_ != TimerState_Exit) {
        LOG_GLOBAL_INFO("Timer waiting to stop.");
        os::Time::sleep(1000);
    }
    return 0;
}

// 添加定时器,错误返回-1， 成功返回一个定时器id
int 
Timer::add(TimerEvent_t &event)
{
    if (event.wait_time <= loop_gap_) {
        LOG_GLOBAL_WARN("Timer wait time is to short.");
        return -1;
    }

    event.id = ++timer_id_;
    event.expire_time = time_.now() + event.wait_time;
    mutex_.lock();
    timer_heap_.push(event);
    mutex_.unlock();

    return event.id;
}

int 
Timer::cancel(int id)
{
    int ret = 0;
    for (int i = 0; i < timer_heap_.size(); ++i) {
        if (timer_heap_[i].id == id) {
            mutex_.lock();
            ret = timer_heap_.remove(i);
            mutex_.unlock();
        }
    }

    return ret;
}

}