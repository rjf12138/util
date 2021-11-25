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
}

Timer::~Timer(void)
{
    this->stop_handler();
}

int 
Timer::run_handler(void)
{
    state_ = TimerState_Running;
    while(state_ == TimerState_Running) {
        if (timer_heap_.size() > 0 && timer_heap_[0].expire_time < time_.now()) {
            TimerEvent_t event;
            mutex_.lock();
            int ret = timer_heap_.pop(event);
            if (ret > 0) {
                if (event.TimeEvent_callback != nullptr) {
                    event.TimeEvent_callback(event.TimeEvent_arg);
                    if (event.attr == TimerEventAttr_ReAdd) {
                        this->readd(event);
                    } else {
                        auto iter = ids_.find(event.id);
                        if (iter != ids_.end()) {
                            ids_.erase(iter);
                        }
                    }
                }
            }
            mutex_.unlock();
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
timer_id_t 
Timer::add(TimerEvent_t &event)
{
    if (event.wait_time <= loop_gap_) {
        LOG_GLOBAL_WARN("Timer wait time is to short(must greater then %d ms).", loop_gap_);
        return INVAILD_TIMER_ID;
    }

    do {
        event.id = ++timer_id_;
    } while (ids_.find(event.id) != ids_.end() || event.id == INVAILD_TIMER_ID);

    event.expire_time = time_.now() + event.wait_time;
    mutex_.lock();
    timer_heap_.push(event);
    ids_.insert(event.id);
    mutex_.unlock();

    return event.id;
}

int 
Timer::readd(TimerEvent_t &event)
{
    if (event.wait_time <= loop_gap_) {
        LOG_GLOBAL_WARN("Timer wait time is to short(must greater then %d ms).", loop_gap_);
        return -1;
    }
    // 因为外部已经上锁了，所以此处不在上锁
    event.expire_time = time_.now() + event.wait_time;
    timer_heap_.push(event);

    return 0;
}

int 
Timer::cancel(timer_id_t id)
{
    int ret = 0;
    for (int i = 0; i < timer_heap_.size(); ++i) {
        if (timer_heap_[i].id == id) {
            mutex_.lock();
            ret = timer_heap_.remove(i);
            
            auto iter = ids_.find(id);
            if (iter != ids_.end()) {
                ids_.erase(iter);
            }
            mutex_.unlock();
        }
    }

    return ret;
}

}