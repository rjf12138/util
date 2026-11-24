#include "util.h"
#include "protocol/protocol.h"

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <limits>
#include <new>

// 内部自定义HTTP消息头
#define HTTP_RECEIVER_OBJECT_ID     "Receiver-ID"
#define HTTP_SENDER_OBJECT_ID       "Sender-ID"
#define HTTP_SENDER_TOPIC           "Topic"

using namespace basic;

namespace util
{

namespace {

// 严格解析无符号 32 位整数，避免 std::stoi / std::atoi 的异常和溢出问题
bool parse_u32_value(const std::string &value, uint32_t &out)
{
    if (value.empty() || value[0] == '-') {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    unsigned long long tmp = std::strtoull(value.c_str(), &end, 10);
    if (errno == ERANGE || end == value.c_str() || *end != '\0') {
        return false;
    }

    if (tmp > static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max())) {
        return false;
    }

    out = static_cast<uint32_t>(tmp);
    return true;
}

} // namespace

std::atomic<MsgObjectState> MsgObject::state_(MsgObjectState_Exit);
std::mutex MsgObject::run_lock_;
obj_id_t MsgObject::next_object_id_ = 0;
std::atomic<MsgObject*> MsgObject::bus_keeper_(nullptr);

std::mutex MsgObject::obj_lock_;
std::condition_variable MsgObject::obj_cv_;
MsgObject::MSG_OBJECT_MAP MsgObject::objects_;

std::atomic<int> MsgObject::active_workers_(0);
std::mutex MsgObject::stop_lock_;
std::condition_variable MsgObject::stop_cv_;

std::mutex MsgObject::topic_lock_;
MsgObject::SUBSCRIBE_TOPIC_OBJECTS_MAP MsgObject::subscribe_object_;
MsgObject::PENDING_SUBSCRIBE_MAP MsgObject::pending_subscribe_;

MsgObject::ForwardWorker *MsgObject::forward_workers_[MAX_HANDLER_THREAD] = {nullptr};

MsgBuffer_Info_t MsgObject::msg_buffer_[MAX_HANDLER_THREAD];

int
MsgObject::ForwardWorker::run_handler(void)
{
    MsgObject::message_forwarding_center(queue_);
    return 0;
}

bool
MsgObject::check_id(const obj_id_t &id)
{
    std::lock_guard<std::mutex> lock(obj_lock_);
    auto iter = objects_.find(id);
    return iter != objects_.end() && iter->second.removing == false && iter->second.obj != nullptr;
}

MsgObject*
MsgObject::acquire_object(const obj_id_t &id)
{
    std::lock_guard<std::mutex> lock(obj_lock_);
    auto iter = objects_.find(id);
    if (iter == objects_.end() || iter->second.removing == true || iter->second.obj == nullptr) {
        return nullptr;
    }

    ++iter->second.refs;
    return iter->second.obj;
}

void
MsgObject::release_object(const obj_id_t &id)
{
    std::lock_guard<std::mutex> lock(obj_lock_);
    auto iter = objects_.find(id);
    if (iter == objects_.end()) {
        return;
    }

    if (iter->second.refs > 0) {
        --iter->second.refs;
    }

    if (iter->second.removing == true && iter->second.refs == 0) {
        obj_cv_.notify_all();
    }
}

int
MsgObject::send_msg(obj_id_t recv_id, const topic_t topic, const basic::ByteBuffer &msg, obj_id_t sender_id)
{
    if (state_.load() != MsgObjectState_Running) {
        return -1;
    }

    if (recv_id == INVALID_ID || check_id(recv_id) == false || msg.data_size() >= MAX_MSG_SIZE) {
        return -1;
    }

    basic::ByteBuffer ptl_content;
    ptl::HttpPtl ptl;
    if (ptl.set_request(HTTP_METHOD_POST, "/") != 0 ||
        ptl.set_header_option(HTTP_RECEIVER_OBJECT_ID, std::to_string(recv_id)) != 0 ||
        ptl.set_header_option(HTTP_SENDER_OBJECT_ID, std::to_string(sender_id)) != 0 ||
        ptl.set_header_option(HTTP_SENDER_TOPIC, std::to_string(topic)) != 0 ||
        ptl.set_content(msg) != 0 ||
        ptl.generate(ptl_content) != 0) {
        LOG_GLOBAL_WARN("MsgObject build message failed, receiver: %u", recv_id);
        return -1;
    }

    int choose_queue_num = static_cast<int>(recv_id % MAX_HANDLER_THREAD); // 确定数据所放的队列中
    std::lock_guard<std::mutex> lock(msg_buffer_[choose_queue_num].mutex);

    // 入队期间总线可能已经停止，需要再次检查
    if (state_.load() != MsgObjectState_Running) {
        return -1;
    }

    if (msg_buffer_[choose_queue_num].buffer.data_size() + ptl_content.data_size() > MAX_MSG_BUFFER_SIZE) {
        LOG_GLOBAL_WARN("MsgObject queue is full, drop message. receiver: %u", recv_id);
        return -1;
    }

    try {
        msg_buffer_[choose_queue_num].buffer += ptl_content;
    } catch (...) {
        LOG_GLOBAL_ERROR("MsgObject append message threw exception, receiver: %u", recv_id);
        return -1;
    }

    return 0;
}

void
MsgObject::clear_message_buffers(void)
{
    for (int i = 0; i < MAX_HANDLER_THREAD; ++i) {
        std::lock_guard<std::mutex> lock(msg_buffer_[i].mutex);
        msg_buffer_[i].buffer.clear();
    }
}

int
MsgObject::start(void)
{
    std::unique_lock<std::mutex> start_lock(run_lock_);
    MsgObjectState current = state_.load();
    if (current == MsgObjectState_Running) {
        return 0;
    }
    if (current == MsgObjectState_WaitExit) {
        LOG_GLOBAL_WARN("MsgObject is stopping, can not start.");
        return -1;
    }

    // 等待上一次运行的所有 worker 完全退出，避免新旧 worker 同时访问缓冲区
    {
        std::unique_lock<std::mutex> wait_lock(stop_lock_);
        stop_cv_.wait(wait_lock, []() {
            return active_workers_.load() == 0;
        });
    }

    // 清空上次运行可能残留的消息
    clear_message_buffers();

    state_.store(MsgObjectState_Running);

    for (int i = 0; i < MAX_HANDLER_THREAD; ++i) {
        forward_workers_[i] = new (std::nothrow) ForwardWorker(&msg_buffer_[i]);
        if (forward_workers_[i] == nullptr || forward_workers_[i]->init() != 0) {
            LOG_GLOBAL_ERROR("MsgObject create forwarding thread failed, queue: %d", i);
            if (forward_workers_[i] != nullptr) {
                delete forward_workers_[i];
                forward_workers_[i] = nullptr;
            }
            start_lock.unlock();
            MsgObject::stop(nullptr);
            return -1;
        }
    }

    start_lock.unlock();

    int i = 0;  // 确保所有转发线程已经完全运行
    for (; i < 1000; ++i) {
        if (active_workers_.load() >= MAX_HANDLER_THREAD) {
            break;
        }
        os::Time::sleep(10);
    }

    if (i >= 1000) {
        LOG_GLOBAL_WARN("Msgobject running thread failed[running threads: %d, targets: %d]",
                        active_workers_.load(), MAX_HANDLER_THREAD);
        MsgObject::stop(nullptr);
        return -1;
    }

    LOG_GLOBAL_DEBUG("MsgObject is running: %lu!", static_cast<unsigned long>(os::Time::now()));
    static MsgObject g_msg_object; // 这个全局的变量保证一旦消息总线开始运行，直到程序结束才会正式停止消息总线
    bus_keeper_.store(&g_msg_object);

    return 0;
}

void*
MsgObject::stop(void* arg)
{
    (void)arg;

    std::unique_lock<std::mutex> lifecycle_lock(run_lock_);
    MsgObjectState current = state_.load();
    if (current == MsgObjectState_Exit) {
        return nullptr;
    }

    state_.store(MsgObjectState_WaitExit);
    lifecycle_lock.unlock();

    // 等待所有转发线程退出当前循环
    {
        std::unique_lock<std::mutex> wait_lock(stop_lock_);
        stop_cv_.wait(wait_lock, []() {
            return active_workers_.load() == 0;
        });
    }

    lifecycle_lock.lock();
    if (state_.load() == MsgObjectState_Exit) {
        return nullptr;
    }

    state_.store(MsgObjectState_Exit);
    destroy_forward_workers();
    clear_message_buffers();
    lifecycle_lock.unlock();

    return nullptr;
}

void
MsgObject::destroy_forward_workers(void)
{
    for (int i = 0; i < MAX_HANDLER_THREAD; ++i) {
        if (forward_workers_[i] == nullptr) {
            continue;
        }

        forward_workers_[i]->join();
        delete forward_workers_[i];
        forward_workers_[i] = nullptr;
    }
}


int
MsgObject::register_object(MsgObject *obj_ptr)
{
    if (obj_ptr == nullptr) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(obj_lock_);
    if (objects_.size() >= static_cast<std::size_t>(MAX_OBJECT_NUM)) {
        LOG_GLOBAL_ERROR("MsgObject number reaches limit: %d", MAX_OBJECT_NUM);
        return -1;
    }

    obj_id_t msg_id = INVALID_ID;
    do {
        msg_id = (next_object_id_++ % MAX_OBJECT_NUM) + 1000;
    } while (msg_id == INVALID_ID || objects_.find(msg_id) != objects_.end());

    obj_ptr->id_.store(msg_id);
    try {
        std::pair<MSG_OBJECT_MAP::iterator, bool> ret =
            objects_.emplace(msg_id, ObjectEntry(obj_ptr));
        if (ret.second == false) {
            obj_ptr->id_.store(INVALID_ID);
            LOG_GLOBAL_ERROR("MsgObject register duplicate id: %u", msg_id);
            return -1;
        }
    } catch (...) {
        obj_ptr->id_.store(INVALID_ID);
        LOG_GLOBAL_ERROR("MsgObject register threw exception.");
        return -1;
    }

    return 0;
}

int
MsgObject::remove_object(const obj_id_t &id)
{
    std::unique_lock<std::mutex> lock(obj_lock_);
    auto iter = objects_.find(id);
    if (iter == objects_.end() || iter->second.removing == true) {
        return -1;
    }

    // 标记为正在移除，之后新的消息不会再取到该对象；
    // 等待已经取得引用的 msg_handler 执行完毕。
    iter->second.removing = true;
    obj_cv_.wait(lock, [&iter]() {
        return iter->second.refs == 0;
    });

    objects_.erase(iter);
    return 0;
}

void*
MsgObject::message_forwarding_center(void *arg)
{
    if (arg == nullptr) {
        return nullptr;
    }

    MsgBuffer_Info_t *msg_queue = reinterpret_cast<MsgBuffer_Info_t*>(arg);
    active_workers_.fetch_add(1);

    try {
        ptl::HttpPtl ptl;
        while (state_.load() == MsgObjectState_Running) {
            bool has_data = false;
            {
                std::lock_guard<std::mutex> lock(msg_queue->mutex);
                has_data = msg_queue->buffer.data_size() > 0;
            }

            if (has_data == false) {
                os::Time::sleep(5);
                continue;
            }

            ptl::HttpParse_ErrorCode err_code = ptl::HttpParse_OK;
            {
                std::lock_guard<std::mutex> lock(msg_queue->mutex);
                try {
                    err_code = ptl.parse(msg_queue->buffer);
                } catch (...) {
                    err_code = ptl::HttpParse_ParseHeaderFailed;
                }
            }

            if (err_code != ptl::HttpParse_OK) {
                // 正常情况下 send_msg 写入的是完整 HTTP 消息，走到这里说明队列数据
                // 已经损坏（例如异常导致半包）。为避免把后续数据继续按错误边界解析，
                // 这里丢弃整个队列，属于流损坏时的保护性处理。
                {
                    std::lock_guard<std::mutex> lock(msg_queue->mutex);
                    msg_queue->buffer.clear();
                }
                ptl.clear();
                LOG_GLOBAL_WARN("Parse Message failed, code: %d", static_cast<int>(err_code));
                continue;
            }

            uint32_t sender_id = 0;
            uint32_t recv_id = 0;
            uint32_t topic = 0;
            std::string topic_str = ptl.get_header_option(HTTP_SENDER_TOPIC);

            if (parse_u32_value(ptl.get_header_option(HTTP_SENDER_OBJECT_ID), sender_id) == false ||
                parse_u32_value(ptl.get_header_option(HTTP_RECEIVER_OBJECT_ID), recv_id) == false ||
                recv_id == INVALID_ID ||
                (topic_str.empty() == false && parse_u32_value(topic_str, topic) == false)) {
                LOG_GLOBAL_WARN("Message header invalid, drop message.");
                continue;
            }

            MsgObject *target = acquire_object(recv_id);
            if (target == nullptr) {
                continue;
            }

            try {
                target->msg_handler(sender_id, ptl.get_content(), topic);
            } catch (const std::exception &e) {
                LOG_GLOBAL_ERROR("msg_handler threw exception: %s", e.what());
            } catch (...) {
                LOG_GLOBAL_ERROR("msg_handler threw unknown exception.");
            }

            release_object(recv_id);
        }
    } catch (const std::exception &e) {
        LOG_GLOBAL_ERROR("Message forwarding center threw exception: %s", e.what());
    } catch (...) {
        LOG_GLOBAL_ERROR("Message forwarding center threw unknown exception.");
    }

    active_workers_.fetch_sub(1);
    stop_cv_.notify_all();
    return nullptr;
}

// 消息对象对外接口
MsgObject::MsgObject(void)
:id_(INVALID_ID)
{
    // 初始化消息总线系统
    if (MsgObject::start() != 0) {
        LOG_GLOBAL_ERROR("MsgObject bus start failed.");
        return;
    }

    // 生成对象ID并注册
    if (MsgObject::register_object(this) != 0) {
        LOG_GLOBAL_ERROR("MsgObject register failed.");
        id_.store(INVALID_ID);
    }
}

MsgObject::~MsgObject(void)
{
    bool is_keeper = (this == bus_keeper_.load());

    // 先清理主题/订阅关系，再注销对象并等待正在执行的回调结束
    if (id_.load() != INVALID_ID) {
        this->detach_from_bus();
    } else {
        this->cleanup_topics();
    }

    // 哨兵对象退出时停止整个消息总线，保证 worker 不再访问静态消息缓冲区
    if (is_keeper) {
        MsgObject::stop(nullptr);
        bus_keeper_.store(nullptr);
    }
}

int
MsgObject::detach_from_bus(void)
{
    obj_id_t old_id = id_.load();
    if (old_id == INVALID_ID) {
        return 0;
    }

    this->cleanup_topics();
    id_.store(INVALID_ID);
    return MsgObject::remove_object(old_id);
}

int
MsgObject::msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic)
{
    (void)sender;
    (void)msg;
    (void)topic;
    return 0;
}

