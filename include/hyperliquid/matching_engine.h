#pragma once

#include "command.h"
#include "event.h"
#include "order_book.h"
#include "price_levels_array.h"
#include "spsc_queue.h"
#include <memory>

namespace hyperliquid {

class MatchingEngine {
public:
  struct Config {
    SymbolId symbol_id;
    PriceBand price_band;
    SPSCQueue<OrderCommand, 65536> *input_queue;
    SPSCQueue<AnyEvent, 65536> *output_queue;
  };

  explicit MatchingEngine(const Config &config);
  void run();

private:
  Config config_;
  std::unique_ptr<OrderBook<PriceLevelsArray>> order_book_;

  void process_trade(const TradeEvent &trade);
  void process_book_update(const BookUpdate &update);
};

} // namespace hyperliquid
