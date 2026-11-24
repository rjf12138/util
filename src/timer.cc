#include "util.h"
#include "basic/logger.h"

#include <cerrno>
#include <ctime>
#include <exception>

using namespace basic;

namespace util {

namespace {

// 定时器使用单调时钟，避免系统时间被 NTP/手工调整时定时器提前或延后
os::mtime_t monotonic_now_ms(void)
{
    struct timespec ts;
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return os::Time::now();
    }
    return static_cast<os::mtime_t>(ts.tv_sec) * 1000 +
           static_cast<os::mtime_t>(ts.tv_nsec) / 1000 / 1000;
}

} // namespace

Timer::Timer(void)
:state_(TimerState_Running),
loop_gap_(5),
timer_id_(0)
{
    if (this->init() != 0) {
        state_.store(TimerState_Exit);
        LOG_GLOBAL_ERROR("Timer init failed, timer is not available.");
    }
}

Timer::~Timer(void)
{
    this->stop_handler();
    this->join();
}

int
Timer::run_handler(void)
{
    // 注意：这里不能把 state_ 重新设置为 Running。
    // 构造函数已经设置 Running；如果对象创建后立即析构，析构函数会先设置
    // WaitExit，此时线程刚启动，如果这里覆盖成 Running，析构会永久等待。
    while (state_.load() == TimerState_Running) {
        TimerEvent_t event;
        bool has_event = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!timer_heap_.empty() && timer_heap_[0].expire_time <= monotonic_now_ms()) {
                if (timer_heap_.pop(event) > 0) {
                    has_event = true;
                }
            }
        }

        if (has_event == false) {
            os::Time::sleep(loop_gap_);
            continue;
        }

        // 回调不能在持有 mutex_ 的情况下执行，否则回调中调用 add/cancel 会死锁。
        if (event.TimeEvent_callback != nullptr) {
            try {
                event.TimeEvent_callback(event.TimeEvent_arg);
            } catch (const std::exception &e) {
                LOG_GLOBAL_ERROR("Timer callback threw exception: %s", e.what());
            } catch (...) {
                LOG_GLOBAL_ERROR("Timer callback threw unknown exception.");
            }

            if (event.attr == TimerEventAttr_ReAdd) {
                std::lock_guard<std::mutex> lock(mutex_);
                // 回调执行期间事件已经从 heap 中弹出。如果此时被 cancel()，
                // ids_ 中已经不存在该 id，不能再重新添加。
                auto iter = ids_.find(event.id);
                if (iter != ids_.end()) {
                    try {
                        if (this->readd(event) != 0) {
                            ids_.erase(iter);
                        }
                    } catch (...) {
                        ids_.erase(iter);
                        LOG_GLOBAL_ERROR("Timer readd failed, event id: %u", event.id);
                    }
                }
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                ids_.erase(event.id);
            }
        } else {
            // 空回调没有执行意义，按退出事件处理，避免 ids_ 泄漏
            std::lock_guard<std::mutex> lock(mutex_);
            ids_.erase(event.id);
        }
    }

    state_.store(TimerState_Exit);
    return 0;
}

int
Timer::stop_handler(void)
{
    if (state_.load() == TimerState_Exit) {
        return 0;
    }

    state_.store(TimerState_WaitExit);
    while (state_.load() != TimerState_Exit) {
        os::Time::sleep(10);
    }
    return 0;
}

// 添加定时器, 失败返回 INVALID_TIMER_ID(0)，成功返回定时器 ID
timer_id_t
Timer::add(TimerEvent_t &event)
{
    if (state_.load() != TimerState_Running) {
        LOG_GLOBAL_WARN("Timer is not running, can not add event.");
        return INVALID_TIMER_ID;
    }

    // 允许恰好等于 loop_gap_ 的间隔（最小 5ms）
    if (event.wait_time < loop_gap_) {
        LOG_GLOBAL_WARN("Timer wait time is too short(must greater than or equal to %u ms).", loop_gap_);
        return INVALID_TIMER_ID;
    }

    if (event.TimeEvent_callback == nullptr) {
        LOG_GLOBAL_WARN("Timer callback is null, can not add event.");
        return INVALID_TIMER_ID;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (ids_.size() >= static_cast<std::size_t>(MAX_TIMER_EVENT_NUM)) {
        LOG_GLOBAL_WARN("Timer event number reaches limit: %d.", MAX_TIMER_EVENT_NUM);
        return INVALID_TIMER_ID;
    }

    do {
        event.id = ++timer_id_;
    } while (event.id == INVALID_TIMER_ID || ids_.find(event.id) != ids_.end());

    event.expire_time = monotonic_now_ms() + event.wait_time;
    try {
        if (timer_heap_.push(event) <= 0) {
            LOG_GLOBAL_ERROR("Timer heap push failed.");
            return INVALID_TIMER_ID;
        }
    } catch (...) {
        LOG_GLOBAL_ERROR("Timer heap push threw exception.");
        return INVALID_TIMER_ID;
    }
    ids_.insert(event.id);

    return event.id;
}

ssize_t
Timer::readd(TimerEvent_t &event)
{
    // 调用者必须持有 mutex_
    if (event.wait_time < loop_gap_) {
        LOG_GLOBAL_WARN("Timer wait time is too short(must greater than or equal to %u ms).", loop_gap_);
        return -1;
    }

    event.expire_time = monotonic_now_ms() + event.wait_time;
    return timer_heap_.push(event) > 0 ? 0 : -1;
}

ssize_t
Timer::cancel(timer_id_t id)
{
    if (id == INVALID_TIMER_ID) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (ssize_t i = 0; i < timer_heap_.size(); ++i) {
        if (timer_heap_[i].id == id) {
            if (timer_heap_.remove(i) > 0) {
                ids_.erase(id);
                return 1;
            }
            return 0;
        }
    }

    // 事件可能已经弹出、正在执行回调；从 ids_ 中移除即可阻止 ReAdd
    auto iter = ids_.find(id);
    if (iter != ids_.end()) {
        ids_.erase(iter);
        return 1;
    }

    return 0;
}

}
