#pragma once

#include "types.h"

namespace hyperliquid {

enum class CommandType : uint8_t {
  NewOrder = 0,
  CancelOrder = 1,
  ModifyOrder = 2
};

struct OrderCommand {
  CommandType type;
  Timestamp recv_ts;
  OrderId order_id;
  SymbolId symbol_id;
  UserId user_id;
  Tick price_ticks;
  Quantity qty;
  Side side;
  OrderType order_type;
  TimeInForce tif;
  uint32_t flags;

  Tick stop_price{0};      // for stop orders
  Quantity display_qty{0}; // for iceberg
  Timestamp expiry_ts{0};  // for gtd

  OrderCommand() = default;
};

struct TradeEvent {
  Timestamp ts;
  OrderId taker_id;
  OrderId maker_id;
  SymbolId symbol_id;
  Tick price_ticks;
  Quantity qty;

  TradeEvent() = default;
  TradeEvent(Timestamp ts_, OrderId taker, OrderId maker, SymbolId sym, Tick px,
             Quantity q)
      : ts(ts_), taker_id(taker), maker_id(maker), symbol_id(sym),
        price_ticks(px), qty(q) {}
};

struct BookUpdate {
  Timestamp ts;
  SymbolId symbol_id;
  Tick best_bid;
  Tick best_ask;
  Quantity bid_qty;
  Quantity ask_qty;

  BookUpdate() = default;
};

struct ExecResult {
  Quantity filled{0};
  Quantity remaining{0};
  bool accepted{true};

  ExecResult() = default;
  ExecResult(Quantity f, Quantity r) : filled(f), remaining(r) {}
};

} // namespace hyperliquid
