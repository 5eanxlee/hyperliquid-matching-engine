#pragma once

/// JSON serialization utilities for matching engine events
/// Provides lightweight JSON encoding without external dependencies

#include "command.h"
#include "types.h"
#include <sstream>
#include <string>

namespace hyperliquid {
namespace json {

/// Escape special characters in a string for JSON
inline std::string escape(const std::string &s) {
  std::string result;
  result.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\b':
      result += "\\b";
      break;
    case '\f':
      result += "\\f";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
    }
  }
  return result;
}

/// Serialize TradeEvent to JSON string
inline std::string to_json(const TradeEvent &trade) {
  std::ostringstream oss;
  oss << "{"
      << "\"type\":\"trade\","
      << "\"ts\":" << trade.ts << ","
      << "\"taker_id\":" << trade.taker_id << ","
      << "\"maker_id\":" << trade.maker_id << ","
      << "\"symbol_id\":" << trade.symbol_id << ","
      << "\"price\":" << trade.price_ticks << ","
      << "\"qty\":" << trade.qty << "}";
  return oss.str();
}

/// Serialize BookUpdate to JSON string
inline std::string to_json(const BookUpdate &update) {
  std::ostringstream oss;
  oss << "{"
      << "\"type\":\"book_update\","
      << "\"ts\":" << update.ts << ","
      << "\"symbol_id\":" << update.symbol_id << ","
      << "\"best_bid\":" << update.best_bid << ","
      << "\"best_ask\":" << update.best_ask << ","
      << "\"bid_qty\":" << update.bid_qty << ","
      << "\"ask_qty\":" << update.ask_qty << "}";
  return oss.str();
}

/// Serialize OrderCommand to JSON string
inline std::string to_json(const OrderCommand &cmd) {
  std::ostringstream oss;
  oss << "{"
      << "\"type\":\"order_command\","
      << "\"command_type\":" << static_cast<int>(cmd.type) << ","
      << "\"order_id\":" << cmd.order_id << ","
      << "\"symbol_id\":" << cmd.symbol_id << ","
      << "\"user_id\":" << cmd.user_id << ","
      << "\"price\":" << cmd.price_ticks << ","
      << "\"qty\":" << cmd.qty << ","
      << "\"side\":" << static_cast<int>(cmd.side) << ","
      << "\"order_type\":" << static_cast<int>(cmd.order_type) << ","
      << "\"tif\":" << static_cast<int>(cmd.tif) << ","
      << "\"flags\":" << cmd.flags;

  // Optional fields for advanced orders
  if (cmd.stop_price != 0) {
    oss << ",\"stop_price\":" << cmd.stop_price;
  }
  if (cmd.display_qty != 0) {
    oss << ",\"display_qty\":" << cmd.display_qty;
  }
  if (cmd.expiry_ts != 0) {
    oss << ",\"expiry_ts\":" << cmd.expiry_ts;
  }

  oss << "}";
  return oss.str();
}

/// Simple JSON parsing result
struct ParseResult {
  bool success{false};
  std::string error;
  OrderCommand command;
};

/// Parse JSON string to OrderCommand (basic implementation)
/// Note: This is a minimal parser for simple key-value JSON objects
inline ParseResult parse_order_command(const std::string &json) {
  ParseResult result;
  result.success = false;

  // Find required fields
  auto find_int = [&](const std::string &key) -> int64_t {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos)
      return 0;
    pos += search.size();

    // Skip whitespace
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
      pos++;

    // Parse number
    bool negative = false;
    if (pos < json.size() && json[pos] == '-') {
      negative = true;
      pos++;
    }

    int64_t value = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
      value = value * 10 + (json[pos] - '0');
      pos++;
    }

    return negative ? -value : value;
  };

  auto find_uint = [&](const std::string &key) -> uint64_t {
    return static_cast<uint64_t>(find_int(key));
  };

  // Parse command type
  int64_t cmd_type = find_int("command_type");
  if (cmd_type < 0 || cmd_type > 2) {
    result.error = "Invalid command_type";
    return result;
  }

  result.command.type = static_cast<CommandType>(cmd_type);
  result.command.order_id = find_uint("order_id");
  result.command.symbol_id = static_cast<SymbolId>(find_uint("symbol_id"));
  result.command.user_id = static_cast<UserId>(find_uint("user_id"));
  result.command.price_ticks = find_int("price");
  result.command.qty = find_int("qty");
  result.command.side = static_cast<Side>(find_int("side"));
  result.command.order_type = static_cast<OrderType>(find_int("order_type"));
  result.command.tif = static_cast<TimeInForce>(find_int("tif"));
  result.command.flags = static_cast<uint32_t>(find_uint("flags"));

  // Optional advanced order fields
  result.command.stop_price = find_int("stop_price");
  result.command.display_qty = find_int("display_qty");
  result.command.expiry_ts = find_uint("expiry_ts");

  result.success = true;
  return result;
}

} // namespace json
} // namespace hyperliquid
