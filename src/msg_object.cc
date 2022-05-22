#include "util.h"
#include "protocol/protocol.h"

// 内部自定义HTTP消息头
#define HTTP_RECEIVER_OBJECT_ID     "Receiver-ID"
#define HTTP_SENDER_OBJECT_ID       "Sender-ID"
#define HTTP_SENDER_TOPIC           "Topic"

using namespace basic;

namespace util
{
os::Mutex MsgObject::run_lock_;
MsgObjectState MsgObject::state_ = MsgObjectState_Exit;
obj_id_t MsgObject::next_object_id_ = 0;

os::Mutex MsgObject::topic_lock_;
MsgObject::SUBSCRIBE_TOPIC_OBJECTS_MAP MsgObject::subscribe_object_;

os::Mutex MsgObject::obj_lock_;
MsgObject::MSG_OBJECT_MAP MsgObject::objects_;
os::ThreadPool MsgObject::msg_handle_pool_;

MsgBuffer_Info_t MsgObject::msg_buffer_[MAX_HANDER_THREAD];

bool 
MsgObject::check_id(const obj_id_t &id)
{
    if (objects_.find(id) != objects_.end()) {
        return true;
    }
    return false;
}

int 
MsgObject::send_msg(obj_id_t recv_id, const topic_t topic, const basic::ByteBuffer &msg, obj_id_t sender_id)
{
    if (state_ == MsgObjectState_WaitExit) {
        return -1;
    }

    if (check_id(recv_id) == false || msg.data_size() >= MAX_MSG_SIZE) {
        return -1;
    }

    basic::ByteBuffer ptl_content;
    ptl::HttpPtl ptl;
    ptl.set_request(HTTP_METHOD_POST, "/");
    ptl.set_header_option(HTTP_RECEIVER_OBJECT_ID, std::to_string(recv_id));
    ptl.set_header_option(HTTP_SENDER_OBJECT_ID, std::to_string(sender_id));
    ptl.set_header_option(HTTP_SENDER_TOPIC, std::to_string(topic));
    ptl.set_content(msg);
    ptl.generate(ptl_content);

    int choose_queue_num = recv_id % MAX_HANDER_THREAD; // 确定数据所放的队列中
    msg_buffer_[choose_queue_num].mutex.lock();
    msg_buffer_[choose_queue_num].buffer += ptl_content;
    msg_buffer_[choose_queue_num].mutex.unlock();

    return 0;
}

int 
MsgObject::start(void)
{
    run_lock_.lock();
    if (state_ == MsgObjectState_Running) {
        run_lock_.unlock();
        return 0;
    }
    state_ = MsgObjectState_Running;
    run_lock_.unlock();

    std::size_t min_thread = MAX_HANDER_THREAD;
    os::ThreadPoolConfig config = {min_thread, 500, os::SHUTDOWN_ALL_THREAD_IMMEDIATELY};
    msg_handle_pool_.set_threadpool_config(config);

    os::Task task;
    task.work_func = message_forwarding_center;
    task.exit_task = MsgObject::stop;//exit_msg_center;

    for (int i = 0; i < MAX_HANDER_THREAD; ++i) {
        task.thread_arg = &msg_buffer_[i];
        msg_handle_pool_.add_task(task);
    }

    int i = 0;  // 确保线程已经完全运行
    for (; i < 1000; ++i) {
        os::ThreadPoolRunningInfo info = msg_handle_pool_.get_running_info();
        if (info.idle_threads_num + info.running_threads_num >= MAX_HANDER_THREAD) {
            break;
        }
        os::Time::sleep(10);
    }

    if (i >= 1000) {
        os::ThreadPoolRunningInfo info = msg_handle_pool_.get_running_info();
        LOG_GLOBAL_WARN("Msgobject running thread failed[running threads: %d, targets: %d]", info.idle_threads_num + info.running_threads_num, MAX_THREADS_NUM);
    }
    LOG_GLOBAL_DEBUG("MsgObject is running: %ld!", os::Time::now());
    static MsgObject g_msg_object; // 这个全局的变量保证一旦消息总线开始运行，直到程序结束才会正式停止消息总线

    return 0;
}

void* 
MsgObject::stop(void* arg)
{
    state_ = MsgObjectState_WaitExit;
    while (state_ != MsgObjectState_Exit) {
        os::Time::sleep(50);
    }
    return nullptr;
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

    ptl::HttpPtl ptl;
    MsgBuffer_Info_t *msg_queue = reinterpret_cast<MsgBuffer_Info_t*>(arg);
    while (state_ == MsgObjectState_Running) {
        if (msg_queue->buffer.data_size() == 0) {
            os::Time::sleep(5);
            continue;
        }

        msg_queue->mutex.lock();
        ptl::HttpParse_ErrorCode err_code = ptl.parse(msg_queue->buffer);
        msg_queue->mutex.unlock();
        if (err_code == ptl::HttpParse_OK) {
            obj_id_t sender_id = std::stoi(ptl.get_header_option(HTTP_SENDER_OBJECT_ID));
            obj_id_t recv_id = std::stoi(ptl.get_header_option(HTTP_RECEIVER_OBJECT_ID));
            std::string topic = ptl.get_header_option(HTTP_SENDER_TOPIC);

            auto find_iter = objects_.find(recv_id);
            if (find_iter != objects_.end()) {
                if (find_iter->second != nullptr) {
                    find_iter->second->msg_handler(sender_id, ptl.get_content(), std::atoi(topic.c_str()));
                }
            }
        } else {
            // 当消息解析错误时清空所有缓存
            msg_queue->mutex.lock();
            msg_queue->buffer.clear();
            msg_queue->mutex.unlock();
            LOG_GLOBAL_WARN("Parse Message failed!");
        }
    }
    state_ = MsgObjectState_Exit;
    return nullptr;
}

// 消息对象对外接口
MsgObject::MsgObject(void)
{
    // 初始化消息总线系统
    MsgObject::start();

    // 生成对象ID并注册
    id_ = MsgObject::next_id();
    MsgObject::register_object(this);
}

MsgObject::~MsgObject(void)
{
    MsgObject::remove_object(id_);
    if (objects_.size() == 0) {
        MsgObject::stop(nullptr);
    }
}

int 
MsgObject::msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic)
{
    return 0;
}

