/// Tests for price level implementations
/// Verifies both PriceLevelsArray and PriceLevelsAVL behave correctly

#include <gtest/gtest.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>
#include <hyperliquid/price_levels_avl.h>

using namespace hyperliquid;

// =============================================================================
// PriceLevelsArray Tests
// =============================================================================

class PriceLevelsArrayTest : public ::testing::Test {
protected:
  PriceBand band_{100, 200, 1};
};

TEST_F(PriceLevelsArrayTest, InitialStateIsEmpty) {
  PriceLevelsArray levels(band_);

  EXPECT_EQ(levels.best_bid(), Sentinel::EMPTY_BID);
  EXPECT_EQ(levels.best_ask(), Sentinel::EMPTY_ASK);
  EXPECT_EQ(levels.best_level_ptr(Side::Bid), nullptr);
  EXPECT_EQ(levels.best_level_ptr(Side::Ask), nullptr);
}

TEST_F(PriceLevelsArrayTest, ValidPriceCheck) {
  PriceLevelsArray levels(band_);

  EXPECT_TRUE(levels.is_valid_price(100));
  EXPECT_TRUE(levels.is_valid_price(150));
  EXPECT_TRUE(levels.is_valid_price(200));
  EXPECT_FALSE(levels.is_valid_price(99));
  EXPECT_FALSE(levels.is_valid_price(201));
}

TEST_F(PriceLevelsArrayTest, GetLevelCreatesEmpty) {
  PriceLevelsArray levels(band_);

  auto &level = levels.get_level(150);
  EXPECT_TRUE(level.empty());
  EXPECT_FALSE(levels.has_level(150));
}

TEST_F(PriceLevelsArrayTest, SetBestUpdatesPointer) {
  PriceLevelsArray levels(band_);

  levels.set_best_bid(150);
  EXPECT_EQ(levels.best_bid(), 150);
  EXPECT_NE(levels.best_level_ptr(Side::Bid), nullptr);

  levels.set_best_ask(160);
  EXPECT_EQ(levels.best_ask(), 160);
  EXPECT_NE(levels.best_level_ptr(Side::Ask), nullptr);
}

TEST_F(PriceLevelsArrayTest, WorksWithOrderBook) {
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band_),
                                   PriceLevelsArray(band_));

  OrderCommand bid{};
  bid.order_id = 1;
  bid.user_id = 100;
  bid.price_ticks = 150;
  bid.qty = 10;
  bid.side = Side::Bid;
  bid.order_type = OrderType::Limit;
  bid.tif = TimeInForce::GTC;

  auto result = book.submit_limit(bid);
  EXPECT_EQ(result.remaining, 10);
  EXPECT_EQ(book.best_bid(), 150);
}

// =============================================================================
// PriceLevelsAVL Tests
// =============================================================================

class PriceLevelsAVLTest : public ::testing::Test {
protected:
};

TEST_F(PriceLevelsAVLTest, InitialStateIsEmpty) {
  PriceLevelsAVL levels;

  EXPECT_EQ(levels.best_bid(), Sentinel::EMPTY_BID);
  EXPECT_EQ(levels.best_ask(), Sentinel::EMPTY_ASK);
  EXPECT_EQ(levels.best_level_ptr(Side::Bid), nullptr);
  EXPECT_EQ(levels.best_level_ptr(Side::Ask), nullptr);
}

TEST_F(PriceLevelsAVLTest, ValidPriceCheck) {
  PriceLevelsAVL levels;

  // AVL accepts any valid price (not sentinels)
  EXPECT_TRUE(levels.is_valid_price(1));
  EXPECT_TRUE(levels.is_valid_price(1000000));
  EXPECT_FALSE(levels.is_valid_price(Sentinel::EMPTY_BID));
  EXPECT_FALSE(levels.is_valid_price(Sentinel::EMPTY_ASK));
}

TEST_F(PriceLevelsAVLTest, GetLevelCreatesEmpty) {
  PriceLevelsAVL levels;

  auto &level = levels.get_level(50000);
  EXPECT_TRUE(level.empty());
  EXPECT_FALSE(levels.has_level(50000));
}

TEST_F(PriceLevelsAVLTest, SetBestUpdatesPointer) {
  PriceLevelsAVL levels;

  // Need to create level first
  levels.get_level(50000);
  levels.set_best_bid(50000);
  EXPECT_EQ(levels.best_bid(), 50000);
  EXPECT_NE(levels.best_level_ptr(Side::Bid), nullptr);

  levels.get_level(50100);
  levels.set_best_ask(50100);
  EXPECT_EQ(levels.best_ask(), 50100);
  EXPECT_NE(levels.best_level_ptr(Side::Ask), nullptr);
}

TEST_F(PriceLevelsAVLTest, FindNextBid) {
  PriceLevelsAVL levels;

  // Add some orders to create non-empty levels
  OrderNode node1{1, 1, 10, 0, 0};
  OrderNode node2{2, 1, 10, 0, 0};
  OrderNode node3{3, 1, 10, 0, 0};

  levels.get_level(100).enqueue(&node1);
  levels.get_level(105).enqueue(&node2);
  levels.get_level(110).enqueue(&node3);

  EXPECT_EQ(levels.find_next_bid(110), 105);
  EXPECT_EQ(levels.find_next_bid(105), 100);
  EXPECT_EQ(levels.find_next_bid(100), Sentinel::EMPTY_BID);
}

