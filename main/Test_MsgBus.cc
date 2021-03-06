#include "util.h"
#include "gtest/gtest.h"

using namespace util;
using namespace basic;

namespace my {
namespace project {
namespace {

// 创建消息测试类
class MsgTest_A : public MsgObject {
public:
    MsgTest_A(void) {
        std::cout << "MsgTest_A_ID: " << id() << std::endl;
        clear();
    }
    ~MsgTest_A(void) {}

    void clear(void) {
        is_ok = true;
        recv_msg_.clear();
        sender_id_ = INVALID_ID;
        recv_count = 0;
    }

    virtual int msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic) {
        if (is_ok == true) {
            if (sender == sender_id_ && msg == recv_msg_) {
                ++recv_count;
            } else {
                is_ok = false;
            }
        }
        return 0;
    }

    static void* msg_test_A_send_func(void *arg) {
        if (arg == nullptr) {
            return nullptr;
        }

        MsgTest_A *msg_test_ptr = static_cast<MsgTest_A*>(arg);
        msg_test_ptr->recv_msg_.write_string("Hello, world!-----1");
        for (int i = 0; i < 5000; ++i) {
            msg_test_ptr->send_msg(msg_test_ptr->multi_thread_recv_id_, msg_test_ptr->recv_msg_);
        }
        return nullptr;
    }
public:
    bool is_ok = true;
    basic::ByteBuffer recv_msg_;
    obj_id_t sender_id_;
    obj_id_t multi_thread_recv_id_;

    int recv_count = 0;
};

class MsgTest_B : public MsgObject {
public:
    MsgTest_B(void) {
        std::cout << "MsgTest_B_ID: " << id() << std::endl;
    }
    ~MsgTest_B(void) {}

    void clear(void) {
        is_ok = true;
        recv_msg_.clear();
        sender_id_ = INVALID_ID;
        recv_count = 0;
    }

    virtual int msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic) {
        if (is_ok == true) {
            if (sender == sender_id_ && msg == recv_msg_) {
                ++recv_count;
            } else {
                is_ok = false;
            }
        }
        return 0;
    }
    
    static void* msg_test_B_send_func(void *arg) {
        if (arg == nullptr) {
            return nullptr;
        }

        MsgTest_B *msg_test_ptr = static_cast<MsgTest_B*>(arg);
        msg_test_ptr->recv_msg_.write_string("Hello, world!-----1");
        for (int i = 0; i < 5000; ++i) {
            msg_test_ptr->send_msg(msg_test_ptr->multi_thread_recv_id_, msg_test_ptr->recv_msg_);
        }
        return nullptr;
    }
public:
    bool is_ok = true;
    basic::ByteBuffer recv_msg_;
    obj_id_t sender_id_;
    obj_id_t multi_thread_recv_id_;

    int recv_count = 0;
};

class MsgBusTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Code here will be called immediately after the constructor (right
        // before each test).
    }

    void TearDown() override {
        // Code here will be called immediately after each test (right
        // before the destructor).
    }
};

TEST_F(MsgBusTest, SingleThreadSendAndRecv)
{
    basic::ByteBuffer send_msg_data;
    MsgTest_A msg_A;
    MsgTest_B msg_B;

    // 单个测试
    msg_B.sender_id_ = msg_A.id();
    msg_B.recv_msg_.write_string("Hello, world!-----1");
    msg_A.send_msg(msg_B.id(), msg_B.recv_msg_);
    
    os::Time::sleep(300);
    ASSERT_EQ(msg_B.is_ok, true);
    ASSERT_EQ(msg_B.recv_count, 1);

    msg_A.sender_id_ = msg_B.id();
    msg_A.recv_msg_.write_string("Hello, world!-----2");
    msg_B.send_msg(msg_A.id(), msg_A.recv_msg_);

    os::Time::sleep(30);
    ASSERT_EQ(msg_A.is_ok, true);
    ASSERT_EQ(msg_A.recv_count, 1);

    // 单个对象发送多条消息
    msg_A.clear();
    msg_B.clear();
    msg_A.sender_id_ = msg_B.id();
    msg_A.recv_msg_.write_string("Hello, world!-----1");
    for (int i = 0; i < 5000; ++i) {
        msg_B.send_msg(msg_A.id(), msg_A.recv_msg_);
    }
    
    os::Time::sleep(10000);
    EXPECT_EQ(msg_A.is_ok, true);
    EXPECT_EQ(msg_A.recv_count, 5000);
}

TEST_F(MsgBusTest, MultiThreadSendAndRecv)
{
    basic::ByteBuffer send_msg_data;
    MsgTest_A msg_A;
    MsgTest_B msg_B;

    msg_B.sender_id_ = msg_A.id();
    msg_B.recv_msg_.write_string("Hello, world!-----1");
    msg_A.multi_thread_recv_id_ = msg_B.id();

    os::ThreadPool test_pool;
    os::Task task;
    task.work_func = MsgTest_A::msg_test_A_send_func;
    task.thread_arg = &msg_A;
    test_pool.add_task(task);

    os::Time::sleep(10000);
    EXPECT_EQ(msg_B.is_ok, true);
    EXPECT_EQ(msg_B.recv_count, 5000);
}

}
}
}


int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}