int MsgObject::send_msg(obj_id_t recv_id, basic::ByteBuffer &msg)
{
    return MsgObject::send_msg(recv_id, 0, msg, id_);
}

//////////////////////// 观察者模式 ///////////////////////////////////
bool 
MsgObject::check_topic(const topic_t &topic)
{
    if (topic_.find(topic) != topic_.end()) {
        return false;
    }

    if (topic == 0) { // topic值不能为0
        return false;
    }

    return true;
}

int 
MsgObject::create_topic(const topic_t &topic)
{
    if (check_topic(topic) == false) {
        return -1;
    }

    if (subscribe_object_.find(topic) != subscribe_object_.end()) {
        return -1;
    }

    topic_.insert(topic);
    topic_lock_.lock();
    subscribe_object_[topic] = std::pair<obj_id_t, std::set<obj_id_t>>(id_, std::set<obj_id_t>());
    topic_lock_.unlock();
    return 0;
}

int 
MsgObject::delete_topic(const topic_t &topic)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter != subscribe_object_.end()) {
        if (subscribe_iter->second.first != id_) {
            return -1;
        }

        topic_lock_.lock();
        subscribe_object_.erase(subscribe_iter);
        topic_lock_.unlock();
    }

    auto topic_iter = topic_.find(topic);
    if (topic_iter != topic_.end()) {
        topic_.erase(topic_iter);
    }

    return 0;
}

int 
MsgObject::publish_msg(const topic_t &topic, const basic::ByteBuffer &msg)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return -1;
    }

    if (subscribe_iter->second.first != id_) {
        return -1;
    }

    int sum = 0;
    for (auto iter = subscribe_iter->second.second.begin(); iter != subscribe_iter->second.second.end(); ++iter) {
        int ret = MsgObject::send_msg(*iter, topic, msg, id_);
        if (ret >= 0) {
            ++sum;
        }
    }

    return sum;
}

int 
MsgObject::subscribe_to_topic(const topic_t &topic)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return -1;
    }

    subscribe_iter->second.second.insert(id_);
    return 0;
}

int 
MsgObject::unsubscribe_topic(const topic_t &topic)
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
MsgObject::get_topic_publisher(const topic_t &topic)
{
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return INVALID_ID;
    }

    return subscribe_iter->second.first;
}

}
