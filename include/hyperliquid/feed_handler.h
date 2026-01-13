#pragma once

#include "command.h"
#include "spsc_queue.h"
#include "types.h"
#include <string>
#include <vector>

namespace hyperliquid {

class FeedHandler {
public:
  struct Config {
    std::string input_file;
    std::vector<SPSCQueue<OrderCommand, 65536> *>
        param_queues; // Queues indexed by symbol_id
  };

  explicit FeedHandler(const Config &config);

  // identifying the main loop
  void run();

private:
  std::string input_path_;
  std::vector<SPSCQueue<OrderCommand, 65536> *> queues_;
};

} // namespace hyperliquid
