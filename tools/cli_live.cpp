// hyperliquid matching engine - live terminal visualization
// connects to hyperliquid api for real market data

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// simple http client using curl (available on most systems)
#include <cstdio>

// ═══════════════════════════════════════════════════════════════════════════
// ansi escape codes
// ═══════════════════════════════════════════════════════════════════════════

namespace ansi {
constexpr auto RST = "\033[0m";
constexpr auto BLACK = "\033[30m";
constexpr auto RED = "\033[31m";
constexpr auto GREEN = "\033[32m";
constexpr auto YELLOW = "\033[33m";
constexpr auto WHITE = "\033[37m";
constexpr auto GRAY = "\033[90m";
constexpr auto BRIGHT_RED = "\033[91m";
constexpr auto BRIGHT_GREEN = "\033[92m";
constexpr auto BRIGHT_WHITE = "\033[97m";
constexpr auto BOLD = "\033[1m";
constexpr auto DIM = "\033[2m";
constexpr auto HIDE_CURSOR = "\033[?25l";
constexpr auto SHOW_CURSOR = "\033[?25h";
constexpr auto CLEAR = "\033[2J\033[H";
constexpr auto BOX_H = "─";
constexpr auto BOX_V = "│";
constexpr auto BOX_TL = "┌";
constexpr auto BOX_TR = "┐";
constexpr auto BOX_BL = "└";
constexpr auto BOX_BR = "┘";
constexpr auto BLOCK_FULL = "█";

inline std::string move_to(int row, int col) {
  return "\033[" + std::to_string(row) + ";" + std::to_string(col) + "H";
}
} // namespace ansi

// ═══════════════════════════════════════════════════════════════════════════
// utilities
// ═══════════════════════════════════════════════════════════════════════════

std::string exec_cmd(const char *cmd) {
  std::string result;
  FILE *pipe = popen(cmd, "r");
  if (!pipe)
    return result;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result += buffer;
  }
  pclose(pipe);
  return result;
}

std::string format_price(double price) {
  std::ostringstream oss;
  if (price >= 1000) {
    oss << std::fixed << std::setprecision(2) << price;
  } else if (price >= 1) {
    oss << std::fixed << std::setprecision(4) << price;
  } else {
    oss << std::fixed << std::setprecision(6) << price;
  }
  return oss.str();
}

