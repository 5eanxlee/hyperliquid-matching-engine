#include "hyperliquid/command.h"
#include "hyperliquid/types.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Simple JSON serialization without external deps for now
namespace {
std::string escape_json(const std::string &s) {
  std::string res;
  for (char c : s) {
    if (c == '"')
      res += "\\\"";
    else if (c == '\\')
      res += "\\\\";
    else if (c == '\b')
      res += "\\b";
    else if (c == '\f')
      res += "\\f";
    else if (c == '\n')
      res += "\\n";
    else if (c == '\r')
      res += "\\r";
    else if (c == '\t')
      res += "\\t";
    else
      res += c;
  }
  return res;
}
} // namespace

using namespace hyperliquid;

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <results_dir>\n";
    return 1;
  }

  std::string results_dir = argv[1];
  std::string trades_path = results_dir + "/trades.bin";
  std::string books_path = results_dir + "/book_updates.bin";
  std::string out_path = results_dir + "/data.json";

  std::vector<TradeEvent> trades;
  std::vector<BookUpdate> updates;

  // Read Trades
  {
    std::ifstream file(trades_path, std::ios::binary);
    if (!file) {
      std::cerr << "Failed to open " << trades_path << "\n";
      return 1;
    }
    TradeEvent evt;
    while (file.read(reinterpret_cast<char *>(&evt), sizeof(TradeEvent))) {
      trades.push_back(evt);
    }
    std::cout << "Read " << trades.size() << " trades.\n";
  }

  // Read Book Updates
  {
    std::ifstream file(books_path, std::ios::binary);
    if (!file) {
      std::cerr << "Failed to open " << books_path << "\n";
      return 1;
    }
    BookUpdate evt;
    while (file.read(reinterpret_cast<char *>(&evt), sizeof(BookUpdate))) {
      updates.push_back(evt);
    }
    std::cout << "Read " << updates.size() << " book updates.\n";
  }

  // Limit output for visualization to keep JSON size manageable (e.g., first
  // 5000 events) Or just all if reasonable. 200k orders might produce ~100k
  // trades. Let's cap at 10000 events total for the web UI smoothness if huge.
  // Actually, let's output a reasonable slice.

  size_t trade_limit = 5000;
  size_t book_limit = 5000;

  std::ofstream out(out_path);
  out << "{\n";

  out << "  \"trades\": [\n";
  for (size_t i = 0; i < std::min(trades.size(), trade_limit); ++i) {
    const auto &t = trades[i];
    out << "    {";
    out << "\"ts\": " << t.ts << ", ";
    out << "\"id\": " << t.maker_id << ", "; // Just showing maker as ID
    out << "\"symbol_id\": " << t.symbol_id << ", ";
    out << "\"price\": " << t.price_ticks << ", ";
    out << "\"qty\": " << t.qty;
    out << "}";
    if (i < std::min(trades.size(), trade_limit) - 1)
      out << ",";
    out << "\n";
  }
  out << "  ],\n";

  out << "  \"book_updates\": [\n";
  for (size_t i = 0; i < std::min(updates.size(), book_limit); ++i) {
    const auto &u = updates[i];
    out << "    {";
    out << "\"ts\": " << u.ts << ", ";
    out << "\"symbol_id\": " << u.symbol_id << ", ";
    out << "\"best_bid\": "
        << (u.best_bid == Sentinel::EMPTY_BID ? 0 : u.best_bid) << ", ";
    out << "\"best_ask\": "
        << (u.best_ask == Sentinel::EMPTY_ASK ? 0 : u.best_ask) << ", ";
    out << "\"bid_qty\": " << u.bid_qty << ", ";
    out << "\"ask_qty\": " << u.ask_qty;
    out << "}";
    if (i < std::min(updates.size(), book_limit) - 1)
      out << ",";
    out << "\n";
  }
  out << "  ]\n";

  out << "}\n";
  std::cout << "Written JSON to " << out_path << "\n";

  return 0;
}
