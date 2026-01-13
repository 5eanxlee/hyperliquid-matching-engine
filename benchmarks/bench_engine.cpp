#include <chrono>
#include <hyperliquid/command.h>
#include <hyperliquid/order_book.h>
#include <hyperliquid/price_levels_array.h>
#include <hyperliquid/timestamp.h>
#include <hyperliquid/types.h>
#include <iomanip>
#include <iostream>
#include <random>

using namespace hyperliquid;

void benchmark_throughput() {
  std::cout << "\n========================================\n";
  std::cout << "  MATCHING ENGINE THROUGHPUT BENCHMARK\n";
  std::cout << "========================================\n\n";

  // Setup
  std::cout << "[1/4] Initializing...\n";
  TimestampUtil::calibrate();
  double tsc_to_ns = TimestampUtil::get_tsc_to_ns_factor();
  std::cout << "      TSC frequency calibrated: " << std::fixed
            << std::setprecision(2) << (1.0 / tsc_to_ns) << " GHz\n";

  PriceBand band(50000, 60000, 1);
  OrderBook<PriceLevelsArray> book(1, PriceLevelsArray(band),
                                   PriceLevelsArray(band));

  constexpr size_t NUM_ORDERS = 1'000'000;
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<Tick> price_dist(51000, 59000);
  std::uniform_int_distribution<Quantity> qty_dist(1, 100);

  // Generate realistic order flow
  std::cout << "\n[2/4] Generating " << NUM_ORDERS << " orders...\n";
  std::vector<OrderCommand> orders;
  orders.reserve(NUM_ORDERS);

  for (size_t i = 0; i < NUM_ORDERS; ++i) {
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
    cmd.recv_ts = 0;
    orders.push_back(cmd);
  }
  std::cout << "      Orders generated (50% buy / 50% sell)\n";

  // Benchmark
  std::cout << "\n[3/4] Running benchmark...\n";

  auto wall_start = std::chrono::steady_clock::now();
  uint64_t cpu_start = TimestampUtil::rdtsc();

  size_t trades = 0;
  size_t resting = 0;

  for (const auto &cmd : orders) {
    auto result = book.submit_limit(cmd);
    if (result.filled > 0) {
      trades++;
    }
    if (result.remaining > 0) {
      resting++;
    }
  }

  uint64_t cpu_end = TimestampUtil::rdtsc();
  auto wall_end = std::chrono::steady_clock::now();

  // Results
  std::cout << "\n[4/4] Results:\n";
  std::cout << "========================================\n\n";

  auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
      wall_end - wall_start);
  double seconds = duration_us.count() / 1'000'000.0;
  double throughput = NUM_ORDERS / seconds;

  uint64_t total_cycles = cpu_end - cpu_start;
  double cycles_per_op = static_cast<double>(total_cycles) / NUM_ORDERS;
  double ns_per_op = cycles_per_op * tsc_to_ns;

  std::cout << "** Throughput **\n";
  std::cout << "  Orders processed: " << NUM_ORDERS << "\n";
  std::cout << "  Trades executed:  " << trades << " (" << std::fixed
            << std::setprecision(1) << (100.0 * trades / NUM_ORDERS) << "%)\n";
  std::cout << "  Resting orders:   " << resting << "\n";
  std::cout << "  Time elapsed:     " << std::fixed << std::setprecision(3)
            << seconds << " seconds\n";
  std::cout << "  Throughput:       " << std::fixed << std::setprecision(0)
            << throughput << " msgs/sec\n";
  std::cout << "  Avg latency:      " << std::fixed << std::setprecision(0)
            << ns_per_op << " ns/op\n\n";

  // Latency percentiles (if profiling enabled)
#ifdef HYPERLIQUID_PROFILING
  auto &tracker = book.get_latency_tracker();
  tracker.compute_percentiles();

  std::cout << "** Latency Distribution (RDTSC) **\n";
  std::cout << "  Samples:  " << tracker.count() << "\n";
  std::cout << "  Min:      " << std::fixed << std::setprecision(0)
            << tracker.min() * tsc_to_ns << " ns (" << tracker.min()
            << " cycles)\n";
  std::cout << "  p50:      " << tracker.p50() * tsc_to_ns << " ns\n";
  std::cout << "  p90:      " << tracker.p90() * tsc_to_ns << " ns\n";
  std::cout << "  p95:      " << tracker.p95() * tsc_to_ns << " ns\n";
  std::cout << "  p99:      " << tracker.p99() * tsc_to_ns << " ns\n";
  std::cout << "  p99.9:    " << tracker.p99_9() * tsc_to_ns << " ns\n";
  std::cout << "  p99.99:   " << tracker.p99_99() * tsc_to_ns << " ns\n";
  std::cout << "  Max:      " << tracker.max() * tsc_to_ns << " ns ("
            << tracker.max() << " cycles)\n";
  std::cout << "  Avg:      " << tracker.avg() * tsc_to_ns << " ns\n\n";

  tracker.export_csv("latency_results.csv", tsc_to_ns);
  std::cout << "Latency data exported to: latency_results.csv\n";
#else
  std::cout << "Note: Compile with -DHYPERLIQUID_PROFILING=ON for detailed "
               "latency stats\n";
#endif

  std::cout << "\n========================================\n";
  std::cout << "Benchmark complete!\n";
  std::cout << "========================================\n\n";
}

int main() {
  benchmark_throughput();
  return 0;
}
