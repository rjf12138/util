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
    MsgTest_A(void) {}
    ~MsgTest_A(void) {}

    virtual int msg_handler(obj_id_t sender, const basic::ByteBuffer &msg) {
        recv_msg_ = msg;
        std::cout << "MsgTest_A: " <<  recv_msg_.str() << std::endl;
        sender_id_ = sender;
        return 0;
    }

public:
    basic::ByteBuffer recv_msg_;
    obj_id_t sender_id_;
};

class MsgTest_B : public MsgObject {
public:
    MsgTest_B(void) {}
    ~MsgTest_B(void) {}

    virtual int msg_handler(obj_id_t sender, const basic::ByteBuffer &msg) {
        recv_msg_ = msg;
        std::cout << "MsgTest_B: " << recv_msg_.str() << std::endl;
        sender_id_ = sender;
        return 0;
    }
    
public:
    basic::ByteBuffer recv_msg_;
    obj_id_t sender_id_;
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

    std::string msg = "Hello, world!-----1";
    send_msg_data.write_string(msg);
    msg_A.send_msg(msg_B.id(), send_msg_data);

    os::Time::sleep(100);
    ASSERT_EQ(send_msg_data, msg_B.recv_msg_);
    ASSERT_EQ(msg_A.id(), msg_B.sender_id_);


    send_msg_data.clear();
    msg = "Hello, world!-----2";
    send_msg_data.write_string(msg);
    msg_B.send_msg(msg_A.id(), send_msg_data);

    os::Time::sleep(100);
    ASSERT_EQ(send_msg_data, msg_A.recv_msg_);
    ASSERT_EQ(msg_A.id(), msg_A.sender_id_);
}

}
}
}


int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}