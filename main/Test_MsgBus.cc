#include "util.h"
#include "gtest/gtest.h"

#include <atomic>
#include <mutex>
#include <thread>

using namespace util;
using namespace basic;

namespace my {
namespace project {
namespace {

const char *kText1 = "Hello, world!-----1";
const char *kText2 = "Hello, world!-----2";

// 创建消息测试类
class MsgTest_A : public MsgObject {
public:
    MsgTest_A(void) {
        std::cout << "MsgTest_A_ID: " << id() << std::endl;
        clear();
    }

    ~MsgTest_A(void) {
        // 在派生类析构中先注销，等待正在执行的回调结束，避免析构期间还在派发消息
        this->detach_from_bus();
    }

    void clear(void) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        is_ok_.store(true);
        recv_msg_.clear();
        sender_id_.store(INVALID_ID);
        recv_count_.store(0);
    }

    void set_expected(obj_id_t sender, const std::string &text) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        sender_id_.store(sender);
        recv_msg_.clear();
        recv_msg_.write_string(text);
    }

    bool is_ok(void) const { return is_ok_.load(); }
    int recv_count(void) const { return recv_count_.load(); }
    bool wait_count(int target, int timeout_ms) const {
        int waited = 0;
        while (recv_count_.load() < target && waited < timeout_ms) {
            os::Time::sleep(5);
            waited += 5;
        }
        return recv_count_.load() >= target;
    }
    virtual int msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic) {
        (void)topic;
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (is_ok_.load() == true) {
            if (sender == sender_id_.load() && msg == recv_msg_) {
                recv_count_.fetch_add(1);
            } else {
                is_ok_.store(false);
            }
        }
        return 0;
    }

    static void* msg_test_A_send_func(void *arg) {
        if (arg == nullptr) {
            return nullptr;
        }

        MsgTest_A *msg_test_ptr = static_cast<MsgTest_A*>(arg);
        basic::ByteBuffer msg;
        msg.write_string(kText1);
        for (int i = 0; i < 5000; ++i) {
            msg_test_ptr->send_msg(msg_test_ptr->multi_thread_recv_id_.load(), msg);
        }
        msg_test_ptr->send_finished_.store(true);
        return nullptr;
    }

public:
    std::atomic<obj_id_t> multi_thread_recv_id_{INVALID_ID};
    std::atomic<bool> send_finished_{false};

private:
    std::atomic<bool> is_ok_{true};
    std::atomic<int> recv_count_{0};
    std::atomic<obj_id_t> sender_id_{INVALID_ID};
    std::mutex data_mutex_;
    basic::ByteBuffer recv_msg_;
};

class MsgTest_B : public MsgObject {
public:
    MsgTest_B(void) {
        std::cout << "MsgTest_B_ID: " << id() << std::endl;
        clear();
    }

    ~MsgTest_B(void) {
        this->detach_from_bus();
    }

    void clear(void) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        is_ok_.store(true);
        recv_msg_.clear();
        sender_id_.store(INVALID_ID);
        recv_count_.store(0);
    }

    void set_expected(obj_id_t sender, const std::string &text) {
        std::lock_guard<std::mutex> lock(data_mutex_);
        sender_id_.store(sender);
        recv_msg_.clear();
        recv_msg_.write_string(text);
    }

    bool is_ok(void) const { return is_ok_.load(); }
    int recv_count(void) const { return recv_count_.load(); }
    bool wait_count(int target, int timeout_ms) const {
        int waited = 0;
        while (recv_count_.load() < target && waited < timeout_ms) {
            os::Time::sleep(5);
            waited += 5;
        }
        return recv_count_.load() >= target;
    }

    virtual int msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic) {
        (void)topic;
        std::lock_guard<std::mutex> lock(data_mutex_);
        if (is_ok_.load() == true) {
            if (sender == sender_id_.load() && msg == recv_msg_) {
                recv_count_.fetch_add(1);
            } else {
                is_ok_.store(false);
            }
        }
        return 0;
    }

private:
    std::atomic<bool> is_ok_{true};
    std::atomic<int> recv_count_{0};
    std::atomic<obj_id_t> sender_id_{INVALID_ID};
    std::mutex data_mutex_;
    basic::ByteBuffer recv_msg_;
};

class MsgBusTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(MsgBusTest, SingleThreadSendAndRecv)
{
    MsgTest_A msg_A;
    MsgTest_B msg_B;

    // 单个测试
    basic::ByteBuffer msg1;
    msg1.write_string(kText1);
    msg_B.set_expected(msg_A.id(), kText1);
    ASSERT_EQ(msg_A.send_msg(msg_B.id(), msg1), 0);

    ASSERT_TRUE(msg_B.wait_count(1, 2000));
    ASSERT_TRUE(msg_B.is_ok());
    ASSERT_EQ(msg_B.recv_count(), 1);

    // 反向发送
    basic::ByteBuffer msg2;
    msg2.write_string(kText2);
    msg_A.set_expected(msg_B.id(), kText2);
    ASSERT_EQ(msg_B.send_msg(msg_A.id(), msg2), 0);

    ASSERT_TRUE(msg_A.wait_count(1, 2000));
    ASSERT_TRUE(msg_A.is_ok());
    ASSERT_EQ(msg_A.recv_count(), 1);

    // 单个对象连续发送多条消息
    msg_A.clear();
    msg_B.clear();
    basic::ByteBuffer msg3;
    msg3.write_string(kText1);
    msg_A.set_expected(msg_B.id(), kText1);
    for (int i = 0; i < 5000; ++i) {
        ASSERT_EQ(msg_B.send_msg(msg_A.id(), msg3), 0);
    }

    ASSERT_TRUE(msg_A.wait_count(5000, 10000));
    ASSERT_TRUE(msg_A.is_ok());
    ASSERT_EQ(msg_A.recv_count(), 5000);

    msg_A.detach_from_bus();
    msg_B.detach_from_bus();
}

TEST_F(MsgBusTest, MultiThreadSendAndRecv)
{
    MsgTest_A msg_A;
    MsgTest_B msg_B;

    msg_B.set_expected(msg_A.id(), kText1);
    msg_A.multi_thread_recv_id_.store(msg_B.id());

    std::thread sender(MsgTest_A::msg_test_A_send_func, &msg_A);

    bool recv_ok = msg_B.wait_count(5000, 10000);
    int waited = 0;
    while (msg_A.send_finished_.load() == false && waited < 5000) {
        os::Time::sleep(5);
        waited += 5;
    }
    bool send_ok = msg_A.send_finished_.load();

    sender.join();

    ASSERT_TRUE(send_ok);
    ASSERT_TRUE(recv_ok);
    ASSERT_TRUE(msg_B.is_ok());
    ASSERT_EQ(msg_B.recv_count(), 5000);

    msg_A.detach_from_bus();
    msg_B.detach_from_bus();
}

}
}
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
