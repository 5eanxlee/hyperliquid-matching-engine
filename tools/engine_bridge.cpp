// engine_bridge.cpp - JSON stdin/stdout bridge for the matching engine
// Reads order commands from stdin, processes through engine, outputs events to
// stdout
// Supports both JSON (default) and binary protocol modes

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

#include <hyperliquid/binary_protocol.h>
#include <hyperliquid/cpu_affinity.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>
#include <hyperliquid/timestamp.h>

using namespace hyperliquid;

// simple json parsing (no external deps)
std::string get_json_string(const std::string &json, const std::string &key) {
  std::string search = "\"" + key + "\":";
  size_t pos = json.find(search);
  if (pos == std::string::npos)
    return "";
  pos += search.length();
  while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"'))
    pos++;
  size_t end = pos;
  bool in_string = json[pos - 1] == '"';
  if (in_string) {
    while (end < json.length() && json[end] != '"')
      end++;
  } else {
    while (end < json.length() && json[end] != ',' && json[end] != '}')
      end++;
  }
  return json.substr(pos, end - pos);
}

double get_json_double(const std::string &json, const std::string &key) {
  std::string val = get_json_string(json, key);
  return val.empty() ? 0.0 : std::stod(val);
}

int64_t get_json_int(const std::string &json, const std::string &key) {
  std::string val = get_json_string(json, key);
  return val.empty() ? 0 : std::stoll(val);
}

// stats tracking (single-threaded, no atomics needed)
struct EngineStats {
  uint64_t orders_processed = 0;
  uint64_t trades_executed = 0;
  uint64_t total_latency_ns = 0;
  uint64_t min_latency_ns = UINT64_MAX;
  uint64_t max_latency_ns = 0;
  uint64_t resting_orders = 0;

  void record_latency(uint64_t ns) {
    total_latency_ns += ns;
    if (ns < min_latency_ns)
      min_latency_ns = ns;
    if (ns > max_latency_ns)
      max_latency_ns = ns;
  }

  double avg_latency_ns() const {
    return orders_processed > 0
               ? static_cast<double>(total_latency_ns) / orders_processed
               : 0;
  }

  void reset() {
    orders_processed = 0;
    trades_executed = 0;
    total_latency_ns = 0;
    min_latency_ns = UINT64_MAX;
    max_latency_ns = 0;
    resting_orders = 0;
  }
};

// output json event
void output_event(const std::string &type, const std::string &data) {
  std::cout << "{\"type\":\"" << type << "\"," << data << "}" << std::endl;
}

void output_stats(const EngineStats &stats, Tick best_bid, Tick best_ask,
                  Quantity bid_qty, Quantity ask_qty) {
  std::ostringstream ss;
  ss << "\"data\":{";
  ss << "\"orders_processed\":" << stats.orders_processed << ",";
  ss << "\"trades_executed\":" << stats.trades_executed << ",";
  ss << "\"resting_orders\":" << stats.resting_orders << ",";
  ss << "\"avg_latency_ns\":" << static_cast<uint64_t>(stats.avg_latency_ns())
     << ",";
  ss << "\"min_latency_ns\":"
     << (stats.min_latency_ns == UINT64_MAX ? 0 : stats.min_latency_ns) << ",";
  ss << "\"max_latency_ns\":" << stats.max_latency_ns << ",";
  ss << "\"best_bid\":" << (best_bid == Sentinel::EMPTY_BID ? 0 : best_bid)
     << ",";
  ss << "\"best_ask\":" << (best_ask == Sentinel::EMPTY_ASK ? 0 : best_ask)
     << ",";
  ss << "\"bid_qty\":" << bid_qty << ",";
  ss << "\"ask_qty\":" << ask_qty;
  ss << "}";
  output_event("stats", ss.str());
}

void output_trade(const TradeEvent &t) {
  std::ostringstream ss;
  ss << "\"data\":{";
  ss << "\"price\":" << t.price_ticks << ",";
  ss << "\"qty\":" << t.qty << ",";
  ss << "\"maker_id\":" << t.maker_id << ",";
  ss << "\"taker_id\":" << t.taker_id << ",";
  ss << "\"ts\":" << t.ts;
  ss << "}";
  output_event("trade", ss.str());
}

void output_book_update(Tick best_bid, Tick best_ask, Quantity bid_qty,
                        Quantity ask_qty) {
  std::ostringstream ss;
  ss << "\"data\":{";
  ss << "\"best_bid\":" << (best_bid == Sentinel::EMPTY_BID ? 0 : best_bid)
     << ",";
  ss << "\"best_ask\":" << (best_ask == Sentinel::EMPTY_ASK ? 0 : best_ask)
     << ",";
  ss << "\"bid_qty\":" << bid_qty << ",";
  ss << "\"ask_qty\":" << ask_qty;
  ss << "}";
  output_event("book", ss.str());
}

