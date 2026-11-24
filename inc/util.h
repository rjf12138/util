#ifndef __UTIL_H__
#define __UTIL_H__

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <sys/types.h>

#include "basic/byte_buffer.h"
#include "basic/wejson.h"
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

#define INVALID_TIMER_ID    0
// 兼容旧代码中的拼写
#define INVAILD_TIMER_ID    INVALID_TIMER_ID
#define MAX_TIMER_EVENT_NUM 10000
typedef uint32_t timer_id_t;

typedef void* (*TimeEvent_callback_t)(void*);
struct TimerEvent {
    friend class Timer;
public:
    TimerEvent(void)
    :wait_time(0),
    TimeEvent_arg(nullptr),
    TimeEvent_callback(nullptr),
    attr(TimerEventAttr_Exit),
    id(INVAILD_TIMER_ID),
    expire_time(0) {}

    ~TimerEvent(void) {}

    bool operator>(const TimerEvent &rhs) {
        return this->expire_time > rhs.expire_time;
    }

    bool operator<(const TimerEvent &rhs) {
        return this->expire_time < rhs.expire_time;
    }

    bool operator==(const TimerEvent &rhs) {
        return this->expire_time == rhs.expire_time;
    }

public:
    os::mtime_t wait_time; // 定时器等待的间隔时间。单位: ms, 必须大于等于 5
    void* TimeEvent_arg; // 回调函数的参数
    TimeEvent_callback_t TimeEvent_callback; // 定时器到期时的回调函数
    TimerEventAttr attr;

private:
    timer_id_t id; // 添加到定时器中时会返回一个ID
    os::mtime_t expire_time; // 定时器触发的时间（单调时钟，单位 ms）
};
typedef TimerEvent TimerEvent_t;

class Timer : public os::Thread {
    enum TimerState {
        TimerState_Exit,
        TimerState_WaitExit,
        TimerState_Running,
    };

public:
    Timer(void);
    ~Timer(void);

    // 添加定时器, 失败返回 INVALID_TIMER_ID(0)，成功返回定时器 ID
    // 注意：不要在回调函数中析构 Timer 或等待 Timer 析构，否则会等待自身。
    timer_id_t add(TimerEvent_t &event);
    // 根据定时器ID取消定时器。取消成功返回1，未找到返回0。
    ssize_t cancel(timer_id_t id);

private:
    virtual int run_handler(void);
    virtual int stop_handler(void);
    // 重新添加定时器
    ssize_t readd(TimerEvent_t &event);
private:
    std::atomic<TimerState> state_;
    uint32_t loop_gap_;  // 单位： ms
    uint32_t timer_id_;

    std::mutex mutex_;
    ds::MinHeap<TimerEvent_t> timer_heap_;
    std::set<timer_id_t> ids_;
};

/////////////////////////// 消息对象 ////////////////////////////////////////////////
// 说明：消息体使用 ByteBuffer，可承载二进制数据；MsgType 仅为兼容旧接口保留。
#define INVALID_ID  0

#define RECEIVER_OBJECT_ID  "recv_id"
#define SENDER_OBJECT_ID    "sender_id"
#define MSG_CONTENT         "msg_content"

#define MAX_MSG_SIZE        (250 * 1024 * 1024) // 单条消息最大不能超过250M
// 每个消息队列缓存的数据上限，防止生产者持续快于消费者导致内存无界增长
#define MAX_MSG_BUFFER_SIZE (300 * 1024 * 1024)
#define MAX_HANDLER_THREAD  4
// 兼容旧代码中的拼写
#define MAX_HANDER_THREAD   MAX_HANDLER_THREAD
// 对象 ID 的最大数量（ID 范围 1000 ~ 1000998）
#define MAX_OBJECT_NUM      999999

enum MsgType {
    MsgType_Text = 1,
    MsgType_Binary = 2
};

enum MsgObjectState {
    MsgObjectState_Running,
    MsgObjectState_WaitExit,
    MsgObjectState_Exit
};

typedef struct MsgBuffer_Info {
    basic::ByteBuffer buffer;
    std::mutex mutex;
} MsgBuffer_Info_t;

typedef uint32_t obj_id_t;
typedef uint32_t topic_t;

class MsgObject {
private:
    struct ObjectEntry {
        MsgObject *obj;
        uint32_t refs;
        bool removing;

        ObjectEntry(MsgObject *ptr = nullptr)
        : obj(ptr), refs(0), removing(false) {}
    };
    typedef std::map<obj_id_t, ObjectEntry>  MSG_OBJECT_MAP;
    typedef std::map<topic_t, std::pair<obj_id_t, std::set<obj_id_t>>>  SUBSCRIBE_TOPIC_OBJECTS_MAP;
    typedef std::map<topic_t, std::set<obj_id_t>>  PENDING_SUBSCRIBE_MAP;

public:
    MsgObject(void);
    virtual ~MsgObject(void);

