/// Tests for advanced order types: GTD, Iceberg, and Stop orders

#include <gtest/gtest.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>

using namespace hyperliquid;

class AdvancedOrdersTest : public ::testing::Test {
protected:
  void SetUp() override {
    PriceBand band(100, 200, 1);
    book_ = std::make_unique<OrderBook<PriceLevelsArray>>(
        1, PriceLevelsArray(band), PriceLevelsArray(band));
  }

  std::unique_ptr<OrderBook<PriceLevelsArray>> book_;
};

// =============================================================================
// GTD (Good-Till-Date) Tests
// =============================================================================

TEST_F(AdvancedOrdersTest, GTDOrderWithExpiry) {
  // Note: GTD expiry checking would typically be done in matching engine loop
  // This test verifies the fields are properly set

  OrderCommand cmd{};
  cmd.order_id = 1;
  cmd.user_id = 100;
  cmd.price_ticks = 150;
  cmd.qty = 10;
  cmd.side = Side::Bid;
  cmd.order_type = OrderType::Limit;
  cmd.tif = TimeInForce::GTD;
  cmd.expiry_ts = 1700000000000000000ULL; // Some future nanosecond timestamp

  auto result = book_->submit_limit(cmd);
  EXPECT_EQ(result.remaining, 10); // Order rests
  EXPECT_EQ(book_->best_bid(), 150);
}

TEST_F(AdvancedOrdersTest, GTDTimeInForceEnumValue) {
  EXPECT_EQ(static_cast<uint8_t>(TimeInForce::GTD), 3);
  EXPECT_STREQ(to_string(TimeInForce::GTD), "GTD");
}

// =============================================================================
// Iceberg Order Tests
// =============================================================================

TEST_F(AdvancedOrdersTest, IcebergOrderFlagsAreSet) {
  OrderCommand cmd{};
  cmd.order_id = 1;
  cmd.user_id = 100;
  cmd.price_ticks = 150;
  cmd.qty = 100; // Total quantity
  cmd.side = Side::Bid;
  cmd.order_type = OrderType::Limit;
  cmd.tif = TimeInForce::GTC;
  cmd.flags = OrderFlags::ICEBERG;
  cmd.display_qty = 10; // Show only 10 at a time

  auto result = book_->submit_limit(cmd);
  EXPECT_EQ(result.remaining, 100);
  EXPECT_EQ(book_->best_bid(), 150);
}

TEST_F(AdvancedOrdersTest, IcebergFlagValue) {
  EXPECT_EQ(OrderFlags::ICEBERG, 1 << 3);
  EXPECT_EQ(OrderFlags::ICEBERG, 8);
}

// =============================================================================
// Stop Order Tests
// =============================================================================

TEST_F(AdvancedOrdersTest, StopOrderTypesExist) {
  EXPECT_EQ(static_cast<uint8_t>(OrderType::StopLimit), 2);
  EXPECT_EQ(static_cast<uint8_t>(OrderType::StopMarket), 3);
  EXPECT_STREQ(to_string(OrderType::StopLimit), "StopLimit");
  EXPECT_STREQ(to_string(OrderType::StopMarket), "StopMarket");
}

TEST_F(AdvancedOrdersTest, StopFlagValue) {
  EXPECT_EQ(OrderFlags::STOP, 1 << 4);
  EXPECT_EQ(OrderFlags::STOP, 16);
}

TEST_F(AdvancedOrdersTest, StopOrderCommandFields) {
  OrderCommand cmd{};
  cmd.order_id = 1;
  cmd.user_id = 100;
  cmd.price_ticks = 145; // Limit price (for stop-limit)
  cmd.stop_price = 150;  // Trigger price
  cmd.qty = 10;
  cmd.side = Side::Bid;
  cmd.order_type = OrderType::StopLimit;
  cmd.tif = TimeInForce::GTC;
  cmd.flags = OrderFlags::STOP;

  EXPECT_EQ(cmd.stop_price, 150);
  EXPECT_EQ(cmd.order_type, OrderType::StopLimit);
  EXPECT_TRUE((cmd.flags & OrderFlags::STOP) != 0);
}

// =============================================================================
// Order Node Field Tests
// =============================================================================

TEST_F(AdvancedOrdersTest, OrderNodeHasIcebergFields) {
  OrderNode node;
  node.id = 1;
  node.user = 100;
  node.qty = 10;
  node.display_qty = 5;
  node.hidden_qty = 50;
  node.flags = OrderFlags::ICEBERG;

  EXPECT_TRUE(node.is_iceberg());
  EXPECT_EQ(node.display_qty, 5);
  EXPECT_EQ(node.hidden_qty, 50);
}

TEST_F(AdvancedOrdersTest, OrderNodeHasExpiryField) {
  OrderNode node;
  node.expiry_ts = 1700000000000000000ULL;
  EXPECT_EQ(node.expiry_ts, 1700000000000000000ULL);
}

TEST_F(AdvancedOrdersTest, OrderNodeHasStopPrice) {
  OrderNode node;
  node.stop_price = 50000;
  EXPECT_EQ(node.stop_price, 50000);
}

TEST_F(AdvancedOrdersTest, OrderNodeReplenishIceberg) {
  OrderNode node;
  node.flags = OrderFlags::ICEBERG;
  node.qty = 0;          // Display exhausted
  node.display_qty = 10; // Original display size
  node.hidden_qty = 25;  // Hidden remaining

  Quantity replenished = node.replenish();

  EXPECT_EQ(replenished, 10);     // Replenished display_qty amount
  EXPECT_EQ(node.qty, 10);        // New visible quantity
  EXPECT_EQ(node.hidden_qty, 15); // Remaining hidden
}

// =============================================================================
// Combination Tests
// =============================================================================

TEST_F(AdvancedOrdersTest, AllFlagsCanCombine) {
  uint32_t flags =
      OrderFlags::POST_ONLY | OrderFlags::STP | OrderFlags::ICEBERG;

  EXPECT_TRUE((flags & OrderFlags::POST_ONLY) != 0);
  EXPECT_TRUE((flags & OrderFlags::STP) != 0);
  EXPECT_TRUE((flags & OrderFlags::ICEBERG) != 0);
  EXPECT_FALSE((flags & OrderFlags::REDUCE_ONLY) != 0);
  EXPECT_FALSE((flags & OrderFlags::STOP) != 0);
}

TEST_F(AdvancedOrdersTest, AllOrderTypesHaveToString) {
  EXPECT_STREQ(to_string(OrderType::Limit), "Limit");
  EXPECT_STREQ(to_string(OrderType::Market), "Market");
  EXPECT_STREQ(to_string(OrderType::StopLimit), "StopLimit");
  EXPECT_STREQ(to_string(OrderType::StopMarket), "StopMarket");
}

TEST_F(AdvancedOrdersTest, AllTimeInForceHaveToString) {
  EXPECT_STREQ(to_string(TimeInForce::GTC), "GTC");
  EXPECT_STREQ(to_string(TimeInForce::IOC), "IOC");
  EXPECT_STREQ(to_string(TimeInForce::FOK), "FOK");
  EXPECT_STREQ(to_string(TimeInForce::GTD), "GTD");
}
