#include "hyperliquid/feed_handler.h"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace hyperliquid {

FeedHandler::FeedHandler(const Config &config)
    : input_path_(config.input_file), queues_(config.param_queues) {}

void FeedHandler::run() {
  int fd = open(input_path_.c_str(), O_RDONLY);
  if (fd == -1) {
    std::cerr << "FeedHandler: Failed to open input file: " << input_path_
              << "\n";
    return;
  }

  // Get file size
  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    std::cerr << "FeedHandler: Failed to stat file\n";
    close(fd);
    return;
  }

  size_t file_size = static_cast<size_t>(sb.st_size);
  if (file_size == 0) {
    std::cout << "FeedHandler: Empty input file\n";
    close(fd);
    return;
  }

  // Map file into memory
  void *addr = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    std::cerr << "FeedHandler: mmap failed\n";
    close(fd);
    return;
  }

  // We can close fd after mmap
  close(fd);

  std::cout << "FeedHandler: Started processing " << file_size
            << " bytes via mmap\n";

  // Hint that we will access sequentially
  madvise(addr, file_size, MADV_SEQUENTIAL);

  const OrderCommand *cmds = static_cast<const OrderCommand *>(addr);
  size_t num_cmds = file_size / sizeof(OrderCommand);
  uint64_t count = 0;

  for (size_t i = 0; i < num_cmds; ++i) {
    const OrderCommand &cmd = cmds[i];

    // Validate symbol_id to avoid segfaults
    if (cmd.symbol_id >= queues_.size() || !queues_[cmd.symbol_id]) {
      // Typically we might log this, but for high perf we might just skip or
      // count errors std::cerr << "FeedHandler: Invalid symbol_id " <<
      // cmd.symbol_id << "\n";
      continue;
    }

    auto *queue = queues_[cmd.symbol_id];

    // Busy wait until we can push
    while (!queue->push(cmd)) {
      // Optimization: Using CPU relax instruction would be better here,
      // but we will do that in the "Matching Logic" optimization step.
      // For now, keep yield to play nice with restricted core counts.
      std::this_thread::yield();
    }

    count++;
    if (count % 1000000 == 0) {
      std::cout << "FeedHandler: Processed " << count << " commands\n";
    }
  }

  // Cleanup
  munmap(addr, file_size);

  std::cout << "FeedHandler: Finished. Total commands: " << count << "\n";
}

} // namespace hyperliquid