TEST_F(PriceLevelsAVLTest, FindNextAsk) {
  PriceLevelsAVL levels;

  OrderNode node1{1, 1, 10, 0, 0};
  OrderNode node2{2, 1, 10, 0, 0};
  OrderNode node3{3, 1, 10, 0, 0};

  levels.get_level(100).enqueue(&node1);
  levels.get_level(105).enqueue(&node2);
  levels.get_level(110).enqueue(&node3);

  EXPECT_EQ(levels.find_next_ask(100), 105);
  EXPECT_EQ(levels.find_next_ask(105), 110);
  EXPECT_EQ(levels.find_next_ask(110), Sentinel::EMPTY_ASK);
}

TEST_F(PriceLevelsAVLTest, ForEachOrder) {
  PriceLevelsAVL levels;

  OrderNode node1{1, 1, 10, 0, 0};
  OrderNode node2{2, 1, 20, 0, 0};

  levels.get_level(100).enqueue(&node1);
  levels.get_level(200).enqueue(&node2);

  int count = 0;
  Quantity total_qty = 0;
  levels.for_each_order([&](Tick px, OrderNode *node) {
    count++;
    total_qty += node->qty;
  });

  EXPECT_EQ(count, 2);
  EXPECT_EQ(total_qty, 30);
}

TEST_F(PriceLevelsAVLTest, WorksWithOrderBook) {
  // Test that PriceLevelsAVL works correctly with OrderBook template
  OrderBook<PriceLevelsAVL> book(1, PriceLevelsAVL(), PriceLevelsAVL());

  // Add bid
  OrderCommand bid{};
  bid.order_id = 1;
  bid.user_id = 100;
  bid.price_ticks = 50000;
  bid.qty = 10;
  bid.side = Side::Bid;
  bid.order_type = OrderType::Limit;
  bid.tif = TimeInForce::GTC;

  auto result = book.submit_limit(bid);
  EXPECT_EQ(result.remaining, 10);
  EXPECT_EQ(book.best_bid(), 50000);

  // Add ask
  OrderCommand ask{};
  ask.order_id = 2;
  ask.user_id = 101;
  ask.price_ticks = 50100;
  ask.qty = 5;
  ask.side = Side::Ask;
  ask.order_type = OrderType::Limit;
  ask.tif = TimeInForce::GTC;

  result = book.submit_limit(ask);
  EXPECT_EQ(result.remaining, 5);
  EXPECT_EQ(book.best_ask(), 50100);
}

TEST_F(PriceLevelsAVLTest, OrderMatchingWithAVL) {
  OrderBook<PriceLevelsAVL> book(1, PriceLevelsAVL(), PriceLevelsAVL());

  std::vector<TradeEvent> trades;
  book.set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  // Add resting ask at 50100
  OrderCommand ask{};
  ask.order_id = 1;
  ask.user_id = 100;
  ask.price_ticks = 50100;
  ask.qty = 10;
  ask.side = Side::Ask;
  ask.order_type = OrderType::Limit;
  ask.tif = TimeInForce::GTC;
  book.submit_limit(ask);

  // Crossing bid at 50200
  OrderCommand bid{};
  bid.order_id = 2;
  bid.user_id = 101;
  bid.price_ticks = 50200;
  bid.qty = 5;
  bid.side = Side::Bid;
  bid.order_type = OrderType::Limit;
  bid.tif = TimeInForce::GTC;
  book.submit_limit(bid);

  ASSERT_EQ(trades.size(), 1);
  EXPECT_EQ(trades[0].taker_id, 2);
  EXPECT_EQ(trades[0].maker_id, 1);
  EXPECT_EQ(trades[0].price_ticks, 50100); // Trade at resting price
  EXPECT_EQ(trades[0].qty, 5);
}

TEST_F(PriceLevelsAVLTest, SparsePriceRange) {
  // Test with very sparse prices (AVL advantage)
  OrderBook<PriceLevelsAVL> book(1, PriceLevelsAVL(), PriceLevelsAVL());

  // Prices very far apart
  OrderCommand bid1{};
  bid1.order_id = 1;
  bid1.user_id = 100;
  bid1.price_ticks = 1000;
  bid1.qty = 10;
  bid1.side = Side::Bid;
  bid1.order_type = OrderType::Limit;
  bid1.tif = TimeInForce::GTC;
  book.submit_limit(bid1);

  OrderCommand bid2{};
  bid2.order_id = 2;
  bid2.user_id = 100;
  bid2.price_ticks = 1000000; // 1M ticks away
  bid2.qty = 10;
  bid2.side = Side::Bid;
  bid2.order_type = OrderType::Limit;
  bid2.tif = TimeInForce::GTC;
  book.submit_limit(bid2);

  EXPECT_EQ(book.best_bid(), 1000000); // Higher price is best bid
}
