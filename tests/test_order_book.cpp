#include <gtest/gtest.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>

using namespace hyperliquid;

class OrderBookTest : public ::testing::Test {
protected:
  void SetUp() override {
    PriceBand band(100, 200, 1);
    book_ = std::make_unique<OrderBook<PriceLevelsArray>>(
        1, PriceLevelsArray(band), PriceLevelsArray(band));
  }

  std::unique_ptr<OrderBook<PriceLevelsArray>> book_;
};

TEST_F(OrderBookTest, EmptyBookHasSentinelPrices) {
  EXPECT_EQ(book_->best_bid(), Sentinel::EMPTY_BID);
  EXPECT_EQ(book_->best_ask(), Sentinel::EMPTY_ASK);
  EXPECT_TRUE(book_->empty(Side::Bid));
  EXPECT_TRUE(book_->empty(Side::Ask));
}

TEST_F(OrderBookTest, LimitOrderRests) {
  OrderCommand cmd{};
  cmd.type = CommandType::NewOrder;
  cmd.order_id = 1;
  cmd.user_id = 100;
  cmd.price_ticks = 150;
  cmd.qty = 10;
  cmd.side = Side::Bid;
  cmd.order_type = OrderType::Limit;
  cmd.tif = TimeInForce::GTC;

  auto result = book_->submit_limit(cmd);

  EXPECT_EQ(result.filled, 0);
  EXPECT_EQ(result.remaining, 10);
  EXPECT_EQ(book_->best_bid(), 150);
}

TEST_F(OrderBookTest, LimitOrderCrosses) {
  // Add buy order
  OrderCommand buy{};
  buy.order_id = 1;
  buy.user_id = 100;
  buy.price_ticks = 150;
  buy.qty = 10;
  buy.side = Side::Bid;
  buy.order_type = OrderType::Limit;
  buy.tif = TimeInForce::GTC;
  book_->submit_limit(buy);

  // Add crossing sell order
  OrderCommand sell{};
  sell.order_id = 2;
  sell.user_id = 101;
  sell.price_ticks = 145;
  sell.qty = 5;
  sell.side = Side::Ask;
  sell.order_type = OrderType::Limit;
  sell.tif = TimeInForce::GTC;

  auto result = book_->submit_limit(sell);

  EXPECT_EQ(result.filled, 5);
  EXPECT_EQ(result.remaining, 0);
}

TEST_F(OrderBookTest, Cancel) {
  OrderCommand cmd{};
  cmd.order_id = 1;
  cmd.user_id = 100;
  cmd.price_ticks = 150;
  cmd.qty = 10;
  cmd.side = Side::Bid;
  cmd.order_type = OrderType::Limit;
  cmd.tif = TimeInForce::GTC;
  book_->submit_limit(cmd);

  EXPECT_TRUE(book_->cancel(1));
  EXPECT_EQ(book_->best_bid(), Sentinel::EMPTY_BID);
  EXPECT_FALSE(book_->cancel(1)); // Already cancelled
}

TEST_F(OrderBookTest, MarketOrder) {
  // Add resting limit
  OrderCommand limit{};
  limit.order_id = 1;
  limit.user_id = 100;
  limit.price_ticks = 150;
  limit.qty = 10;
  limit.side = Side::Ask;
  limit.order_type = OrderType::Limit;
  limit.tif = TimeInForce::GTC;
  book_->submit_limit(limit);

  // Market buy
  OrderCommand market{};
  market.order_id = 2;
  market.user_id = 101;
  market.qty = 5;
  market.side = Side::Bid;
  market.order_type = OrderType::Market;

  auto result = book_->submit_market(market);

  EXPECT_EQ(result.filled, 5);
  EXPECT_EQ(result.remaining, 0);
}

