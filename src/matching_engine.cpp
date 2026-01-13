#include "hyperliquid/matching_engine.h"
#include "hyperliquid/timestamp.h"
#include <iostream>
#include <thread>

namespace hyperliquid {

MatchingEngine::MatchingEngine(const Config &config) : config_(config) {
  // Initialize price levels with the provided band
  PriceLevelsArray bids(config.price_band);
  PriceLevelsArray asks(config.price_band);

  // Create order book
  order_book_ = std::make_unique<OrderBook<PriceLevelsArray>>(
      config.symbol_id, std::move(bids), std::move(asks));

  // Bind callbacks
  // We use raw 'this' because MatchingEngine strictly outlives OrderBook
  order_book_->set_on_trade(
      [this](const TradeEvent &trade) { this->process_trade(trade); });

  order_book_->set_on_book_update(
      [this](const BookUpdate &update) { this->process_book_update(update); });
}

void MatchingEngine::run() {
  // Warmup or calibration?

  OrderCommand cmd;
  while (true) {
    // Spin wait for command
    while (!config_.input_queue->pop(cmd)) {
      std::this_thread::yield();
      // TODO: Check for shutdown signal?
      // For now, infinite loop until process death.
    }

    // Process command
    switch (cmd.type) {
    case CommandType::NewOrder:
      if (cmd.order_type == OrderType::Limit) {
        order_book_->submit_limit(cmd);
      } else {
        order_book_->submit_market(cmd);
      }
      break;
    case CommandType::CancelOrder:
      order_book_->cancel(cmd.order_id);
      break;
    case CommandType::ModifyOrder:
      // TODO: Implement modify
      order_book_->modify(cmd.order_id, cmd.price_ticks, cmd.qty);
      break;
    }
  }
}

void MatchingEngine::process_trade(const TradeEvent &trade) {
  // Enqueue trade event
  AnyEvent evt(trade);
  while (!config_.output_queue->push(evt)) {
    // Spin if output full
    std::this_thread::yield();
  }
}

void MatchingEngine::process_book_update(const BookUpdate &update) {
  // Enqueue book update
  AnyEvent evt(update);
  while (!config_.output_queue->push(evt)) {
    std::this_thread::yield();
  }
}

} // namespace hyperliquid
