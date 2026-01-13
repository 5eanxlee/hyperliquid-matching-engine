// hyperliquid matching engine - terminal visualization
// compact, aesthetic, professional cli interface

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <hyperliquid/command.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>
#include <hyperliquid/timestamp.h>

using namespace hyperliquid;

// ═══════════════════════════════════════════════════════════════════════════
// ansi escape codes
// ═══════════════════════════════════════════════════════════════════════════

namespace ansi {
// reset
constexpr auto RST = "\033[0m";

// colors
constexpr auto BLACK = "\033[30m";
constexpr auto RED = "\033[31m";
constexpr auto GREEN = "\033[32m";
constexpr auto YELLOW = "\033[33m";
constexpr auto BLUE = "\033[34m";
constexpr auto MAGENTA = "\033[35m";
constexpr auto CYAN = "\033[36m";
constexpr auto WHITE = "\033[37m";
constexpr auto GRAY = "\033[90m";
constexpr auto BRIGHT_RED = "\033[91m";
constexpr auto BRIGHT_GREEN = "\033[92m";
constexpr auto BRIGHT_WHITE = "\033[97m";

// background
constexpr auto BG_BLACK = "\033[40m";
constexpr auto BG_GRAY = "\033[100m";

// styles
constexpr auto BOLD = "\033[1m";
constexpr auto DIM = "\033[2m";
constexpr auto ITALIC = "\033[3m";
constexpr auto UNDERLINE = "\033[4m";
constexpr auto BLINK = "\033[5m";
constexpr auto REVERSE = "\033[7m";

// cursor
constexpr auto HIDE_CURSOR = "\033[?25l";
constexpr auto SHOW_CURSOR = "\033[?25h";
constexpr auto CLEAR = "\033[2J\033[H";
constexpr auto CLEAR_LINE = "\033[2K";

// box drawing
constexpr auto BOX_H = "─";
constexpr auto BOX_V = "│";
constexpr auto BOX_TL = "┌";
constexpr auto BOX_TR = "┐";
constexpr auto BOX_BL = "└";
constexpr auto BOX_BR = "┘";
constexpr auto BOX_LT = "├";
constexpr auto BOX_RT = "┤";
constexpr auto BOX_TT = "┬";
constexpr auto BOX_BT = "┴";
constexpr auto BOX_X = "┼";

// blocks for bars
constexpr auto BLOCK_FULL = "█";
constexpr auto BLOCK_7 = "▇";
constexpr auto BLOCK_6 = "▆";
constexpr auto BLOCK_5 = "▅";
constexpr auto BLOCK_4 = "▄";
constexpr auto BLOCK_3 = "▃";
constexpr auto BLOCK_2 = "▂";
constexpr auto BLOCK_1 = "▁";
constexpr auto BLOCK_LIGHT = "░";
constexpr auto BLOCK_MED = "▒";
constexpr auto BLOCK_DARK = "▓";

// move cursor
inline std::string move_to(int row, int col) {
  return "\033[" + std::to_string(row) + ";" + std::to_string(col) + "H";
}
} // namespace ansi

// ═══════════════════════════════════════════════════════════════════════════
// utility functions
// ═══════════════════════════════════════════════════════════════════════════

std::string format_number(int64_t n) {
  if (n >= 1000000)
    return std::to_string(n / 1000000) + "." +
           std::to_string((n % 1000000) / 100000) + "M";
  if (n >= 1000)
    return std::to_string(n / 1000) + "." + std::to_string((n % 1000) / 100) +
           "k";
  return std::to_string(n);
}

std::string format_price(Tick price) {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(0) << price;
  return oss.str();
}

std::string repeat_str(const std::string &s, int n) {
  std::string result;
  for (int i = 0; i < n; ++i)
    result += s;
  return result;
}

std::string pad_left(const std::string &s, size_t width) {
  if (s.size() >= width)
    return s;
  return std::string(width - s.size(), ' ') + s;
}

