/// Determinism tests - verify that replaying the same order sequence
/// produces identical results every time

#include <gtest/gtest.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>
#include <random>
#include <vector>

using namespace hyperliquid;

class DeterminismTest : public ::testing::Test {
protected:
  PriceBand band_{100, 200, 1};

  // Generate a sequence of orders
  std::vector<OrderCommand> generate_orders(size_t count, uint64_t seed) {
    std::vector<OrderCommand> orders;
    orders.reserve(count);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<Tick> price_dist(110, 190);
    std::uniform_int_distribution<Quantity> qty_dist(1, 50);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> type_dist(0, 3); // 0-2: limit, 3: cancel

    for (size_t i = 0; i < count; ++i) {
      OrderCommand cmd{};
      cmd.order_id = i + 1;
      cmd.user_id = (i % 100) + 1;
      cmd.symbol_id = 1;
      cmd.recv_ts = i * 1000; // Monotonic timestamps

      int action = type_dist(rng);
      if (action < 3 || i < 10) { // First 10 are always new orders
        cmd.type = CommandType::NewOrder;
        cmd.price_ticks = price_dist(rng);
        cmd.qty = qty_dist(rng);
        cmd.side = (side_dist(rng) == 0) ? Side::Bid : Side::Ask;
        cmd.order_type = OrderType::Limit;
        cmd.tif = TimeInForce::GTC;
      } else {
        // Cancel a random earlier order
        cmd.type = CommandType::CancelOrder;
        cmd.order_id = (i % (i / 2)) + 1; // Cancel an earlier order
      }

      orders.push_back(cmd);
    }

    return orders;
  }

  // Execute orders and collect results
  struct ExecutionResult {
    std::vector<TradeEvent> trades;
    Tick final_best_bid;
    Tick final_best_ask;
  };

  ExecutionResult execute_orders(const std::vector<OrderCommand> &orders) {
    OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band_),
                                     PriceLevelsArray(band_));

    ExecutionResult result;
    book.set_on_trade([&](const TradeEvent &t) { result.trades.push_back(t); });

    for (const auto &cmd : orders) {
      switch (cmd.type) {
      case CommandType::NewOrder:
        if (cmd.order_type == OrderType::Limit) {
          book.submit_limit(cmd);
        } else {
          book.submit_market(cmd);
        }
        break;
      case CommandType::CancelOrder:
        book.cancel(cmd.order_id);
        break;
      case CommandType::ModifyOrder:
        book.modify(cmd.order_id, cmd.price_ticks, cmd.qty);
        break;
      }
    }

    result.final_best_bid = book.best_bid();
    result.final_best_ask = book.best_ask();
    return result;
  }
};

TEST_F(DeterminismTest, SameInputSameOutput) {
  // Generate same sequence twice with same seed
  auto orders1 = generate_orders(100, 42);
  auto orders2 = generate_orders(100, 42);

  // Execute both sequences
  auto result1 = execute_orders(orders1);
  auto result2 = execute_orders(orders2);

  // Verify identical results
  EXPECT_EQ(result1.final_best_bid, result2.final_best_bid);
  EXPECT_EQ(result1.final_best_ask, result2.final_best_ask);

  ASSERT_EQ(result1.trades.size(), result2.trades.size());
  for (size_t i = 0; i < result1.trades.size(); ++i) {
    EXPECT_EQ(result1.trades[i].taker_id, result2.trades[i].taker_id);
    EXPECT_EQ(result1.trades[i].maker_id, result2.trades[i].maker_id);
    EXPECT_EQ(result1.trades[i].price_ticks, result2.trades[i].price_ticks);
    EXPECT_EQ(result1.trades[i].qty, result2.trades[i].qty);
  }
}

TEST_F(DeterminismTest, DifferentSeedsDifferentOutput) {
  auto orders1 = generate_orders(100, 42);
  auto orders2 = generate_orders(100, 12345); // Different seed

  auto result1 = execute_orders(orders1);
  auto result2 = execute_orders(orders2);

  // Very unlikely to be identical with different seeds
  bool trades_different = (result1.trades.size() != result2.trades.size());
  if (!trades_different && !result1.trades.empty()) {
    trades_different =
        (result1.trades[0].price_ticks != result2.trades[0].price_ticks);
  }

  EXPECT_TRUE(trades_different ||
              result1.final_best_bid != result2.final_best_bid ||
              result1.final_best_ask != result2.final_best_ask);
}

