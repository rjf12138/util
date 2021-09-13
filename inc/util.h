#ifndef __UTIL_H__
#define __UTIL_H__

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

    // 添加定时器,错误返回-1， 成功返回一个定时器id
    int add(TimerEvent_t &event);
    // 根据定时器ID，取消定时器
    int cancel(int id);

private:
    virtual int run_handler(void);
    virtual int stop_handler(void);

private:
    bool exit_;
    os::Time time_;
    uint32_t loop_gap_;  // 单位： ms
    uint32_t timer_id_;

    os::Mutex mutex_;
    ds::MinHeap<TimerEvent_t> timer_heap_;
};

/////////////////////////// 消息总线 ////////////////////////////////////////////////
// TODO: 不支持二进制数据
#define INVALID_ID  0

#define RECEIVER_OBJECT_ID  "recv_id"
#define SENDER_OBJECT_ID    "sender_id"
#define MSG_CONTENT         "msg_content"

#define MAX_HANDER_THREAD   4

typedef struct MsgBuffer_Info {
    ds::Queue<basic::WeJson> queue;
    os::Mutex mutex;
} MsgBuffer_Info_t;

typedef uint32_t obj_id_t;
typedef std::map<int, MsgObject*>  MSG_OBJECT_MAP;
typedef std::vector<MsgBuffer_Info_t> MSG_BUFFER;

class MsgObject {
public:
    MsgObject(void);
    virtual ~MsgObject(void);

    obj_id_t id(void) const {return id_;}

    virtual int msg_handler(obj_id_t sender, const basic::WeJson &msg);
    int send_msg(obj_id_t recv_id, const basic::WeJson &msg);
private:
    obj_id_t id_;

public:
    static bool check_id(const obj_id_t &id);
    static int send_msg(obj_id_t recv_id, const basic::WeJson &msg, obj_id_t sender_id = INVALID_ID);

private:
    static int start(void);
    static int stop(void) {is_running = false;};
    static obj_id_t next_id(void);

    static int register_object(MsgObject *obj_ptr);
    static int remove_object(const obj_id_t &id);

    static void* message_forwarding_center(void *arg);

private:
    static bool is_running;
    static obj_id_t next_object_id_;

    static os::Mutex obj_lock_;
    static MSG_OBJECT_MAP objects_;

    static os::ThreadPool msg_handle_pool_;

    static MSG_BUFFER msg_buffer_;;
};


///////////////////////////// 观察者模式 //////////////////////////////////////////////////////
typedef void* (*MsgRecv_CallBack_t)(basic::WeJson msg);
struct MsgBusUser {
    std::string topic;                  // 订阅的消息主题
    MsgRecv_CallBack_t msg_handler;     // 消息接受函数
};

class MsgBus {
    typedef std::map<std::string, ds::Queue<basic::WeJson>> MSG_BUFFER_CENTER;
public:
    MsgBus(void);
    virtual ~MsgBus(void);

    // 创建主题
    virtual int create_topic(const std::string &topic);
    // 删除主题
    virtual int delete_topic(const std::string &topic);
    // 发布消息
    virtual int publish_msg(const basic::WeJson &msg);

private:
    std::vector<std::string> publisher_;      // 创建的主题
    std::vector<std::string> subscriber_;     // 订阅的主题

    uint32_t handle_;
private:
    static os::ThreadPool msg_handle_pool_; 
    static MSG_BUFFER_CENTER msg_buffer_; // 发布消息的缓冲区
};


} // namespace util


#endif