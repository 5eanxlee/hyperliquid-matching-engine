#include <cstring>
#include <fstream>
#include <hyperliquid/command.h>
#include <hyperliquid/timestamp.h>
#include <hyperliquid/types.h>
#include <iostream>
#include <random>
#include <vector>

using namespace hyperliquid;

void print_usage(const char *program) {
  std::cout
      << "Usage: " << program << " [options]\n"
      << "Options:\n"
      << "  --orders N            Number of orders to generate (default: "
         "100000)\n"
      << "  --output FILE         Output file path (default: orders.bin)\n"
      << "  --help                Show this help message\n";
}

int main(int argc, char *argv[]) {
  size_t num_orders = 100000;
  std::string output_file = "orders.bin";

  // Parse arguments
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
    if (std::strcmp(argv[i], "--orders") == 0 && i + 1 < argc) {
      num_orders = std::stoull(argv[++i]);
    }
    if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
      output_file = argv[++i];
    }
  }

  std::cout << "Generating " << num_orders << " synthetic orders...\n";

  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<> price_dist(50000, 60000);
  std::uniform_int_distribution<> qty_dist(1, 100);
  std::uniform_int_distribution<> side_dist(0, 1);
  std::uniform_int_distribution<> type_dist(0, 100);

  std::ofstream out(output_file, std::ios::binary);
  if (!out) {
    std::cerr << "Failed to open output file: " << output_file << "\n";
    return 1;
  }

  std::vector<OrderId> active_orders;
  OrderId next_order_id = 1;

  for (size_t i = 0; i < num_orders; ++i) {
    OrderCommand cmd{};
    cmd.recv_ts = TimestampUtil::now_ns();
    cmd.symbol_id = 1;      // BTC-PERP
    cmd.user_id = i % 1000; // 1000 different users

    int op_type = type_dist(gen);

    if (op_type < 70 || active_orders.empty()) {
      // 70% new orders
      cmd.type = CommandType::NewOrder;
      cmd.order_id = next_order_id++;
      cmd.price_ticks = price_dist(gen);
      cmd.qty = qty_dist(gen);
      cmd.side = (side_dist(gen) == 0) ? Side::Bid : Side::Ask;
      cmd.order_type = OrderType::Limit;
      cmd.tif = TimeInForce::GTC;
      cmd.flags = OrderFlags::NONE;

      active_orders.push_back(cmd.order_id);
    } else if (op_type < 90) {
      // 20% cancels
      cmd.type = CommandType::CancelOrder;
      size_t idx = gen() % active_orders.size();
      cmd.order_id = active_orders[idx];
      active_orders.erase(active_orders.begin() + idx);
    } else {
      // 10% modifies
      cmd.type = CommandType::ModifyOrder;
      cmd.order_id = active_orders[gen() % active_orders.size()];
      cmd.price_ticks = price_dist(gen);
      cmd.qty = qty_dist(gen);
    }

    out.write(reinterpret_cast<const char *>(&cmd), sizeof(cmd));

    if ((i + 1) % 10000 == 0) {
      std::cout << "  Generated " << (i + 1) << " orders...\n";
    }
  }

  out.close();

  std::cout << "Successfully generated " << num_orders << " orders to "
            << output_file << "\n";
  std::cout << "File size: " << (num_orders * sizeof(OrderCommand))
            << " bytes\n";

  return 0;
}