TEST_F(OrderBookTest, ModifyInPlaceResize) {
  // Add order 1: 10 @ 150
  OrderCommand cmd1{};
  cmd1.order_id = 1;
  cmd1.user_id = 100;
  cmd1.price_ticks = 150;
  cmd1.qty = 10;
  cmd1.side = Side::Bid;
  cmd1.order_type = OrderType::Limit;
  cmd1.tif = TimeInForce::GTC;
  book_->submit_limit(cmd1);

  // Add order 2: 10 @ 150 (same price, should be behind 1)
  OrderCommand cmd2{};
  cmd2.order_id = 2;
  cmd2.user_id = 101;
  cmd2.price_ticks = 150;
  cmd2.qty = 10;
  cmd2.side = Side::Bid;
  cmd2.order_type = OrderType::Limit;
  cmd2.tif = TimeInForce::GTC;
  book_->submit_limit(cmd2);

  // Modify order 1: Reduce qty 10 -> 5. Should keep priority.
  // modify returns ExecResult{filled=0, remaining=new_qty}
  auto res = book_->modify(1, 150, 5);
  EXPECT_EQ(res.filled, 0);
  EXPECT_EQ(res.remaining, 5);

  // Match against book. Should hit order 1 (5 qty) then order 2.
  OrderCommand sell{};
  sell.order_id = 1000;
  sell.user_id = 200;
  sell.qty = 6; // Should take all 5 of order 1, and 1 of order 2
  sell.price_ticks = 140;
  sell.side = Side::Ask;
  sell.order_type = OrderType::Limit;
  sell.tif = TimeInForce::GTC;

  // Track trades
  std::vector<TradeEvent> trades;
  book_->set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  book_->submit_limit(sell);

  ASSERT_EQ(trades.size(), 2);
  // Order 1 was 5, Order 2 was 10.
  // Matching 6.
  // Trade 1: sell(6) vs buy(1). Maker=1. Qty=5.
  // Trade 2: sell(1) vs buy(2). Maker=2. Qty=1.
  EXPECT_EQ(trades[0].maker_id, 1);
  EXPECT_EQ(trades[0].qty, 5);
  EXPECT_EQ(trades[1].maker_id, 2);
  EXPECT_EQ(trades[1].qty, 1);
}

TEST_F(OrderBookTest, ModifyCancelReplace) {
  // ... (keep context)
  // Add order 1: 10 @ 150
  OrderCommand cmd1{};
  cmd1.order_id = 1;
  cmd1.user_id = 100;
  cmd1.price_ticks = 150;
  cmd1.qty = 10;
  cmd1.side = Side::Bid;
  cmd1.order_type = OrderType::Limit;
  cmd1.tif = TimeInForce::GTC;
  book_->submit_limit(cmd1);

  // Add order 2: 10 @ 150
  OrderCommand cmd2 = cmd1;
  cmd2.order_id = 2;
  cmd2.user_id = 101;
  book_->submit_limit(cmd2);

  // Modify order 1: Increase qty 10 -> 15. Should lose priority
  // (cancel/replace).
  book_->modify(1, 150, 15);

  // Prioritization check. Order 2 should now match first.
  OrderCommand sell{};
  sell.order_id = 1000;
  sell.user_id = 200;
  sell.qty = 5; // Take 5
  sell.price_ticks = 140;
  sell.side = Side::Ask;
  sell.order_type = OrderType::Limit;

  std::vector<TradeEvent> trades;
  book_->set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  book_->submit_limit(sell);

  ASSERT_EQ(trades.size(), 1);
  EXPECT_EQ(trades[0].maker_id, 2); // Order 2 matches first
}

TEST_F(OrderBookTest, FOK_Fail) {
  // Order book has 10 @ 150
  OrderCommand cmd1{};
  cmd1.order_id = 1;
  cmd1.price_ticks = 150;
  cmd1.qty = 10;
  cmd1.side = Side::Ask;
  cmd1.order_type = OrderType::Limit;
  cmd1.tif = TimeInForce::GTC;
  book_->submit_limit(cmd1);

  // FOK Buy 15 @ 150. book only has 10. Should fail completely.
  OrderCommand fok{};
  fok.order_id = 2;
  fok.price_ticks = 150;
  fok.qty = 15;
  fok.side = Side::Bid;
  fok.order_type = OrderType::Limit;
  fok.tif = TimeInForce::FOK;

  std::vector<TradeEvent> trades;
  book_->set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  auto res = book_->submit_limit(fok);

  EXPECT_EQ(res.filled, 0);
  EXPECT_EQ(res.remaining, 0); // FOK kills matching
  EXPECT_TRUE(trades.empty());

  // Check book still has original 10
  EXPECT_EQ(book_->best_ask(), 150);
}

TEST_F(OrderBookTest, FOK_Success) {
  // Order book has 20 @ 150
  OrderCommand cmd1{};
  cmd1.order_id = 1;
  cmd1.price_ticks = 150;
  cmd1.qty = 20;
  cmd1.side = Side::Ask;
  cmd1.order_type = OrderType::Limit;
  cmd1.tif = TimeInForce::GTC;
  book_->submit_limit(cmd1);

  // FOK Buy 15 @ 150. book has 20. Should succeed.
  OrderCommand fok{};
  fok.order_id = 2;
  fok.price_ticks = 150;
  fok.qty = 15;
  fok.side = Side::Bid;
  fok.order_type = OrderType::Limit;
  fok.tif = TimeInForce::FOK;

  auto res = book_->submit_limit(fok);

  EXPECT_EQ(res.filled, 15);
  EXPECT_EQ(res.remaining, 0);

  // Remaining 5 in book
  EXPECT_EQ(book_->best_ask(), 150);
}
