#pragma once

/// Lightweight WebSocket server for real-time event streaming
/// Uses Boost.Beast (header-only) for WebSocket implementation
///
/// To use: require Boost.Beast dependency in CMakeLists.txt
/// This is an optional component - the engine works without it

#ifdef HYPERLIQUID_WEBSOCKET

#include "command.h"
#include "types.h"
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace hyperliquid {
namespace net {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

/// Active WebSocket session
class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
  explicit WebSocketSession(tcp::socket socket) : ws_(std::move(socket)) {}

  /// Start the session (accept WebSocket handshake)
  void start() {
    ws_.async_accept([self = shared_from_this()](beast::error_code ec) {
      if (!ec) {
        self->do_read();
      }
    });
  }

  /// Send a text message
  void send(const std::string &message) {
    auto msg = std::make_shared<std::string>(message);
    asio::post(ws_.get_executor(),
               [self = shared_from_this(), msg]() { self->do_write(*msg); });
  }

  /// Set callback for incoming messages
  void set_on_message(std::function<void(const std::string &)> cb) {
    on_message_ = std::move(cb);
  }

  /// Set callback for disconnect
  void set_on_close(std::function<void()> cb) { on_close_ = std::move(cb); }

private:
  void do_read() {
    ws_.async_read(buffer_, [self = shared_from_this()](beast::error_code ec,
                                                        std::size_t) {
      if (ec) {
        if (self->on_close_)
          self->on_close_();
        return;
      }

      if (self->on_message_) {
        self->on_message_(beast::buffers_to_string(self->buffer_.data()));
      }
      self->buffer_.consume(self->buffer_.size());
      self->do_read();
    });
  }

  void do_write(const std::string &message) {
    ws_.text(true);
    ws_.async_write(
        asio::buffer(message),
        [self = shared_from_this()](beast::error_code ec, std::size_t) {
          if (ec) {
            // Handle write error
          }
        });
  }

  websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buffer_;
  std::function<void(const std::string &)> on_message_;
  std::function<void()> on_close_;
};

/// WebSocket server for broadcasting events
class WebSocketServer {
public:
  /// Configuration for the server
  struct Config {
    uint16_t port{8080};
    std::string address{"0.0.0.0"};
    size_t io_threads{1};
  };

  explicit WebSocketServer(const Config &config)
      : config_(config), acceptor_(ioc_), running_(false) {}

  ~WebSocketServer() { stop(); }

  /// Start the server
  void start() {
    if (running_.exchange(true))
      return;

    tcp::endpoint endpoint(asio::ip::make_address(config_.address),
                           config_.port);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(asio::socket_base::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    do_accept();

    // Start IO threads
    for (size_t i = 0; i < config_.io_threads; ++i) {
      io_threads_.emplace_back([this]() { ioc_.run(); });
    }
  }

  /// Stop the server
  void stop() {
    if (!running_.exchange(false))
      return;

    ioc_.stop();
    for (auto &t : io_threads_) {
      if (t.joinable())
        t.join();
    }
    io_threads_.clear();
  }

  /// Broadcast a message to all connected clients
  void broadcast(const std::string &message) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto &session : sessions_) {
      session->send(message);
    }
  }

  /// Set callback for incoming messages from clients
  void set_on_message(std::function<void(const std::string &)> cb) {
    on_message_ = std::move(cb);
  }

  /// Get number of connected clients
  size_t client_count() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    return sessions_.size();
  }

private:
  void do_accept() {
    acceptor_.async_accept([this](beast::error_code ec, tcp::socket socket) {
      if (ec) {
        if (running_)
          do_accept();
        return;
      }

      auto session = std::make_shared<WebSocketSession>(std::move(socket));

      // Track session
      {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        sessions_.insert(session);
      }

      // Handle messages
      session->set_on_message([this](const std::string &msg) {
        if (on_message_)
          on_message_(msg);
      });

      // Handle disconnect
      auto weak_session = std::weak_ptr<WebSocketSession>(session);
      session->set_on_close([this, weak_session]() {
        if (auto s = weak_session.lock()) {
          std::lock_guard<std::mutex> lock(sessions_mutex_);
          sessions_.erase(s);
        }
      });

      session->start();

      if (running_)
        do_accept();
    });
  }

  Config config_;
  asio::io_context ioc_;
  tcp::acceptor acceptor_;
  std::atomic<bool> running_;
  std::vector<std::thread> io_threads_;

  mutable std::mutex sessions_mutex_;
  std::unordered_set<std::shared_ptr<WebSocketSession>> sessions_;

  std::function<void(const std::string &)> on_message_;
};

} // namespace net
} // namespace hyperliquid

#else // !HYPERLIQUID_WEBSOCKET

// Stub implementation when WebSocket is disabled
#include <cstdint>
#include <functional>
#include <string>

namespace hyperliquid {
namespace net {

class WebSocketServer {
public:
  struct Config {
    uint16_t port{8080};
    std::string address{"0.0.0.0"};
    size_t io_threads{1};
  };

  explicit WebSocketServer(const Config &) {}
  void start() {}
  void stop() {}
  void broadcast(const std::string &) {}
  void set_on_message(std::function<void(const std::string &)>) {}
  size_t client_count() const { return 0; }
};

} // namespace net
} // namespace hyperliquid

#endif // HYPERLIQUID_WEBSOCKET
