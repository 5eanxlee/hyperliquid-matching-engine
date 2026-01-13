#pragma once

/// Network-based feed handler using WebSocket
/// Alternative to file-based FeedHandler for real-time order submission

#include "command.h"
#include "json_serializer.h"
#include "spsc_queue.h"
#include "timestamp.h"
#include "websocket_server.h"
#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace hyperliquid {

/// Network feed handler that receives orders via WebSocket
class NetworkFeedHandler {
public:
  struct Config {
    uint16_t port{8080};
    std::string bind_address{"0.0.0.0"};
    std::vector<SPSCQueue<OrderCommand, 65536> *>
        output_queues; // Per-symbol queues
  };

  explicit NetworkFeedHandler(const Config &config)
      : config_(config), running_(false) {

    net::WebSocketServer::Config ws_config;
    ws_config.port = config.port;
    ws_config.address = config.bind_address;
    ws_config.io_threads = 1;

    server_ = std::make_unique<net::WebSocketServer>(ws_config);
  }

  /// Start the network feed handler
  void start() {
    if (running_.exchange(true))
      return;

    server_->set_on_message(
        [this](const std::string &msg) { handle_message(msg); });

    server_->start();
  }

  /// Stop the network feed handler
  void stop() {
    if (!running_.exchange(false))
      return;
    server_->stop();
  }

  /// Check if running
  bool is_running() const { return running_.load(); }

  /// Get connected client count
  size_t client_count() const { return server_->client_count(); }

  /// Broadcast a trade event to all clients
  void broadcast_trade(const TradeEvent &trade) {
    server_->broadcast(json::to_json(trade));
  }

  /// Broadcast a book update to all clients
  void broadcast_book_update(const BookUpdate &update) {
    server_->broadcast(json::to_json(update));
  }

  /// Set callback for order acknowledgment
  void set_on_order_received(std::function<void(const OrderCommand &)> cb) {
    on_order_received_ = std::move(cb);
  }

private:
  void handle_message(const std::string &msg) {
    auto result = json::parse_order_command(msg);
    if (!result.success) {
      // Could log error here
      return;
    }

    // Stamped with receive time
    result.command.recv_ts = TimestampUtil::now_ns();

    // Route to appropriate symbol queue
    SymbolId symbol = result.command.symbol_id;
    if (symbol < config_.output_queues.size() &&
        config_.output_queues[symbol]) {
      // Push to queue, spin if full
      while (!config_.output_queues[symbol]->push(result.command)) {
        SPSCQueue<OrderCommand, 65536>::pause();
      }
    }

    // Notify callback
    if (on_order_received_) {
      on_order_received_(result.command);
    }
  }

  Config config_;
  std::unique_ptr<net::WebSocketServer> server_;
  std::atomic<bool> running_;
  std::function<void(const OrderCommand &)> on_order_received_;
};

} // namespace hyperliquid
