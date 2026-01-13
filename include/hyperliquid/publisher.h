#pragma once

#include "event.h"
#include "spsc_queue.h"
#include <fstream>
#include <string>
#include <vector>

namespace hyperliquid {

class Publisher {
public:
  struct Config {
    std::string output_dir;
    std::vector<SPSCQueue<AnyEvent, 65536> *> input_queues;
  };

  explicit Publisher(const Config &config);

  void run();

private:
  std::vector<SPSCQueue<AnyEvent, 65536> *> queues_;
  std::ofstream trades_log_;
  std::ofstream book_updates_log_;
  std::string output_dir_;
};

} // namespace hyperliquid
