/// Integration tests for matching engine components
/// Tests command routing, SPSC queue integration, and event propagation

#include <gtest/gtest.h>
#include <hyperliquid/event.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>
#include <hyperliquid/spsc_queue.h>

using namespace hyperliquid;

TEST(MatchingIntegrationTest, SPSCQueueIntegration) {
  auto input_queue = std::make_unique<SPSCQueue<OrderCommand, 65536>>();

  // Test that SPSC queues work correctly with command/event types
  OrderCommand cmd{};
  cmd.type = CommandType::NewOrder;
  cmd.order_id = 42;
  cmd.price_ticks = 155;
  cmd.qty = 100;

  EXPECT_TRUE(input_queue->push(cmd));
  EXPECT_FALSE(input_queue->empty());

  OrderCommand popped;
  EXPECT_TRUE(input_queue->pop(popped));
  EXPECT_TRUE(input_queue->empty());

  EXPECT_EQ(popped.order_id, 42);
  EXPECT_EQ(popped.price_ticks, 155);
  EXPECT_EQ(popped.qty, 100);
}

TEST(MatchingIntegrationTest, TradeEventPropagation) {
  // Create order book directly to test trade propagation
  PriceBand band(100, 200, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  std::vector<TradeEvent> trades;
  book.set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  // Add resting ask
  OrderCommand ask{};
  ask.order_id = 1;
  ask.user_id = 100;
  ask.price_ticks = 150;
  ask.qty = 10;
  ask.side = Side::Ask;
  ask.order_type = OrderType::Limit;
  ask.tif = TimeInForce::GTC;
  book.submit_limit(ask);

  // Crossing bid should generate trade
  OrderCommand bid{};
  bid.order_id = 2;
  bid.user_id = 101;
  bid.price_ticks = 155;
  bid.qty = 5;
  bid.side = Side::Bid;
  bid.order_type = OrderType::Limit;
  bid.tif = TimeInForce::GTC;
  book.submit_limit(bid);

  ASSERT_EQ(trades.size(), 1);
  EXPECT_EQ(trades[0].taker_id, 2);
  EXPECT_EQ(trades[0].maker_id, 1);
  EXPECT_EQ(trades[0].price_ticks, 150);
  EXPECT_EQ(trades[0].qty, 5);
}

TEST(MatchingIntegrationTest, BookUpdateEventPropagation) {
  PriceBand band(100, 200, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  std::vector<BookUpdate> updates;
  book.set_on_book_update([&](const BookUpdate &u) { updates.push_back(u); });

  // Add bid
  OrderCommand bid{};
  bid.order_id = 1;
  bid.user_id = 100;
  bid.price_ticks = 145;
  bid.qty = 10;
  bid.side = Side::Bid;
  bid.order_type = OrderType::Limit;
  bid.tif = TimeInForce::GTC;
  book.submit_limit(bid);

  ASSERT_GE(updates.size(), 1);
  auto &last = updates.back();
  EXPECT_EQ(last.best_bid, 145);
  EXPECT_EQ(last.bid_qty, 10);
}

TEST(MatchingIntegrationTest, CancelOrderRouting) {
  PriceBand band(100, 200, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  // Add order
  OrderCommand add{};
  add.order_id = 1;
  add.user_id = 100;
  add.price_ticks = 150;
  add.qty = 10;
  add.side = Side::Bid;
  add.order_type = OrderType::Limit;
  add.tif = TimeInForce::GTC;
  book.submit_limit(add);

  EXPECT_EQ(book.best_bid(), 150);

  // Cancel order
  EXPECT_TRUE(book.cancel(1));
  EXPECT_EQ(book.best_bid(), Sentinel::EMPTY_BID);

  // Cancel again should fail
  EXPECT_FALSE(book.cancel(1));
}

TEST(MatchingIntegrationTest, ModifyOrderRouting) {
  PriceBand band(100, 200, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  // Add order
  OrderCommand add{};
  add.order_id = 1;
  add.user_id = 100;
  add.price_ticks = 150;
  add.qty = 10;
  add.side = Side::Bid;
  add.order_type = OrderType::Limit;
  add.tif = TimeInForce::GTC;
  book.submit_limit(add);

  // Modify to reduce quantity
  auto result = book.modify(1, 150, 5);
  EXPECT_EQ(result.filled, 0);
  EXPECT_EQ(result.remaining, 5);

  // Modify to different price (cancel-replace)
  result = book.modify(1, 155, 8);
  EXPECT_EQ(book.best_bid(), 155);
}

TEST(MatchingIntegrationTest, MarketOrderRouting) {
  PriceBand band(100, 200, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  // Add resting limit ask
  OrderCommand ask{};
  ask.order_id = 1;
  ask.user_id = 100;
  ask.price_ticks = 150;
  ask.qty = 10;
  ask.side = Side::Ask;
  ask.order_type = OrderType::Limit;
  ask.tif = TimeInForce::GTC;
  book.submit_limit(ask);

  // Market buy
  OrderCommand market{};
  market.order_id = 2;
  market.user_id = 101;
  market.qty = 5;
  market.side = Side::Bid;
  market.order_type = OrderType::Market;
  auto result = book.submit_market(market);

  EXPECT_EQ(result.filled, 5);
  EXPECT_EQ(result.remaining, 0);
}

TEST(MatchingIntegrationTest, IOCOrderBehavior) {
  PriceBand band(100, 200, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  // Add resting limit
  OrderCommand ask{};
  ask.order_id = 1;
  ask.user_id = 100;
  ask.price_ticks = 150;
  ask.qty = 5;
  ask.side = Side::Ask;
  ask.order_type = OrderType::Limit;
  ask.tif = TimeInForce::GTC;
  book.submit_limit(ask);

  // IOC buy for more than available - partial fill, no rest
  OrderCommand ioc{};
  ioc.order_id = 2;
  ioc.user_id = 101;
  ioc.price_ticks = 155;
  ioc.qty = 10;
  ioc.side = Side::Bid;
  ioc.order_type = OrderType::Limit;
  ioc.tif = TimeInForce::IOC;
  auto result = book.submit_limit(ioc);

  EXPECT_EQ(result.filled, 5);
  EXPECT_EQ(result.remaining, 0); // IOC cancels unfilled portion
  EXPECT_EQ(book.best_bid(), Sentinel::EMPTY_BID); // No resting order
}

TEST(MatchingIntegrationTest, SelfTradePreventionFlag) {
  PriceBand band(100, 200, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  // Add resting limit from user 100
  OrderCommand ask{};
  ask.order_id = 1;
  ask.user_id = 100;
  ask.price_ticks = 150;
  ask.qty = 10;
  ask.side = Side::Ask;
  ask.order_type = OrderType::Limit;
  ask.tif = TimeInForce::GTC;
  book.submit_limit(ask);

  // Same user tries to buy with STP flag - should not match
  OrderCommand bid{};
  bid.order_id = 2;
  bid.user_id = 100; // Same user
  bid.price_ticks = 155;
  bid.qty = 5;
  bid.side = Side::Bid;
  bid.order_type = OrderType::Limit;
  bid.tif = TimeInForce::GTC;
  bid.flags = OrderFlags::STP;
  auto result = book.submit_limit(bid);

  EXPECT_EQ(result.filled, 0);    // No self-trade
  EXPECT_EQ(result.remaining, 5); // Order rests
}

TEST(MatchingIntegrationTest, AnyEventTypeHandling) {
  auto output_queue = std::make_unique<SPSCQueue<AnyEvent, 65536>>();

  // Test that AnyEvent can hold both TradeEvent and BookUpdate
  AnyEvent trade_evt(TradeEvent{1000, 1, 2, 1, 150, 10});
  AnyEvent book_evt(BookUpdate{});

  EXPECT_EQ(trade_evt.type, EventType::Trade);
  EXPECT_EQ(book_evt.type, EventType::BookUpdate);

  // Push both to queue
  EXPECT_TRUE(output_queue->push(trade_evt));
  EXPECT_TRUE(output_queue->push(book_evt));

  AnyEvent popped;
  EXPECT_TRUE(output_queue->pop(popped));
  EXPECT_EQ(popped.type, EventType::Trade);
  EXPECT_EQ(popped.trade.price_ticks, 150);
}
