#include <gtest/gtest.h>
#include <hyperliquid/spsc_queue.h>
#include <thread>

using namespace hyperliquid;

TEST(SPSCQueueTest, BasicPushPop) {
  SPSCQueue<int, 16> queue;

  EXPECT_TRUE(queue.empty());
  EXPECT_TRUE(queue.push(42));
  EXPECT_FALSE(queue.empty());

  int value;
  EXPECT_TRUE(queue.pop(value));
  EXPECT_EQ(value, 42);
  EXPECT_TRUE(queue.empty());
}

TEST(SPSCQueueTest, FillAndDrain) {
  SPSCQueue<int, 16> queue;

  // Fill to capacity (N-1 elements)
  for (int i = 0; i < 15; ++i) {
    EXPECT_TRUE(queue.push(i));
  }

  // Should be full
  EXPECT_FALSE(queue.push(999));

  // Drain
  for (int i = 0; i < 15; ++i) {
    int value;
    EXPECT_TRUE(queue.pop(value));
    EXPECT_EQ(value, i);
  }

  EXPECT_TRUE(queue.empty());
}

TEST(SPSCQueueTest, ConcurrentProducerConsumer) {
  SPSCQueue<int, 1024> queue;
  constexpr int NUM_ITEMS = 10000;

  std::thread producer([&queue]() {
    for (int i = 0; i < NUM_ITEMS; ++i) {
      while (!queue.push(i))
        std::this_thread::yield();
    }
  });

  std::thread consumer([&queue]() {
    for (int i = 0; i < NUM_ITEMS; ++i) {
      int value;
      while (!queue.pop(value))
        std::this_thread::yield();
      EXPECT_EQ(value, i);
    }
  });

  producer.join();
  consumer.join();

  EXPECT_TRUE(queue.empty());
}