TEST_F(DeterminismTest, ReplayMultipleTimes) {
  const size_t NUM_REPLAYS = 5;
  auto orders = generate_orders(200, 9999);

  std::vector<ExecutionResult> results;
  for (size_t i = 0; i < NUM_REPLAYS; ++i) {
    results.push_back(execute_orders(orders));
  }

  // All replays should produce identical results
  for (size_t i = 1; i < NUM_REPLAYS; ++i) {
    EXPECT_EQ(results[0].final_best_bid, results[i].final_best_bid);
    EXPECT_EQ(results[0].final_best_ask, results[i].final_best_ask);
    ASSERT_EQ(results[0].trades.size(), results[i].trades.size());

    for (size_t j = 0; j < results[0].trades.size(); ++j) {
      EXPECT_EQ(results[0].trades[j].taker_id, results[i].trades[j].taker_id)
          << "Mismatch at replay " << i << ", trade " << j;
      EXPECT_EQ(results[0].trades[j].maker_id, results[i].trades[j].maker_id);
      EXPECT_EQ(results[0].trades[j].price_ticks,
                results[i].trades[j].price_ticks);
      EXPECT_EQ(results[0].trades[j].qty, results[i].trades[j].qty);
    }
  }
}

TEST_F(DeterminismTest, PriceTimePriorityIsConsistent) {
  // Test that price-time priority is deterministic
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band_),
                                   PriceLevelsArray(band_));

  std::vector<TradeEvent> trades;
  book.set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  // Add multiple bids at same price
  for (int i = 1; i <= 5; ++i) {
    OrderCommand bid{};
    bid.order_id = i;
    bid.user_id = i;
    bid.price_ticks = 150;
    bid.qty = 10;
    bid.side = Side::Bid;
    bid.order_type = OrderType::Limit;
    bid.tif = TimeInForce::GTC;
    bid.recv_ts = i * 1000; // Ordered timestamps
    book.submit_limit(bid);
  }

  // Sell should match in FIFO order (1, 2, 3...)
  OrderCommand sell{};
  sell.order_id = 100;
  sell.user_id = 999;
  sell.price_ticks = 145;
  sell.qty = 25; // Match first 2.5 orders
  sell.side = Side::Ask;
  sell.order_type = OrderType::Limit;
  sell.tif = TimeInForce::GTC;
  book.submit_limit(sell);

  // Verify FIFO order: maker 1, then 2, then partial 3
  ASSERT_EQ(trades.size(), 3);
  EXPECT_EQ(trades[0].maker_id, 1);
  EXPECT_EQ(trades[0].qty, 10);
  EXPECT_EQ(trades[1].maker_id, 2);
  EXPECT_EQ(trades[1].qty, 10);
  EXPECT_EQ(trades[2].maker_id, 3);
  EXPECT_EQ(trades[2].qty, 5); // Partial fill
}

TEST_F(DeterminismTest, CancelDoesNotAffectOtherOrders) {
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band_),
                                   PriceLevelsArray(band_));

  // Add orders 1, 2, 3 at same price
  for (int i = 1; i <= 3; ++i) {
    OrderCommand bid{};
    bid.order_id = i;
    bid.user_id = i;
    bid.price_ticks = 150;
    bid.qty = 10;
    bid.side = Side::Bid;
    bid.order_type = OrderType::Limit;
    bid.tif = TimeInForce::GTC;
    book.submit_limit(bid);
  }

  // Cancel order 2
  book.cancel(2);

  std::vector<TradeEvent> trades;
  book.set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  // Sell 15 - should match 1 (10) then 3 (5)
  OrderCommand sell{};
  sell.order_id = 100;
  sell.user_id = 999;
  sell.price_ticks = 145;
  sell.qty = 15;
  sell.side = Side::Ask;
  sell.order_type = OrderType::Limit;
  sell.tif = TimeInForce::GTC;
  book.submit_limit(sell);

  ASSERT_EQ(trades.size(), 2);
  EXPECT_EQ(trades[0].maker_id, 1);
  EXPECT_EQ(trades[0].qty, 10);
  EXPECT_EQ(trades[1].maker_id, 3); // Order 2 was cancelled
  EXPECT_EQ(trades[1].qty, 5);
}

TEST_F(DeterminismTest, LargeOrderSequence) {
  // Stress test with larger sequence
  auto orders = generate_orders(1000, 777);

  auto result1 = execute_orders(orders);
  auto result2 = execute_orders(orders);

  EXPECT_EQ(result1.final_best_bid, result2.final_best_bid);
  EXPECT_EQ(result1.final_best_ask, result2.final_best_ask);
  EXPECT_EQ(result1.trades.size(), result2.trades.size());
}
