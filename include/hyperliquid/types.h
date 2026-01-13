#pragma once

#include <cstdint>
#include <limits>

namespace hyperliquid {

using OrderId = uint64_t;
using UserId = uint32_t;
using SymbolId = uint32_t;
using SeqNo = uint64_t;
using Tick = int64_t; // price in ticks
using Quantity = int64_t;
using Timestamp = uint64_t; // nanoseconds

enum class Side : uint8_t { Bid = 0, Ask = 1 };

enum class OrderType : uint8_t {
  Limit = 0,
  Market = 1,
  StopLimit = 2, // trigger at stop price
  StopMarket = 3
};

enum class TimeInForce : uint8_t {
  GTC = 0, // good till cancelled
  IOC = 1, // immediate or cancel
  FOK = 2, // fill or kill
  GTD = 3  // good till date
};

namespace OrderFlags {
constexpr uint32_t NONE = 0;
constexpr uint32_t POST_ONLY = 1 << 0; // maker only
constexpr uint32_t REDUCE_ONLY = 1 << 1;
constexpr uint32_t STP = 1 << 2;     // self-trade prevention
constexpr uint32_t ICEBERG = 1 << 3; // hidden qty
constexpr uint32_t STOP = 1 << 4;
} // namespace OrderFlags

struct PriceBand {
  Tick min_tick;
  Tick max_tick;
  Tick tick_size;

  PriceBand(Tick min_t, Tick max_t, Tick ts = 1)
      : min_tick(min_t), max_tick(max_t), tick_size(ts) {}
};

namespace Sentinel {
constexpr Tick EMPTY_BID = std::numeric_limits<Tick>::min();
constexpr Tick EMPTY_ASK = std::numeric_limits<Tick>::max();
constexpr OrderId INVALID_ORDER = 0;
} // namespace Sentinel

inline const char *to_string(Side s) {
  return (s == Side::Bid) ? "Bid" : "Ask";
}

inline const char *to_string(OrderType t) {
  switch (t) {
  case OrderType::Limit:
    return "Limit";
  case OrderType::Market:
    return "Market";
  case OrderType::StopLimit:
    return "StopLimit";
  case OrderType::StopMarket:
    return "StopMarket";
  default:
    return "Unknown";
  }
}

inline const char *to_string(TimeInForce tif) {
  switch (tif) {
  case TimeInForce::GTC:
    return "GTC";
  case TimeInForce::IOC:
    return "IOC";
  case TimeInForce::FOK:
    return "FOK";
  case TimeInForce::GTD:
    return "GTD";
  default:
    return "Unknown";
  }
}

} // namespace hyperliquid