int MsgObject::send_msg(obj_id_t recv_id, basic::ByteBuffer &msg)
{
    obj_id_t sender_id = id_.load();
    if (sender_id == INVALID_ID) {
        return -1;
    }
    return MsgObject::send_msg(recv_id, 0, msg, sender_id);
}


//////////////////////// 观察者模式 ///////////////////////////////////
void
MsgObject::cleanup_topics(void)
{
    std::lock_guard<std::mutex> lock(topic_lock_);
    obj_id_t self_id = id_.load();

    // 自己创建的主题：直接删除，订阅者随主题一起失效
    for (auto iter = topic_.begin(); iter != topic_.end(); ++iter) {
        subscribe_object_.erase(*iter);
        pending_subscribe_.erase(*iter);
    }
    topic_.clear();

    // 自己订阅的主题：从所有订阅集合中移除
    for (auto iter = subscribe_object_.begin(); iter != subscribe_object_.end(); ++iter) {
        iter->second.second.erase(self_id);
    }

    // 自己还没等到 topic 创建的订阅请求也要移除
    for (auto iter = pending_subscribe_.begin(); iter != pending_subscribe_.end();) {
        iter->second.erase(self_id);
        if (iter->second.empty() == true) {
            iter = pending_subscribe_.erase(iter);
        } else {
            ++iter;
        }
    }
}

