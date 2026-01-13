#include "hyperliquid/publisher.h"
#include <filesystem>
#include <iostream>
#include <thread>

namespace hyperliquid {

Publisher::Publisher(const Config &config)
    : queues_(config.input_queues), output_dir_(config.output_dir) {

  // Ensure output directory exists
  std::filesystem::create_directories(output_dir_);

  std::string trades_path = output_dir_ + "/trades.bin";
  std::string book_path = output_dir_ + "/book_updates.bin";

  trades_log_.open(trades_path, std::ios::binary | std::ios::out);
  book_updates_log_.open(book_path, std::ios::binary | std::ios::out);

  if (!trades_log_) {
    std::cerr << "Publisher: Failed to open " << trades_path << "\n";
  }
  if (!book_updates_log_) {
    std::cerr << "Publisher: Failed to open " << book_path << "\n";
  }
}

void Publisher::run() {
  std::cout << "Publisher listener started...\n";

  AnyEvent evt;
  bool work_done;
  uint64_t total_events = 0;

  while (true) {
    work_done = false;

    // Round-robin poll all queues
    for (auto *queue : queues_) {
      while (queue->pop(evt)) {
        work_done = true;
        total_events++;

        if (evt.type == EventType::Trade) {
          trades_log_.write(reinterpret_cast<const char *>(&evt.trade),
                            sizeof(TradeEvent));
        } else {
          book_updates_log_.write(
              reinterpret_cast<const char *>(&evt.book_update),
              sizeof(BookUpdate));
        }
      }
    }

    if (!work_done) {
      // If all queues empty, yield
      std::this_thread::yield();
    }

    // TODO: Periodic flush or shutdown check
  }
}

} // namespace hyperliquid
