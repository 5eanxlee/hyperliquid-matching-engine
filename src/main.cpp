#include "hyperliquid/cpu_affinity.h"
#include "hyperliquid/feed_handler.h"
#include "hyperliquid/matching_engine.h"
#include "hyperliquid/publisher.h"
#include "hyperliquid/timestamp.h"
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace hyperliquid;

struct ProgramConfig {
  std::string input_file;
  std::string output_dir = "results";
  std::vector<std::string> symbols;
  std::vector<int> cpu_cores;
  Tick min_price = 1;
  Tick max_price = 100000;
};

void print_usage(const char *program) {
  std::cout
      << "Usage: " << program << " [options]\n"
      << "Options:\n"
      << "  --input <file>        Input binary order file\n"
      << "  --output <dir>        Output directory (default: results)\n"
      << "  --symbols <list>      Comma-separated symbols (e.g. BTC,ETH)\n"
      << "  --price-band <min:max> Price range (default: 1:100000)\n"
      << "  --cpu-cores <list>    Comma-separated CPU cores (e.g. 0,1,2,3)\n";
}

int main(int argc, char *argv[]) {
  ProgramConfig config;

  // Basic argument parsing
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      config.input_file = argv[++i];
    } else if (std::strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
      config.output_dir = argv[++i];
    } else if (std::strcmp(argv[i], "--symbols") == 0 && i + 1 < argc) {
      std::string s = argv[++i];
      size_t pos = 0;
      while ((pos = s.find(',')) != std::string::npos) {
        config.symbols.push_back(s.substr(0, pos));
        s.erase(0, pos + 1);
      }
      config.symbols.push_back(s);
    } else if (std::strcmp(argv[i], "--price-band") == 0 && i + 1 < argc) {
      // Parse min:max
      std::string s = argv[++i];
      size_t sep = s.find(':');
      if (sep != std::string::npos) {
        config.min_price = std::stoll(s.substr(0, sep));
        config.max_price = std::stoll(s.substr(sep + 1));
      }
    } else if (std::strcmp(argv[i], "--cpu-cores") == 0 && i + 1 < argc) {
      std::string s = argv[++i];
      size_t pos = 0;
      while ((pos = s.find(',')) != std::string::npos) {
        config.cpu_cores.push_back(std::stoi(s.substr(0, pos)));
        s.erase(0, pos + 1);
      }
      config.cpu_cores.push_back(std::stoi(s));
    } else if (std::strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
  }

  if (config.input_file.empty()) {
    std::cerr << "Error: --input required\n";
    print_usage(argv[0]);
    return 1;
  }

  if (config.symbols.empty()) {
    std::cerr << "Error: --symbols required\n";
    return 1;
  }

  std::cout << "Initializing Hyperliquid Engine...\n";
  TimestampUtil::calibrate();

  // Queues
  // We allocate them on heap to avoid stack overflow or moving issues,
  // although vectors of unique_ptrs would be better, raw pointers for SPSCQueue
  // are common in low-latency code to strictly control layout. Here we use
  // vector of pointers.
  std::vector<SPSCQueue<OrderCommand, 65536> *> input_queues;
  std::vector<SPSCQueue<AnyEvent, 65536> *> output_queues;

  // We need as many input queues as symbols
  // The feed handler will dispatch by symbol_id (index in vector)
  for (size_t i = 0; i < config.symbols.size(); ++i) {
    input_queues.push_back(new SPSCQueue<OrderCommand, 65536>());
    output_queues.push_back(new SPSCQueue<AnyEvent, 65536>());
  }

  // Components
  std::vector<std::unique_ptr<MatchingEngine>> engines;

  // Create Engines
  // Create Engines
  for (size_t i = 0; i < config.symbols.size(); ++i) {
    MatchingEngine::Config engine_config{
        .symbol_id = static_cast<SymbolId>(i),
        .price_band = PriceBand(config.min_price, config.max_price),
        .input_queue = input_queues[i],
        .output_queue = output_queues[i]};

    engines.push_back(std::make_unique<MatchingEngine>(engine_config));
  }

  // Create Feed Handler
  FeedHandler::Config fh_config;
  fh_config.input_file = config.input_file;
  fh_config.param_queues = input_queues;
  auto feed_handler = std::make_unique<FeedHandler>(fh_config);

  // Create Publisher
  Publisher::Config pub_config;
  pub_config.output_dir = config.output_dir;
  pub_config.input_queues = output_queues;
  auto publisher = std::make_unique<Publisher>(pub_config);

  std::cout << "Starting " << engines.size() << " matching engines...\n";

  // Launch threads
  std::vector<std::thread> threads;

  // 1. Publisher (Core N+1)
  threads.emplace_back([&]() {
    if (config.cpu_cores.size() > config.symbols.size() + 1) {
      pin_this_thread(config.cpu_cores.back());
    }
    publisher->run();
  });

  // 2. Engines (Cores 1..N)
  for (size_t i = 0; i < engines.size(); ++i) {
    threads.emplace_back([&, i]() {
      if (config.cpu_cores.size() > i + 1) {
        pin_this_thread(config.cpu_cores[i + 1]);
      }
      engines[i]->run();
    });
  }

  // 3. Feed Handler (Core 0)
  // We run this in the main thread or a separate thread. Let's spawn a thread
  // to keep main clean.
  threads.emplace_back([&]() {
    if (!config.cpu_cores.empty()) {
      pin_this_thread(config.cpu_cores[0]);
    }
    feed_handler->run();
  });

  // Wait for threads
  for (auto &t : threads) {
    if (t.joinable())
      t.join();
  }

  // Cleanup
  for (auto *q : input_queues)
    delete q;
  for (auto *q : output_queues)
    delete q;

  return 0;
}