int
MsgObject::create_topic(const topic_t &topic)
{
    obj_id_t self_id = id_.load();
    if (topic == 0 || self_id == INVALID_ID) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(topic_lock_);
    if (id_.load() != self_id) {
        return -1;
    }
    if (topic_.find(topic) != topic_.end() ||
        subscribe_object_.find(topic) != subscribe_object_.end()) {
        return -1;
    }

    // 把先订阅、后创建的对象补进来
    std::set<obj_id_t> subscribers;
    auto pending_iter = pending_subscribe_.find(topic);
    if (pending_iter != pending_subscribe_.end()) {
        subscribers.swap(pending_iter->second);
        pending_subscribe_.erase(pending_iter);
    }

    try {
        topic_.insert(topic);
        std::pair<SUBSCRIBE_TOPIC_OBJECTS_MAP::iterator, bool> ret =
            subscribe_object_.emplace(topic, std::make_pair(self_id, subscribers));
        if (ret.second == false) {
            topic_.erase(topic);
            return -1;
        }
    } catch (...) {
        topic_.erase(topic);
        LOG_GLOBAL_ERROR("create_topic threw exception, topic: %u", topic);
        return -1;
    }

    return 0;
}

int
MsgObject::delete_topic(const topic_t &topic)
{
    obj_id_t self_id = id_.load();
    if (topic == 0 || self_id == INVALID_ID) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(topic_lock_);
    if (id_.load() != self_id) {
        return -1;
    }
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter != subscribe_object_.end()) {
        if (subscribe_iter->second.first != self_id) {
            return -1;
        }
        subscribe_object_.erase(subscribe_iter);
    }

    topic_.erase(topic);
    return 0;
}