std::string format_size(double size) {
  std::ostringstream oss;
  if (size >= 1000) {
    oss << std::fixed << std::setprecision(1) << (size / 1000) << "k";
  } else {
    oss << std::fixed << std::setprecision(2) << size;
  }
  return oss.str();
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

// simple json value extractor (no external deps)
std::string extract_json_string(const std::string &json,
                                const std::string &key) {
  std::string search = "\"" + key + "\":";
  size_t pos = json.find(search);
  if (pos == std::string::npos)
    return "";
  pos += search.length();
  while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"'))
    pos++;
  size_t end = pos;
  while (end < json.length() && json[end] != '"' && json[end] != ',' &&
         json[end] != '}')
    end++;
  return json.substr(pos, end - pos);
}

// ═══════════════════════════════════════════════════════════════════════════
// data structures
// ═══════════════════════════════════════════════════════════════════════════

struct OrderLevel {
  double price;
  double size;
  int count;
};

struct Trade {
  double price;
  double size;
  std::string side;
  int64_t time;
};

struct MarketData {
  std::string coin;
  std::vector<OrderLevel> bids;
  std::vector<OrderLevel> asks;
  std::vector<Trade> trades;
  double last_price = 0;
  double mark_price = 0;
  double funding_rate = 0;
  double volume_24h = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
// api fetching
// ═══════════════════════════════════════════════════════════════════════════

MarketData fetch_market_data(const std::string &coin) {
  MarketData data;
  data.coin = coin;

  // fetch l2 order book
  std::string cmd = "curl -s -X POST https://api.hyperliquid.xyz/info "
                    "-H 'Content-Type: application/json' "
                    "-d '{\"type\": \"l2Book\", \"coin\": \"" +
                    coin + "\"}'";

  std::string response = exec_cmd(cmd.c_str());

  // parse levels (simplified parsing)
  // response format: {"levels":[[bids],[asks]]}
  // each level: {"px":"price","sz":"size","n":count}

  size_t levels_pos = response.find("\"levels\"");
  if (levels_pos != std::string::npos) {
    // find bids array
    size_t bids_start = response.find("[[", levels_pos);
    size_t bids_end = response.find("],[", bids_start);
    if (bids_start != std::string::npos && bids_end != std::string::npos) {
      std::string bids_str =
          response.substr(bids_start + 2, bids_end - bids_start - 2);
      // parse each bid
      size_t pos = 0;
      while ((pos = bids_str.find("{\"px\":", pos)) != std::string::npos) {
        OrderLevel level;
        level.price =
            std::stod(extract_json_string(bids_str.substr(pos), "px"));
        level.size = std::stod(extract_json_string(bids_str.substr(pos), "sz"));
        level.count = std::stoi(extract_json_string(bids_str.substr(pos), "n"));
        data.bids.push_back(level);
        pos++;
      }
    }

    // find asks array
    size_t asks_start = response.find("],[", bids_end);
    size_t asks_end = response.find("]]", asks_start);
    if (asks_start != std::string::npos && asks_end != std::string::npos) {
      std::string asks_str =
          response.substr(asks_start + 3, asks_end - asks_start - 3);
      size_t pos = 0;
      while ((pos = asks_str.find("{\"px\":", pos)) != std::string::npos) {
        OrderLevel level;
        level.price =
            std::stod(extract_json_string(asks_str.substr(pos), "px"));
        level.size = std::stod(extract_json_string(asks_str.substr(pos), "sz"));
        level.count = std::stoi(extract_json_string(asks_str.substr(pos), "n"));
        data.asks.push_back(level);
        pos++;
      }
    }
  }

  // fetch meta info for mark price
  cmd = "curl -s -X POST https://api.hyperliquid.xyz/info "
        "-H 'Content-Type: application/json' "
        "-d '{\"type\": \"meta\"}'";
  response = exec_cmd(cmd.c_str());

  // simple extraction of mark price from universe array
  size_t coin_pos = response.find("\"name\":\"" + coin + "\"");
  if (coin_pos != std::string::npos) {
    // look backwards for markPx
    size_t mark_pos = response.rfind("markPx", coin_pos);
    if (mark_pos != std::string::npos && mark_pos > coin_pos - 200) {
      // actually search forward from a context block
    }
  }

  // set last price from best bid/ask
  if (!data.bids.empty() && !data.asks.empty()) {
    data.last_price = (data.bids[0].price + data.asks[0].price) / 2;
  }

  return data;
}

// ═══════════════════════════════════════════════════════════════════════════
// rendering
// ═══════════════════════════════════════════════════════════════════════════

void draw_box(int row, int col, int width, int height,
              const std::string &title = "") {
  std::cout << ansi::move_to(row, col) << ansi::GRAY << ansi::BOX_TL;
  if (!title.empty()) {
    std::cout << ansi::BOX_H << ansi::RST << ansi::DIM << " " << title << " "
              << ansi::RST << ansi::GRAY;
    int remaining = width - 4 - static_cast<int>(title.size());
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
    for (int i = 0; i < width - filled; ++i)
      std::cout << " ";
    std::cout << color;
    for (int i = 0; i < filled; ++i)
      std::cout << ansi::BLOCK_FULL;
  } else {
    std::cout << color;
    for (int i = 0; i < filled; ++i)
      std::cout << ansi::BLOCK_FULL;
    std::cout << ansi::RST;
    for (int i = filled; i < width; ++i)
      std::cout << " ";
  }
  std::cout << ansi::RST;
}

void render_header(const std::string &coin) {
  std::cout << ansi::move_to(1, 1) << ansi::BOLD << ansi::WHITE;
  std::cout << " ╦ ╦╦ ╦╔═╗╔═╗╦═╗╦  ╦╔═╗ ╦ ╦╦╔╦╗" << std::endl;
  std::cout << " ╠═╣╚╦╝╠═╝║╣ ╠╦╝║  ║║═╬╗║ ║║ ║║ " << std::endl;
  std::cout << " ╩ ╩ ╩ ╩  ╚═╝╩╚═╩═╝╩╚═╝╚╚═╝╩═╩╝ " << std::endl;
  std::cout << ansi::RST;

  std::cout << ansi::move_to(2, 36) << ansi::DIM << "LIVE MARKET DATA"
            << ansi::RST;
  std::cout << ansi::move_to(3, 36) << ansi::BRIGHT_GREEN << coin << "-USD"
            << ansi::RST;

  std::cout << ansi::move_to(4, 1) << ansi::GRAY;
  for (int i = 0; i < 80; ++i)
    std::cout << "─";
  std::cout << ansi::RST;
}

void render_order_book(int row, int col, int width, int height,
                       const MarketData &data) {
  draw_box(row, col, width, height, "ORDER BOOK");

  if (data.bids.empty() || data.asks.empty()) {
    std::cout << ansi::move_to(row + height / 2, col + 2) << ansi::DIM
              << "Loading..." << ansi::RST;
    return;
  }

  double max_size = 0;
  for (const auto &b : data.bids)
    max_size = std::max(max_size, b.size);
  for (const auto &a : data.asks)
    max_size = std::max(max_size, a.size);
  if (max_size == 0)
    max_size = 1;

  int levels_to_show = (height - 5) / 2;
  int bar_width = 10;

  // header
  std::cout << ansi::move_to(row + 1, col + 2) << ansi::DIM
            << pad_right("SIZE", 10) << pad_right("PRICE", 14) << "DEPTH"
            << ansi::RST;

  // asks (reversed, best ask at bottom)
  int ask_row = row + 2;
  for (int i = std::min(levels_to_show, (int)data.asks.size()) - 1; i >= 0;
       --i) {
    const auto &ask = data.asks[i];
    std::cout << ansi::move_to(ask_row, col + 2);
    std::cout << ansi::WHITE << pad_left(format_size(ask.size), 10)
              << ansi::RST;
    std::cout << ansi::BRIGHT_RED << pad_left(format_price(ask.price), 14)
              << ansi::RST;
    draw_bar(ask_row, col + 28, bar_width, ask.size / max_size, ansi::RED,
             true);
    ask_row++;
  }

  // spread
  int spread_row = row + 2 + levels_to_show;
  double spread = data.asks[0].price - data.bids[0].price;
  double spread_pct = (spread / data.last_price) * 100;
  std::cout << ansi::move_to(spread_row, col + 2) << ansi::GRAY;
  for (int i = 0; i < width - 4; ++i)
    std::cout << "·";
  std::cout << ansi::RST;
  std::cout << ansi::move_to(spread_row, col + width / 2 - 8) << ansi::DIM
            << " $" << format_price(spread) << " (" << std::fixed
            << std::setprecision(3) << spread_pct << "%) " << ansi::RST;

  // bids
  int bid_row = spread_row + 1;
  for (int i = 0; i < std::min(levels_to_show, (int)data.bids.size()); ++i) {
    const auto &bid = data.bids[i];
    std::cout << ansi::move_to(bid_row, col + 2);
    std::cout << ansi::WHITE << pad_left(format_size(bid.size), 10)
              << ansi::RST;
    std::cout << ansi::BRIGHT_GREEN << pad_left(format_price(bid.price), 14)
              << ansi::RST;
    draw_bar(bid_row, col + 28, bar_width, bid.size / max_size, ansi::GREEN,
             false);
    bid_row++;
  }
}

void render_stats(int row, int col, const MarketData &data) {
  int box_width = 18;

  // mid price
  draw_box(row, col, box_width, 5, "MID PRICE");
  if (data.last_price > 0) {
    std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD
              << ansi::BRIGHT_WHITE
              << pad_left(format_price(data.last_price), box_width - 4)
              << ansi::RST;
  }
  std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM
            << pad_left("USD", box_width - 4) << ansi::RST;

  col += box_width + 1;

  // best bid
  draw_box(row, col, box_width, 5, "BEST BID");
  if (!data.bids.empty()) {
    std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD
              << ansi::BRIGHT_GREEN
              << pad_left(format_price(data.bids[0].price), box_width - 4)
              << ansi::RST;
    std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM
              << pad_left(format_size(data.bids[0].size), box_width - 4)
              << ansi::RST;
  }

  col += box_width + 1;

  // best ask
  draw_box(row, col, box_width, 5, "BEST ASK");
  if (!data.asks.empty()) {
    std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD
              << ansi::BRIGHT_RED
              << pad_left(format_price(data.asks[0].price), box_width - 4)
              << ansi::RST;
    std::cout << ansi::move_to(row + 3, col + 2) << ansi::DIM
              << pad_left(format_size(data.asks[0].size), box_width - 4)
              << ansi::RST;
  }

  col += box_width + 1;

  // spread
  draw_box(row, col, box_width, 5, "SPREAD");
  if (!data.bids.empty() && !data.asks.empty()) {
    double spread = data.asks[0].price - data.bids[0].price;
    std::cout << ansi::move_to(row + 2, col + 2) << ansi::BOLD << ansi::YELLOW
              << pad_left("$" + format_price(spread), box_width - 4)
              << ansi::RST;
  }
}

void render_footer(int row, int width, int refresh_count) {
  std::cout << ansi::move_to(row, 1) << ansi::GRAY;
  for (int i = 0; i < width; ++i)
    std::cout << "─";
  std::cout << ansi::RST;

  std::cout << ansi::move_to(row + 1, 2) << ansi::DIM << "HYPERLIQUID LIVE"
            << ansi::RST;
  std::cout << ansi::move_to(row + 1, 25) << ansi::DIM << "refresh #"
            << refresh_count << ansi::RST;
  std::cout << ansi::move_to(row + 1, width - 22) << ansi::DIM
            << "press ctrl+c to exit" << ansi::RST;
}

// ═══════════════════════════════════════════════════════════════════════════
// main
// ═══════════════════════════════════════════════════════════════════════════

int main(int argc, char *argv[]) {
  std::string coin = "BTC";
  if (argc > 1) {
    coin = argv[1];
    // Convert to uppercase
    for (auto &c : coin)
      c = toupper(c);
  }

  constexpr int WIDTH = 80;
  constexpr int HEIGHT = 28;

  std::cout << ansi::CLEAR << ansi::HIDE_CURSOR;

  int refresh_count = 0;

  while (true) {
    MarketData data = fetch_market_data(coin);
    refresh_count++;

    std::cout << ansi::CLEAR;
    render_header(coin);
    render_stats(5, 2, data);
    render_order_book(10, 2, 45, 16, data);
    render_footer(HEIGHT - 1, WIDTH, refresh_count);

    std::cout << std::flush;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  std::cout << ansi::SHOW_CURSOR;
  std::cout << ansi::move_to(HEIGHT + 1, 1);

  return 0;
}
