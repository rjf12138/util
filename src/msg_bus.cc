#include "util.h"
#include "protocol/protocol.h"

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
MsgObject::send_msg(obj_id_t recv_id, const basic::ByteBuffer &msg, obj_id_t sender_id)
{
    if (check_id(recv_id) == false || msg.data_size() >= MAX_MSG_SIZE) {
        return -1;
    }
    
    msg.

    basic::ByteBuffer ws_ptl;
    ptl::WebsocketPtl ptl;
    ptl.generate();

    int choose_queue_num = recv_id % MAX_HANDER_THREAD; // 确定数据所放的队列中
    msg_buffer_[choose_queue_num].mutex.lock();
    msg_buffer_[choose_queue_num].buffer += msg;
    msg_buffer_[choose_queue_num].mutex.unlock();

    return 0;
}

int 
MsgObject::start(void)
{
    if (is_running == true) {
        return 0;
    }

    is_running = true;

    std::size_t min_thread = MAX_HANDER_THREAD;
    std::size_t max_thread = MAX_HANDER_THREAD;
    ThreadPoolConfig config = {min_thread, max_thread, 30, 60, SHUTDOWN_ALL_THREAD_IMMEDIATELY};
    msg_handle_pool_.set_threadpool_config(config);
    msg_handle_pool_.init();

    os::Task task;
    task.work_func = message_forwarding_center;
    task.exit_task = [](void*)->void*{is_running = false;};//exit_msg_center;

    for (int i = 0; i < MAX_HANDER_THREAD; ++i) {
        MsgBuffer_Info_t info;
        msg_buffer_.push_back(info);

        task.thread_arg = &msg_buffer_[i];
        msg_handle_pool_.add_task(task);
    }

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

    obj_lock_.lock();
    objects_[msg_id] = obj_ptr;
    obj_lock_.unlock();

    return 0;
}

int 
MsgObject::remove_object(const obj_id_t &id)
{
    auto iter = objects_.find(id);
    if (iter != objects_.end()) {
        obj_lock_.lock();
        objects_.erase(iter);
        obj_lock_.unlock();
    }

    return 0;
}

void* 
MsgObject::message_forwarding_center(void *arg)
{
    if (arg == nullptr) {
        return nullptr;
    }

    MsgBuffer_Info_t *msg_queue = reinterpret_cast<MsgBuffer_Info_t*>(arg);
    while (is_running) {
        if (msg_queue->queue.size() > 0) {
            basic::WeJson msg;
            msg_queue->mutex.lock();
            if (msg_queue->queue.pop(msg) == 0) {
                continue;
            }
            msg_queue->mutex.unlock();

            JsonObject &jsobj = msg.get_object();
            if (jsobj.find(RECEIVER_OBJECT_ID) != jsobj.end()) {
                JsonNumber js_sender_id = jsobj[SENDER_OBJECT_ID];
                obj_id_t sender_id = static_cast<obj_id_t>(js_sender_id.to_int());

                JsonNumber js_recv_id = jsobj[RECEIVER_OBJECT_ID];
                obj_id_t recv_id = static_cast<obj_id_t>(js_recv_id.to_int());

                auto find_iter = objects_.find(recv_id);
                if (find_iter != objects_.end()) {
                    if (find_iter->second != nullptr) {
                        find_iter->second->msg_handler(sender_id, jsobj[MSG_CONTENT]);
                    }
                }
            }
        }
        Time::sleep(10);
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
    MsgObject::remove_object(id_);
    if (objects_.size() == 0) {
        MsgObject::stop();
    }
}

int 
MsgObject::msg_handler(obj_id_t sender, const basic::ByteBuffer &msg)
{
    return 0;
}

int MsgObject::send_msg(obj_id_t recv_id, const basic::ByteBuffer &msg, MsgType msg_type)
{
    return MsgObject::send_msg(recv_id, msg, id_);
}

//////////////////////// 观察者模式 ///////////////////////////////////
int 
MsgObject::create_topic(const std::string &topic)
{
    if (topic_.find(topic) != topic_.end()) {
        return -1;
    }

    if (subscribe_object_.find(topic) != subscribe_object_.end()) {
        return -1;
    }

    topic_.insert(topic);
    lock_.lock();
    subscribe_object_[topic] = std::pair<obj_id_t, std::set<obj_id_t>>(id_, std::set<obj_id_t>());
    lock_.unlock();
    return 0;
}

int 
MsgObject::delete_topic(const std::string &topic)
{
    auto topic_iter = topic_.find(topic);
    if (topic_iter != topic_.end()) {
        topic_.erase(topic_iter);
    }

    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter != subscribe_object_.end()) {
        lock_.lock();
        subscribe_object_.erase(subscribe_iter);
        lock_.unlock();
    }

    return 0;
}

int 
MsgObject::publish_msg(const std::string &topic, const basic::WeJson &msg)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return -1;
    }

    int sum = 0;
    for (auto iter = subscribe_iter->second.second.begin(); iter != subscribe_iter->second.second.end(); ++iter) {
        int ret = MsgObject::send_msg(*iter, msg, id_);
        if (ret >= 0) {
            ++sum;
        }
    }

    return sum;
}

int 
MsgObject::subscribe_to_topic(const std::string &topic)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return -1;
    }

    subscribe_iter->second.second.insert(id_);
    return 0;
}

int 
MsgObject::unsubscribe_topic(const std::string &topic)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return 0;
    }

    auto obj_iter = subscribe_iter->second.second.find(id_);
    if (obj_iter != subscribe_iter->second.second.end()) {
        subscribe_iter->second.second.erase(obj_iter);
    }
    return 0;
}

obj_id_t 
MsgObject::get_topic_publisher(const std::string &topic)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return INVALID_ID;
    }

    return subscribe_iter->second.first;
}

}