int
MsgObject::publish_msg(const topic_t &topic, const basic::ByteBuffer &msg)
{
    obj_id_t self_id = id_.load();
    if (topic == 0 || self_id == INVALID_ID) {
        return -1;
    }

    // 在锁内复制订阅者快照，发送消息时不再持有 topic_lock_，
    // 避免发布过程中 delete_topic/unsubscribe 造成迭代器失效。
    std::set<obj_id_t> subscribers;
    {
        std::lock_guard<std::mutex> lock(topic_lock_);
        if (id_.load() != self_id) {
            return -1;
        }
        auto subscribe_iter = subscribe_object_.find(topic);
        if (subscribe_iter == subscribe_object_.end()) {
            return -1;
        }
        if (subscribe_iter->second.first != self_id) {
            return -1;
        }
        subscribers = subscribe_iter->second.second;
    }

    int sum = 0;
    for (auto iter = subscribers.begin(); iter != subscribers.end(); ++iter) {
        int ret = MsgObject::send_msg(*iter, topic, msg, self_id);
        if (ret >= 0) {
            ++sum;
        }
    }

    return sum;
}

int
MsgObject::subscribe_to_topic(const topic_t &topic)
{
    obj_id_t self_id = id_.load();
    if (topic == 0 || self_id == INVALID_ID) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(topic_lock_);
    if (id_.load() != self_id) {
        return -1;
    }
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter != subscribe_object_.end()) {
        subscribe_iter->second.second.insert(self_id);
        return 0;
    }

    // topic 后创建：先挂起，topic 创建成功后自动订阅
    pending_subscribe_[topic].insert(self_id);
    return 0;
}

int
MsgObject::unsubscribe_topic(const topic_t &topic)
{
    obj_id_t self_id = id_.load();
    if (topic == 0 || self_id == INVALID_ID) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(topic_lock_);
    if (id_.load() != self_id) {
        return 0;
    }
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter != subscribe_object_.end()) {
        subscribe_iter->second.second.erase(self_id);
    }

    auto pending_iter = pending_subscribe_.find(topic);
    if (pending_iter != pending_subscribe_.end()) {
        pending_iter->second.erase(self_id);
        if (pending_iter->second.empty() == true) {
            pending_subscribe_.erase(pending_iter);
        }
    }

    return 0;
}

obj_id_t
MsgObject::get_topic_publisher(const topic_t &topic)
{
    std::lock_guard<std::mutex> lock(topic_lock_);
    auto subscribe_iter = subscribe_object_.find(topic);
    if (subscribe_iter == subscribe_object_.end()) {
        return INVALID_ID;
    }

    return subscribe_iter->second.first;
}

}