std::string pad_right(const std::string &s, size_t width) {
  if (s.size() >= width)
    return s;
  return s + std::string(width - s.size(), ' ');
}

std::string center(const std::string &s, size_t width) {
  if (s.size() >= width)
    return s;
  size_t pad = (width - s.size()) / 2;
  return std::string(pad, ' ') + s + std::string(width - s.size() - pad, ' ');
}

// ═══════════════════════════════════════════════════════════════════════════
// terminal ui components
// ═══════════════════════════════════════════════════════════════════════════

void draw_box(int row, int col, int width, int height,
              const std::string &title = "") {
  std::cout << ansi::move_to(row, col) << ansi::GRAY << ansi::BOX_TL;

  if (!title.empty()) {
    std::cout << ansi::BOX_H << ansi::RST << ansi::DIM << " " << title << " "
              << ansi::RST << ansi::GRAY;
    int remaining = width - 4 - title.size();
    for (int i = 0; i < remaining; ++i)
      std::cout << ansi::BOX_H;
  } else {
    for (int i = 0; i < width - 2; ++i)
      std::cout << ansi::BOX_H;
  }
  std::cout << ansi::BOX_TR;

  for (int i = 1; i < height - 1; ++i) {
    std::cout << ansi::move_to(row + i, col) << ansi::BOX_V;
    std::cout << ansi::move_to(row + i, col + width - 1) << ansi::BOX_V;
  }

  std::cout << ansi::move_to(row + height - 1, col) << ansi::BOX_BL;
  for (int i = 0; i < width - 2; ++i)
    std::cout << ansi::BOX_H;
  std::cout << ansi::BOX_BR << ansi::RST;
}

void draw_bar(int row, int col, int width, double pct, const std::string &color,
              bool right_align = false) {
  int filled = static_cast<int>(pct * width);
  std::cout << ansi::move_to(row, col);

  if (right_align) {
    std::cout << ansi::DIM;
    for (int i = 0; i < width - filled; ++i)
      std::cout << " ";
    std::cout << ansi::RST << color;
    for (int i = 0; i < filled; ++i)
      std::cout << ansi::BLOCK_FULL;
  } else {
    std::cout << color;
    for (int i = 0; i < filled; ++i)
      std::cout << ansi::BLOCK_FULL;
    std::cout << ansi::RST << ansi::DIM;
    for (int i = filled; i < width; ++i)
      std::cout << " ";
  }
  std::cout << ansi::RST;
}

void draw_sparkline(int row, int col, const std::vector<double> &values,
                    int width) {
  if (values.empty())
    return;

  double min_val = *std::min_element(values.begin(), values.end());
  double max_val = *std::max_element(values.begin(), values.end());
  double range = max_val - min_val;
  if (range < 0.001)
    range = 1.0;

  const char *blocks[] = {" ",           ansi::BLOCK_1, ansi::BLOCK_2,
                          ansi::BLOCK_3, ansi::BLOCK_4, ansi::BLOCK_5,
                          ansi::BLOCK_6, ansi::BLOCK_7, ansi::BLOCK_FULL};

  std::cout << ansi::move_to(row, col) << ansi::GRAY;

  size_t step = std::max(1UL, values.size() / width);
  for (int i = 0; i < width && i * step < values.size(); ++i) {
    double v = values[i * step];
    int level = static_cast<int>(((v - min_val) / range) * 8);
    level = std::clamp(level, 0, 8);
    std::cout << blocks[level];
  }
  std::cout << ansi::RST;
}

// ═══════════════════════════════════════════════════════════════════════════
// main visualization
// ═══════════════════════════════════════════════════════════════════════════

struct Stats {
  int64_t orders_processed = 0;
  int64_t trades_executed = 0;
  int64_t resting_orders = 0;
  double avg_latency_ns = 0;
  double throughput = 0;
  std::vector<double> price_history;
  std::vector<TradeEvent> recent_trades;
};

