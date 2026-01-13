#include <gtest/gtest.h>
#include <hyperliquid/mempool.h>

using namespace hyperliquid;

struct TestObject {
  int value;
  char data[64];
};

TEST(MempoolTest, AllocateAndFree) {
  SlabPool<TestObject> pool(1);

  TestObject *obj = pool.alloc();
  ASSERT_NE(obj, nullptr);
  EXPECT_EQ(pool.in_use(), 1);

  pool.free(obj);
  EXPECT_EQ(pool.in_use(), 0);
}

TEST(MempoolTest, MultipleAllocations) {
  SlabPool<TestObject> pool(2);

  std::vector<TestObject *> objs;
  for (int i = 0; i < 100; ++i) {
    objs.push_back(pool.alloc());
  }

  EXPECT_EQ(pool.in_use(), 100);

  for (auto *obj : objs) {
    pool.free(obj);
  }

  EXPECT_EQ(pool.in_use(), 0);
}
