// order book visualization demo
// shows live order book and trade execution

#include <chrono>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace hyperliquid;

// ansi colors
namespace color {
constexpr auto reset = "\033[0m";
constexpr auto red = "\033[31m";
constexpr auto green = "\033[32m";
constexpr auto yellow = "\033[33m";
constexpr auto cyan = "\033[36m";
constexpr auto dim = "\033[2m";
constexpr auto bold = "\033[1m";
} // namespace color

void clear_screen() { std::cout << "\033[2J\033[H"; }

void print_header() {
  std::cout << color::bold << color::cyan;
  std::cout << R"(
  ╔═══════════════════════════════════════════════════════════════╗
  ║           HYPERLIQUID MATCHING ENGINE - LIVE DEMO             ║
  ╚═══════════════════════════════════════════════════════════════╝
)" << color::reset;
}

void print_book_state(Tick best_bid, Tick best_ask, Quantity bid_qty,
                      Quantity ask_qty) {
  std::cout << color::dim
            << "  ─────────────────────────────────────────────────\n"
            << color::reset;
  std::cout << color::bold << "  ORDER BOOK" << color::reset << "\n\n";

  // simple visualization of top of book
  double spread = 0;
  if (best_ask != Sentinel::EMPTY_ASK && best_bid != Sentinel::EMPTY_BID) {
    spread = (best_ask - best_bid) / 100.0;
  }

  if (best_ask != Sentinel::EMPTY_ASK) {
    int bar = std::min(40, static_cast<int>(ask_qty / 5));
    std::cout << color::red << "  ASK  " << std::setw(8) << std::fixed
              << std::setprecision(2) << (best_ask / 100.0) << "  "
              << std::setw(5) << ask_qty << "  ";
    for (int i = 0; i < bar; ++i)
      std::cout << "█";
    std::cout << color::reset << "\n";
  } else {
    std::cout << color::dim << "  ASK  (empty)" << color::reset << "\n";
  }

  std::cout << color::yellow << "  ──────── spread: " << spread << " ────────"
            << color::reset << "\n";

  if (best_bid != Sentinel::EMPTY_BID) {
    int bar = std::min(40, static_cast<int>(bid_qty / 5));
    std::cout << color::green << "  BID  " << std::setw(8) << std::fixed
              << std::setprecision(2) << (best_bid / 100.0) << "  "
              << std::setw(5) << bid_qty << "  ";
    for (int i = 0; i < bar; ++i)
      std::cout << "█";
    std::cout << color::reset << "\n";
  } else {
    std::cout << color::dim << "  BID  (empty)" << color::reset << "\n";
  }
}

void print_trades(const std::vector<TradeEvent> &trades) {
  std::cout << "\n"
            << color::dim
            << "  ─────────────────────────────────────────────────\n"
            << color::reset;
  std::cout << color::bold << "  RECENT TRADES" << color::reset << "\n\n";

  if (trades.empty()) {
    std::cout << color::dim << "  (no trades yet)" << color::reset << "\n";
    return;
  }

  size_t start = trades.size() > 8 ? trades.size() - 8 : 0;
  for (size_t i = start; i < trades.size(); ++i) {
    const auto &t = trades[i];
    std::cout << "  " << color::cyan << std::setw(8) << std::fixed
              << std::setprecision(2) << (t.price_ticks / 100.0) << color::reset
              << "  qty: " << std::setw(4) << t.qty << "  #" << t.maker_id
              << " → #" << t.taker_id << "\n";
  }
}

void print_stats(int orders, int total_trades, int resting, double elapsed) {
  std::cout << "\n"
            << color::dim
            << "  ─────────────────────────────────────────────────\n"
            << color::reset;
  std::cout << color::bold << "  STATS" << color::reset << "\n";
  std::cout << "  orders: " << orders << "  trades: " << total_trades
            << "  resting: " << resting
            << "  rate: " << static_cast<int>(orders / std::max(0.001, elapsed))
            << " ord/sec\n";
}

int main() {
  // init book: price range 95.00 - 105.00
  PriceBand band(9500, 10500, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  std::vector<TradeEvent> trades;
  Quantity best_bid_qty = 0, best_ask_qty = 0;

  book.set_on_trade([&](const TradeEvent &t) { trades.push_back(t); });

  book.set_on_book_update([&](const BookUpdate &u) {
    best_bid_qty = u.bid_qty;
    best_ask_qty = u.ask_qty;
  });

  std::mt19937_64 rng(42);
  std::uniform_int_distribution<Tick> price_dist(9800, 10200);
  std::uniform_int_distribution<Quantity> qty_dist(10, 100);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> action_dist(0, 9);

  OrderId next_id = 1;
  int resting_orders = 0;
  auto start_time = std::chrono::steady_clock::now();

  std::cout << "\n  press ctrl+c to exit\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));

  // seed initial orders
  for (int i = 0; i < 30; ++i) {
    OrderCommand cmd{};
    cmd.order_id = next_id++;
    cmd.user_id = 100 + (i % 10);
    cmd.price_ticks = price_dist(rng);
    cmd.qty = qty_dist(rng);
    cmd.side = (side_dist(rng) == 0) ? Side::Bid : Side::Ask;
    cmd.order_type = OrderType::Limit;
    cmd.tif = TimeInForce::GTC;
    auto result = book.submit_limit(cmd);
    if (result.remaining > 0)
      resting_orders++;
  }

  // main loop
  for (int iter = 0; iter < 300; ++iter) {
    clear_screen();
    print_header();

    // add random order
    OrderCommand cmd{};
    cmd.order_id = next_id++;
    cmd.user_id = 100 + (rng() % 50);
    cmd.price_ticks = price_dist(rng);
    cmd.qty = qty_dist(rng);

    int action = action_dist(rng);
    if (action < 6) {
      // 60% limit orders
      cmd.side = (side_dist(rng) == 0) ? Side::Bid : Side::Ask;
      cmd.order_type = OrderType::Limit;
      cmd.tif = TimeInForce::GTC;
      auto result = book.submit_limit(cmd);
      if (result.remaining > 0)
        resting_orders++;
    } else if (action < 8) {
      // 20% market orders
      cmd.side = (side_dist(rng) == 0) ? Side::Bid : Side::Ask;
      cmd.order_type = OrderType::Market;
      book.submit_market(cmd);
    } else {
      // 20% cancels
      if (next_id > 10) {
        if (book.cancel(rng() % (next_id - 5) + 1)) {
          resting_orders = std::max(0, resting_orders - 1);
        }
      }
    }

    auto now = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(now - start_time).count();

    print_book_state(book.best_bid(), book.best_ask(), best_bid_qty,
                     best_ask_qty);
    print_trades(trades);
    print_stats(static_cast<int>(next_id - 1), static_cast<int>(trades.size()),
                resting_orders, elapsed);

    std::cout << "\n"
              << color::dim << "  iteration " << (iter + 1) << "/300"
              << color::reset << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }

  std::cout << "\n  demo complete!\n\n";
  return 0;
}