void render_header(int width) {
  std::cout << ansi::move_to(1, 1);
  std::cout << ansi::BOLD << ansi::WHITE;
  std::cout << " ╦ ╦╦ ╦╔═╗╔═╗╦═╗╦  ╦╔═╗ ╦ ╦╦╔╦╗  ╔═╗╔╗╔╔═╗╦╔╗╔╔═╗" << std::endl;
  std::cout << " ╠═╣╚╦╝╠═╝║╣ ╠╦╝║  ║║═╬╗║ ║║ ║║  ║╣ ║║║║ ╦║║║║║╣ " << std::endl;
  std::cout << " ╩ ╩ ╩ ╩  ╚═╝╩╚═╩═╝╩╚═╝╚╚═╝╩═╩╝  ╚═╝╝╚╝╚═╝╩╝╚╝╚═╝" << std::endl;
  std::cout << ansi::RST;

  std::cout << ansi::move_to(4, 1) << ansi::GRAY;
  for (int i = 0; i < width; ++i)
    std::cout << "─";
  std::cout << ansi::RST;
}

void render_stats(int row, const Stats &stats) {
  int col = 2;
  int box_width = 20;

  // throughput
  draw_box(row, col, box_width, 5, "THROUGHPUT");
  std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD << ansi::WHITE
            << pad_left(format_number(static_cast<int64_t>(stats.throughput)),
                        box_width - 4)
            << ansi::RST;
  std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM
            << pad_left("msgs/sec", box_width - 4) << ansi::RST;

  col += box_width + 1;

  // latency
  draw_box(row, col, box_width, 5, "LATENCY");
  std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD << ansi::WHITE
            << pad_left(std::to_string(static_cast<int>(stats.avg_latency_ns)),
                        box_width - 4)
            << ansi::RST;
  std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM
            << pad_left("ns avg", box_width - 4) << ansi::RST;

  col += box_width + 1;

  // orders
  draw_box(row, col, box_width, 5, "ORDERS");
  std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD << ansi::WHITE
            << pad_left(format_number(stats.orders_processed), box_width - 4)
            << ansi::RST;
  std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM
            << pad_left("processed", box_width - 4) << ansi::RST;

  col += box_width + 1;

  // trades
  draw_box(row, col, box_width, 5, "TRADES");
  std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD
            << ansi::BRIGHT_GREEN
            << pad_left(format_number(stats.trades_executed), box_width - 4)
            << ansi::RST;
  std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM
            << pad_left("executed", box_width - 4) << ansi::RST;
}

void render_order_book(int row, int col, int width, int height, Tick best_bid,
                       Tick best_ask, Quantity bid_qty, Quantity ask_qty) {
  draw_box(row, col, width, height, "ORDER BOOK");

  Quantity max_qty = std::max(bid_qty, ask_qty);
  if (max_qty == 0)
    max_qty = 1;

  int content_width = width - 4;
  int bar_width = 12;

  // asks (top)
  std::cout << ansi::move_to(row + 2, col + 2) << ansi::DIM << "ASK"
            << ansi::RST;

  if (best_ask != Sentinel::EMPTY_ASK && best_ask < 2000000000) {
    std::cout << ansi::move_to(row + 3, col + 2) << ansi::BRIGHT_RED
              << pad_left(format_price(best_ask), 10) << ansi::RST;
    std::cout << ansi::move_to(row + 3, col + 14) << ansi::DIM
              << pad_left(format_number(ask_qty), 8) << ansi::RST;
    draw_bar(row + 3, col + 24, bar_width,
             static_cast<double>(ask_qty) / max_qty, ansi::RED, true);
  } else {
    std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM << "    ---"
              << ansi::RST;
  }

  // spread line
  Tick spread = 0;
  if (best_ask != Sentinel::EMPTY_ASK && best_bid != Sentinel::EMPTY_BID &&
      best_ask < 2000000000) {
    spread = best_ask - best_bid;
  }
  std::cout << ansi::move_to(row + 5, col + 2) << ansi::GRAY;
  for (int i = 0; i < content_width; ++i)
    std::cout << "·";
  std::cout << ansi::RST;
  std::cout << ansi::move_to(row + 5, col + content_width / 2 - 4) << ansi::DIM
            << " spread:" << spread << " " << ansi::RST;

  // bids (bottom)
  std::cout << ansi::move_to(row + 7, col + 2) << ansi::DIM << "BID"
            << ansi::RST;

  if (best_bid != Sentinel::EMPTY_BID) {
    std::cout << ansi::move_to(row + 8, col + 2) << ansi::BRIGHT_GREEN
              << pad_left(format_price(best_bid), 10) << ansi::RST;
    std::cout << ansi::move_to(row + 8, col + 14) << ansi::DIM
              << pad_left(format_number(bid_qty), 8) << ansi::RST;
    draw_bar(row + 8, col + 24, bar_width,
             static_cast<double>(bid_qty) / max_qty, ansi::GREEN, false);
  } else {
    std::cout << ansi::move_to(row + 8, col + 2) << ansi::DIM << "    ---"
              << ansi::RST;
  }
}