int main(int argc, char *argv[]) {
  // Parse command line args
  bool binary_mode = false;
  int pin_core = -1;
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--binary")
      binary_mode = true;
    if (std::string(argv[i]) == "--pin-core" && i + 1 < argc) {
      pin_core = std::stoi(argv[++i]);
    }
  }

  // disable buffering for real-time output
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(nullptr);
  std::cout.tie(nullptr);

  // Pin to CPU core for consistent low-latency
  if (pin_core >= 0) {
    if (pin_thread_to_core(static_cast<unsigned>(pin_core))) {
      std::cerr << "[Engine] Pinned to core " << pin_core << std::endl;
    } else {
      std::cerr << "[Engine] Warning: Failed to pin to core " << pin_core
                << std::endl;
    }
  }

  // calibrate timestamp
  TimestampUtil::calibrate();

  // create order book with wide price band
  // prices will be scaled: actual_price * 100 to handle decimals as ticks
  PriceBand band(1, 100000000, 1); // 0.01 to 1,000,000.00 scaled
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  EngineStats stats;
  uint64_t order_id = 1;
  uint64_t last_stats_output = 0;

  // order tracking for cancels
  std::unordered_map<uint64_t, OrderCommand> active_orders;

  // set up callbacks
  book.set_on_trade([&](const TradeEvent &t) {
    stats.trades_executed++;
    output_trade(t);
  });

  book.set_on_book_update([&](const BookUpdate &u) {
    output_book_update(u.best_bid, u.best_ask, u.bid_qty, u.ask_qty);
  });

  // signal ready
  output_event("ready", "\"data\":{\"version\":\"1.0\"}");

  // main loop - read commands from stdin
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty())
      continue;

    std::string cmd_type = get_json_string(line, "cmd");

    if (cmd_type == "order") {
      // parse order
      double price = get_json_double(line, "price");
      double size = get_json_double(line, "size");
      std::string side_str = get_json_string(line, "side");

      if (price <= 0 || size <= 0)
        continue;

      // scale price to ticks (cents)
      Tick price_ticks = static_cast<Tick>(price * 100);
      Quantity qty = static_cast<Quantity>(size * 1000); // scale size too

      OrderCommand cmd;
      cmd.type = CommandType::NewOrder;
      cmd.order_id = order_id++;
      cmd.symbol_id = 1;
      cmd.user_id = 1;
      cmd.price_ticks = price_ticks;
      cmd.qty = qty;
      cmd.side = (side_str == "B" || side_str == "buy") ? Side::Bid : Side::Ask;
      cmd.order_type = OrderType::Limit;
      cmd.tif = TimeInForce::GTC;
      cmd.flags = 0;

      // measure latency
      auto start = TimestampUtil::rdtsc();
      auto result = book.submit_limit(cmd);
      auto end = TimestampUtil::rdtsc();

      uint64_t latency_cycles = end - start;
      uint64_t latency_ns = TimestampUtil::cycles_to_ns(latency_cycles);

      stats.orders_processed++;
      stats.record_latency(latency_ns);

      if (result.remaining > 0) {
        stats.resting_orders++;
        active_orders[cmd.order_id] = cmd;
      }

    } else if (cmd_type == "cancel") {
      // cancel random resting order to keep book from growing too large
      if (!active_orders.empty()) {
        auto it = active_orders.begin();
        book.cancel(it->first);
        stats.resting_orders--;
        active_orders.erase(it);
      }

    } else if (cmd_type == "stats") {
      // output current stats
      Quantity bid_qty = 0, ask_qty = 0;
      Tick best_bid = book.best_bid();
      Tick best_ask = book.best_ask();
      output_stats(stats, best_bid, best_ask, bid_qty, ask_qty);

    } else if (cmd_type == "reset") {
      // reset stats only (book cannot be easily reset)
      stats.reset();
      active_orders.clear();
      output_event("reset", "\"data\":{\"success\":true}");
    }

    // periodic stats output (every 100 orders)
    if (stats.orders_processed - last_stats_output >= 100) {
      last_stats_output = stats.orders_processed;
      Quantity bid_qty = 0, ask_qty = 0;
      output_stats(stats, book.best_bid(), book.best_ask(), bid_qty, ask_qty);
    }
  }

  return 0;
}
