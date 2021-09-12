#include "util.h"

using namespace os;

namespace util
{
bool MsgObject::is_running = false;
obj_id_t MsgObject::next_object_id_ = 0;

MSG_OBJECT_MAP MsgObject::objects_;
os::ThreadPool MsgObject::msg_handle_pool_;

bool 
MsgObject::check_id(const obj_id_t &id)
{
    if (objects_.find(id) != objects_.end()) {
        return true;
    }
    return false;
}

int 
MsgObject::send_msg(const basic::WeJson &msg, obj_id_t recv_id, obj_id_t sender_id)
{
    if (check_id(recv_id) == false) {
        return -1;
    }

    basic::WeJson send_msg;
    send_msg.create_object();
    send_msg.get_object().add("recv_id", (int)recv_id);
    send_msg.get_object().add("sender_id", (int)sender_id);
    send_msg.get_object().add("data", msg);
    

    return 0;
}

int 
MsgObject::start(void)
{
    if (is_running == true) {
        return 0;
    }

    is_running = true;

    std::size_t min_thread = 4;
    std::size_t max_thread = 4;
    ThreadPoolConfig config = {min_thread, max_thread, 30, 60, SHUTDOWN_ALL_THREAD_IMMEDIATELY};
    msg_handle_pool_.set_threadpool_config(config);
    msg_handle_pool_.init();

    os::Task task;
    task.work_func = message_forwarding_center;
    task.exit_task = [](void*)->void*{is_running = false;};//exit_msg_center;

    task.thread_arg = &msg_queue1_;
    msg_handle_pool_.add_task(task);

    task.thread_arg = &msg_queue2_;
    msg_handle_pool_.add_task(task);

    task.thread_arg = &msg_queue3_;
    msg_handle_pool_.add_task(task);

    task.thread_arg = &msg_queue4_;
    msg_handle_pool_.add_task(task);

    static MsgObject g_msg_object; // 这个全局的变量保证一旦消息总线开始运行，直到程序结束才会正式停止消息总线

    return 0;
}

obj_id_t 
MsgObject::next_id(void)
{
    obj_id_t id = INVALID_ID;
    while (true) {
        obj_id_t tmp = (next_object_id_++ % 999999) + 1000;
        if (check_id(tmp) == true) {
            continue;
        } else {
            id = tmp;
            break;
        }
    }
    return id;
}

int 
MsgObject::register_object(MsgObject *obj_ptr)
{
    if (obj_ptr == nullptr) {
        return -1;
    }

    obj_id_t msg_id = obj_ptr->id();
    if (check_id(msg_id) == true) {
        return -1;
    }
    objects_[msg_id] = obj_ptr;

    return 0;
}

int 
MsgObject::remove_object(const obj_id_t &id)
{
    auto iter = objects_.find(id);
    if (iter != objects_.end()) {
        objects_.erase(iter);
    }

    return 0;
}

void* 
MsgObject::message_forwarding_center(void *arg)
{
    if (arg == nullptr) {
        return nullptr;
    }

    ds::Queue<basic::WeJson> *msg_queue = reinterpret_cast<ds::Queue<basic::WeJson>*>(arg);
    while (is_running) {
        if (msg_queue->size() > 0) { // TODO: 处理消息转发
            os::Mutex 
            basic::WeJson msg = 
        }
    }
}


MsgObject::MsgObject(void)
{
    // 初始化消息总线系统
    MsgObject::start();

    id_ = MsgObject::next_id();
}

MsgObject::~MsgObject(void)
{

}

    virtual int msg_handler(const basic::WeJson &msg);

}