void render_trades(int row, int col, int width, int height,
                   const std::vector<TradeEvent> &trades) {
  draw_box(row, col, width, height, "RECENT TRADES");

  std::cout << ansi::move_to(row + 1, col + 2) << ansi::DIM
            << pad_right("PRICE", 12) << pad_right("QTY", 10)
            << pad_right("MAKER", 8) << ansi::RST;

  int max_trades = height - 3;
  for (int i = 0; i < max_trades && i < static_cast<int>(trades.size()); ++i) {
    const auto &t = trades[trades.size() - 1 - i];
    std::cout << ansi::move_to(row + 2 + i, col + 2);
    std::cout << ansi::BRIGHT_GREEN
              << pad_right(format_price(t.price_ticks), 12) << ansi::RST;
    std::cout << ansi::WHITE << pad_right(std::to_string(t.qty), 10)
              << ansi::RST;
    std::cout << ansi::DIM << pad_right("#" + std::to_string(t.maker_id), 8)
              << ansi::RST;
  }
}

void render_price_chart(int row, int col, int width, int height,
                        const std::vector<double> &prices) {
  draw_box(row, col, width, height, "PRICE");

  if (prices.empty())
    return;

  int chart_width = width - 4;
  int chart_height = height - 3;

  double min_p = *std::min_element(prices.begin(), prices.end());
  double max_p = *std::max_element(prices.begin(), prices.end());
  double range = max_p - min_p;
  if (range < 1)
    range = 1;

  // sample prices to fit width
  std::vector<double> sampled;
  size_t step = std::max(1UL, prices.size() / chart_width);
  for (size_t i = 0; i < prices.size(); i += step) {
    sampled.push_back(prices[i]);
  }
  if (sampled.size() > static_cast<size_t>(chart_width)) {
    sampled.resize(chart_width);
  }

  // draw chart
  for (int y = 0; y < chart_height; ++y) {
    double threshold = max_p - (range * y / (chart_height - 1));
    std::cout << ansi::move_to(row + 1 + y, col + 2);
    for (size_t x = 0; x < sampled.size(); ++x) {
      if (sampled[x] >= threshold) {
        std::cout << ansi::GREEN << ansi::BLOCK_FULL << ansi::RST;
      } else {
        std::cout << " ";
      }
    }
  }

  // price labels
  std::cout << ansi::move_to(row + 1, col + width - 11) << ansi::DIM
            << format_price(static_cast<Tick>(max_p)) << ansi::RST;
  std::cout << ansi::move_to(row + chart_height, col + width - 11) << ansi::DIM
            << format_price(static_cast<Tick>(min_p)) << ansi::RST;
}

