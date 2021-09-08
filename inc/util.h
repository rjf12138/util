#ifndef __UTIL_H__
#define __UTIL_H__

#include "basic/byte_buffer.h"
#include "system/system.h"
#include "data_structure/heap.h"

namespace util
{
////////////////////////// Timer //////////////////////////////////////////
// 说明： 定时器最小能设置等待间隔时间是 5 ms
enum TimerEventAttr {
    TimerEventAttr_Exit, // 定时器到期后删除当前事件
    TimerEventAttr_ReAdd, // 定时器到期后重新添加
};

typedef void* (*TimeEvent_callback_t)(void*);
typedef struct TimerEvent {
    int id; // 添加到定时器中时会返回一个ID
    uint32_t expire_time; // 定时器触发的时间，不能小于当前时间
    uint32_t wait_time; // 定时器等待的间隔时间。单位: ms, 必须大于 0
    void* TimeEvent_arg; // 回调函数的参数
    TimeEvent_callback_t TimeEvent_callback; // 定时器到期时的回调函数
    TimerEventAttr attr;

    bool operator>(const TimerEvent &rhs) {
        return this->expire_time > rhs.expire_time;
    }

    bool operator<(const TimerEvent &rhs) {
        return this->expire_time < rhs.expire_time;
    }

    bool operator==(const TimerEvent &rhs) {
        return this->expire_time == rhs.expire_time;
    }

    TimerEvent(void)
    :id(0),
    expire_time(0),
    wait_time(0),
    attr(TimerEventAttr_Exit),
    TimeEvent_arg(nullptr),
    TimeEvent_callback(nullptr) {}

    ~TimerEvent(void) {}
} TimerEvent_t;

class Timer : public os::Thread {
public:
    Timer(void);
    ~Timer(void);

    virtual int run_handler(void);
    virtual int stop_handler(void);

    // 添加定时器,错误返回-1， 成功返回一个定时器id
    int add(TimerEvent_t &event);
    // 根据定时器ID，取消定时器
    int cancel(int id);
    
private:
    bool exit_;
    os::Time time_;
    uint32_t loop_gap_;  // 单位： ms
    uint32_t timer_id_;

    os::Mutex mutex_;
    ds::MinHeap<TimerEvent_t> timer_heap_;
};

} // namespace util


#endif