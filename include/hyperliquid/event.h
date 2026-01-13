#pragma once

#include "command.h"
#include <variant>

namespace hyperliquid {

enum class EventType : uint8_t { Trade = 0, BookUpdate = 1 };

struct AnyEvent {
  EventType type;
  union {
    TradeEvent trade;
    BookUpdate book_update;
  };

  AnyEvent() {}
  AnyEvent(const TradeEvent &t) : type(EventType::Trade), trade(t) {}
  AnyEvent(const BookUpdate &b) : type(EventType::BookUpdate), book_update(b) {}
};

} // namespace hyperliquid