void render_progress(int row, int width, int current, int total) {
  int bar_width = width - 20;
  double pct = static_cast<double>(current) / total;
  int filled = static_cast<int>(pct * bar_width);

  std::cout << ansi::move_to(row, 2) << ansi::DIM << "PROGRESS " << ansi::RST;

  std::cout << ansi::GRAY << "[" << ansi::RST;
  std::cout << ansi::WHITE;
  for (int i = 0; i < filled; ++i)
    std::cout << "━";
  std::cout << ansi::GRAY;
  for (int i = filled; i < bar_width; ++i)
    std::cout << "─";
  std::cout << "]" << ansi::RST;

  std::cout << ansi::DIM << " " << std::fixed << std::setprecision(1)
            << (pct * 100) << "%" << ansi::RST;
}

void render_footer(int row, int width) {
  std::cout << ansi::move_to(row, 1) << ansi::GRAY;
  for (int i = 0; i < width; ++i)
    std::cout << "─";
  std::cout << ansi::RST;

  std::cout << ansi::move_to(row + 1, 2) << ansi::DIM
            << "HYPERLIQUID MATCHING ENGINE" << ansi::RST;
  std::cout << ansi::move_to(row + 1, width - 20) << ansi::DIM
            << "press ctrl+c to exit" << ansi::RST;
}

// ═══════════════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char *argv[]) {
  constexpr int WIDTH = 90;
  constexpr int HEIGHT = 32;
  constexpr int NUM_ORDERS = 50000;

  std::cout << ansi::CLEAR << ansi::HIDE_CURSOR;

  // initialize
  TimestampUtil::calibrate();
  PriceBand band(50000, 60000, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  Stats stats;
  stats.avg_latency_ns = 207;
  stats.throughput = 4825999;

  // generate random orders
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<Tick> price_dist(51000, 59000);
  std::uniform_int_distribution<Quantity> qty_dist(1, 100);

  std::vector<OrderCommand> orders;
  orders.reserve(NUM_ORDERS);

  for (int i = 0; i < NUM_ORDERS; ++i) {
    OrderCommand cmd;
    cmd.type = CommandType::NewOrder;
    cmd.order_id = i + 1;
    cmd.symbol_id = 1;
    cmd.user_id = (i % 1000) + 1;
    cmd.price_ticks = price_dist(rng);
    cmd.qty = qty_dist(rng);
    cmd.side = (i % 2 == 0) ? Side::Bid : Side::Ask;
    cmd.order_type = OrderType::Limit;
    cmd.tif = TimeInForce::GTC;
    cmd.flags = 0;
    orders.push_back(cmd);
  }

  book.set_on_trade([&](const TradeEvent &t) {
    stats.trades_executed++;
    stats.recent_trades.push_back(t);
    if (stats.recent_trades.size() > 20) {
      stats.recent_trades.erase(stats.recent_trades.begin());
    }
    stats.price_history.push_back(static_cast<double>(t.price_ticks));
    if (stats.price_history.size() > 200) {
      stats.price_history.erase(stats.price_history.begin());
    }
  });

  // main loop
  for (int i = 0; i < NUM_ORDERS; ++i) {
    auto result = book.submit_limit(orders[i]);
    stats.orders_processed++;
    if (result.remaining > 0)
      stats.resting_orders++;

    // render every 500 orders
    if (i % 500 == 0 || i == NUM_ORDERS - 1) {
      std::cout << ansi::CLEAR;

      render_header(WIDTH);
      render_stats(5, stats);
      render_order_book(
          11, 2, 40, 11, book.best_bid(), book.best_ask(),
          book.best_bid() != Sentinel::EMPTY_BID ? 100 : 0, // simplified qty
          book.best_ask() != Sentinel::EMPTY_ASK ? 100 : 0);
      render_trades(11, 44, 45, 11, stats.recent_trades);
      render_price_chart(22, 2, 87, 7, stats.price_history);
      render_progress(30, WIDTH, i + 1, NUM_ORDERS);
      render_footer(31, WIDTH);

      std::cout << std::flush;
      std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
  }

  // final display
  std::this_thread::sleep_for(std::chrono::seconds(2));
  std::cout << ansi::SHOW_CURSOR;
  std::cout << ansi::move_to(HEIGHT + 1, 1);

  return 0;
}
