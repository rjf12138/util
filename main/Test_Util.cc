#include "util.h"
#include "gtest/gtest.h"

#include <atomic>

using namespace util;
using namespace basic;

namespace {

struct TimerContext {
    Timer *timer;
    std::atomic<int> *count;
};

void* timer_count_callback(void *arg)
{
    TimerContext *ctx = static_cast<TimerContext*>(arg);
    if (ctx != nullptr && ctx->count != nullptr) {
        ctx->count->fetch_add(1);
    }
    return nullptr;
}

void* timer_add_from_callback(void *arg)
{
    TimerContext *ctx = static_cast<TimerContext*>(arg);
    if (ctx == nullptr || ctx->timer == nullptr) {
        return nullptr;
    }

    TimerEvent_t event;
    event.wait_time = 20;
    event.TimeEvent_arg = ctx;
    event.TimeEvent_callback = timer_count_callback;
    event.attr = TimerEventAttr_Exit;
    ctx->timer->add(event);
    ctx->count->fetch_add(1);
    return nullptr;
}

class Receiver : public MsgObject {
public:
    ~Receiver(void) {
        this->detach_from_bus();
    }

    virtual int msg_handler(obj_id_t sender, basic::ByteBuffer &msg, topic_t topic) {
        (void)sender;
        (void)msg;
        last_topic_.store(topic);
        count_.fetch_add(1);
        return 0;
    }

    bool wait_count(int target, int timeout_ms) {
        int waited = 0;
        while (count_.load() < target && waited < timeout_ms) {
            os::Time::sleep(5);
            waited += 5;
        }
        return count_.load() >= target;
    }

    int count(void) const { return count_.load(); }
    topic_t last_topic(void) const { return last_topic_.load(); }

private:
    std::atomic<int> count_{0};
    std::atomic<topic_t> last_topic_{0};
};

TEST(TimerTest, ImmediateDestroy)
{
    // 构造函数启动线程后立刻析构，不能挂死
    for (int i = 0; i < 10; ++i) {
        Timer timer;
    }
    SUCCEED();
}

TEST(TimerTest, NullCallbackRejected)
{
    Timer timer;
    TimerEvent_t event;
    event.wait_time = 20;
    event.TimeEvent_callback = nullptr;
    EXPECT_EQ(timer.add(event), static_cast<timer_id_t>(INVALID_TIMER_ID));
}

TEST(TimerTest, CallbackCanUseTimerApi)
{
    Timer timer;
    std::atomic<int> count(0);
    TimerContext ctx;
    ctx.timer = &timer;
    ctx.count = &count;

    TimerEvent_t event;
    event.wait_time = 20;
    event.TimeEvent_arg = &ctx;
    event.TimeEvent_callback = timer_add_from_callback;
    event.attr = TimerEventAttr_Exit;

    ASSERT_NE(timer.add(event), static_cast<timer_id_t>(INVALID_TIMER_ID));
    os::Time::sleep(150);
    EXPECT_GE(count.load(), 1);
}

TEST(TimerTest, CancelBeforeExpire)
{
    Timer timer;
    std::atomic<int> count(0);
    TimerContext ctx;
    ctx.timer = &timer;
    ctx.count = &count;

    TimerEvent_t event;
    event.wait_time = 100;
    event.TimeEvent_arg = &ctx;
    event.TimeEvent_callback = timer_count_callback;
    event.attr = TimerEventAttr_Exit;

    timer_id_t id = timer.add(event);
    ASSERT_NE(id, static_cast<timer_id_t>(INVALID_TIMER_ID));
    EXPECT_EQ(timer.cancel(id), 1);

    os::Time::sleep(200);
    EXPECT_EQ(count.load(), 0);
}

TEST(MsgObjectTest, SubscribeBeforeCreateTopic)
{
    Receiver subscriber;
    MsgObject publisher;

    const topic_t topic = 12345;
    ASSERT_EQ(subscriber.subscribe_to_topic(topic), 0);
    ASSERT_EQ(publisher.create_topic(topic), 0);

    basic::ByteBuffer msg;
    msg.write_string("hello topic");
    EXPECT_EQ(publisher.publish_msg(topic, msg), 1);
    EXPECT_TRUE(subscriber.wait_count(1, 2000));
    EXPECT_EQ(subscriber.last_topic(), topic);

    subscriber.detach_from_bus();
    publisher.detach_from_bus();
}

TEST(MsgObjectTest, PublisherDestroyRemovesTopic)
{
    const topic_t topic = 23456;
    {
        MsgObject publisher;
        ASSERT_EQ(publisher.create_topic(topic), 0);
    }

    MsgObject another_publisher;
    EXPECT_EQ(another_publisher.create_topic(topic), 0);
    another_publisher.detach_from_bus();
}

TEST(MsgObjectTest, DetachStopsReceiving)
{
    Receiver receiver;
    obj_id_t old_id = receiver.id();

    basic::ByteBuffer msg;
    msg.write_string("before detach");
    ASSERT_EQ(receiver.send_msg(old_id, msg), 0);
    ASSERT_TRUE(receiver.wait_count(1, 2000));

    ASSERT_EQ(receiver.detach_from_bus(), 0);
    EXPECT_EQ(receiver.id(), static_cast<obj_id_t>(INVALID_ID));
    EXPECT_EQ(receiver.send_msg(old_id, msg), -1);
}

} // namespace

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
