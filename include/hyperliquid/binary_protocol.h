#pragma once
/**
 * Binary Protocol for High-Performance IPC
 *
 * Fixed-size packed messages for zero-copy deserialization.
 * All multi-byte integers are little-endian.
 * Prices/sizes stored as fixed-point (raw * 1e8).
 */

#include <cstdint>
#include <cstring>
#include <span>

namespace hyperliquid {
namespace binary {

// Message types for binary protocol
enum class MsgType : uint8_t {
  ADD_ORDER = 1,
  CANCEL_ORDER = 2,
  MODIFY_ORDER = 3,
  RESET = 4,
  STATS_REQUEST = 5,
};

// Response types
enum class RspType : uint8_t {
  ACK = 1,
  TRADE = 2,
  STATS = 3,
  ERROR = 4,
};

// Order side for binary protocol (maps to Side::Bid/Ask)
enum class OrderSide : uint8_t {
  BUY = 0,
  SELL = 1,
};

#pragma pack(push, 1)

/**
 * Header for all messages (4 bytes)
 */
struct Header {
  uint16_t length; // Total message length including header
  MsgType type;    // Message type
  uint8_t flags;   // Reserved for future use
};
static_assert(sizeof(Header) == 4);

/**
 * Add Order Message
 */
struct AddOrder {
  Header header;
  uint64_t order_id;     // Unique order identifier
  uint64_t price_raw;    // Price * 1e8 (fixed point)
  uint64_t size_raw;     // Size * 1e8 (fixed point)
  OrderSide side;        // Buy or Sell
  uint32_t timestamp_ns; // Nanosecond timestamp (relative)

  static constexpr MsgType TYPE = MsgType::ADD_ORDER;

  void init(uint64_t id, double price, double size, OrderSide s) {
    header.length = sizeof(AddOrder);
    header.type = TYPE;
    header.flags = 0;
    order_id = id;
    price_raw = static_cast<uint64_t>(price * 1e8);
    size_raw = static_cast<uint64_t>(size * 1e8);
    side = s;
    timestamp_ns = 0;
  }

  [[nodiscard]] double price() const {
    return static_cast<double>(price_raw) / 1e8;
  }
  [[nodiscard]] double size() const {
    return static_cast<double>(size_raw) / 1e8;
  }
};

/**
 * Cancel Order Message
 */
struct CancelOrder {
  Header header;
  uint64_t order_id;

  static constexpr MsgType TYPE = MsgType::CANCEL_ORDER;

  void init(uint64_t id) {
    header.length = sizeof(CancelOrder);
    header.type = TYPE;
    header.flags = 0;
    order_id = id;
  }
};

/**
 * Modify Order Message
 */
struct ModifyOrder {
  Header header;
  uint64_t order_id;
  uint64_t new_price_raw;
  uint64_t new_size_raw;
  uint8_t modify_flags; // 1=price, 2=size, 3=both

  static constexpr MsgType TYPE = MsgType::MODIFY_ORDER;
};

/**
 * Reset Message (header only)
 */
struct Reset {
  Header header;

  static constexpr MsgType TYPE = MsgType::RESET;

  void init() {
    header.length = sizeof(Reset);
    header.type = TYPE;
    header.flags = 0;
  }
};

/**
 * Stats Response
 */
struct StatsRsp {
  Header header;
  uint64_t orders_processed;
  uint64_t trades_executed;
  uint64_t resting_orders;
  uint64_t avg_latency_ns;
  uint64_t min_latency_ns;
  uint64_t max_latency_ns;

  static constexpr RspType TYPE = RspType::STATS;
};

/**
 * Trade Response
 */
struct TradeRsp {
  Header header;
  uint64_t trade_id;
  uint64_t maker_order_id;
  uint64_t taker_order_id;
  uint64_t price_raw;
  uint64_t size_raw;

  static constexpr RspType TYPE = RspType::TRADE;

  [[nodiscard]] double price() const {
    return static_cast<double>(price_raw) / 1e8;
  }
  [[nodiscard]] double size() const {
    return static_cast<double>(size_raw) / 1e8;
  }
};

#pragma pack(pop)

/**
 * Zero-copy parser
 */
class Parser {
public:
  static MsgType peek_type(std::span<const uint8_t> data) {
    if (data.size() < sizeof(Header))
      return static_cast<MsgType>(0);
    return reinterpret_cast<const Header *>(data.data())->type;
  }

  static uint16_t peek_length(std::span<const uint8_t> data) {
    if (data.size() < sizeof(Header))
      return 0;
    return reinterpret_cast<const Header *>(data.data())->length;
  }

  template <typename T> static const T *parse(std::span<const uint8_t> data) {
    if (data.size() < sizeof(T))
      return nullptr;
    return reinterpret_cast<const T *>(data.data());
  }
};

/**
 * Serializer
 */
class Serializer {
public:
  template <typename T>
  static std::span<const uint8_t> serialize(const T &msg) {
    return std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(&msg),
                                    sizeof(T));
  }
};

} // namespace binary
} // namespace hyperliquid