    // 获取对象 ID
    obj_id_t id(void) const {return id_.load();}

    // 消息收到时回调函数
    // msg 只在本次回调执行期间有效，如果需要保存，请在回调中自行复制。
    virtual int msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic);
    // 发送消息（使用当前消息类作为发送ID）
    int send_msg(obj_id_t recv_id, basic::ByteBuffer &msg);

    // 检查对象 ID 是否存在
    static bool check_id(const obj_id_t &id);
    // 发送消息
    static int send_msg(obj_id_t recv_id, const topic_t topic, const basic::ByteBuffer &msg, obj_id_t sender_id);
    // 从消息总线注销并等待正在执行的回调结束。
    // 如果对象的派生类还有成员需要在析构时安全释放，建议在派生类析构函数中先调用它。
    // 注意：不能在 msg_handler 中对自身调用，否则会等待自己持有的引用而卡住。
    int detach_from_bus(void);

private:
    std::atomic<obj_id_t> id_; // 对象ID

    // 禁止拷贝：拷贝会共享 ID，导致析构时互相注销
    MsgObject(const MsgObject&) = delete;
    MsgObject& operator=(const MsgObject&) = delete;

public:
// 下面是观察者模式的相关模式
// 创建和删除主题
// 消息对象可以订阅对应主题
// 主题的创建者通过主题发布消息，订阅该主题的对象都能收到该消息

    // 创建主题
    int create_topic(const topic_t &topic);
    // 删除主题
    int delete_topic(const topic_t &topic);
    // 发布消息
    int publish_msg(const topic_t &topic, const basic::ByteBuffer &msg);

    // 订阅主题。topic 尚未创建时先挂起，topic 创建成功后自动生效。
    int subscribe_to_topic(const topic_t &topic);
    // 取消订阅
    int unsubscribe_topic(const topic_t &topic);

    // 获取主题的发布者
    obj_id_t get_topic_publisher(const topic_t &topic);

private:
    // 对象销毁时清理自己创建/订阅/待订阅的所有 topic 关系
    void cleanup_topics(void);

private:
    std::set<topic_t> topic_;// 当前对象创建的主题

// 消息转发以及对象ID维护的相关函数
// 处理消息转发启动和停止，消息对象注册和注销
// 当得知接受者的对象id时，可以通过对象id发送消息
private:
    // 开启消息处理
    static int start(void);
    // 停止消息处理
    static void* stop(void* arg);

    // 注册对象类（生成 ID 并插入注册表）
    static int register_object(MsgObject *obj_ptr);
    // 移除对象类
    static int remove_object(const obj_id_t &id);

    // 获取/释放对象引用，保证 msg_handler 执行期间对象不被销毁
    static MsgObject* acquire_object(const obj_id_t &id);
    static void release_object(const obj_id_t &id);

    // 清空所有消息队列缓存
    static void clear_message_buffers(void);

    // 消息转发中心
    static void* message_forwarding_center(void *arg);

    // 每个消息队列一个转发线程，生命周期由 start/stop 管理
    class ForwardWorker : public os::Thread {
    public:
        explicit ForwardWorker(MsgBuffer_Info_t *queue) : queue_(queue) {}
        virtual ~ForwardWorker(void) {}
        virtual int run_handler(void);

    private:
        MsgBuffer_Info_t *queue_;
    };

    static void destroy_forward_workers(void);

private:
    static std::atomic<MsgObjectState> state_;
    static std::mutex run_lock_;
    static obj_id_t next_object_id_;
    static std::atomic<MsgObject*> bus_keeper_; // 保证消息总线运行到进程退出的哨兵对象

    // 注册的消息对象
    static std::mutex obj_lock_;
    static std::condition_variable obj_cv_;
    static MSG_OBJECT_MAP objects_;

    // 正在运行的转发线程数量，用于安全停止
    static std::atomic<int> active_workers_;
    static std::mutex stop_lock_;
    static std::condition_variable stop_cv_;

    // 主题/订阅关系
    static std::mutex topic_lock_;
    static SUBSCRIBE_TOPIC_OBJECTS_MAP subscribe_object_; // 所有主题以及订阅主题的对象
    static PENDING_SUBSCRIBE_MAP pending_subscribe_;     // topic 尚不存在时的待订阅对象

    // 消息缓冲区
    static ForwardWorker *forward_workers_[MAX_HANDLER_THREAD];
    static MsgBuffer_Info_t msg_buffer_[MAX_HANDLER_THREAD];
};
} // namespace util


#endif
