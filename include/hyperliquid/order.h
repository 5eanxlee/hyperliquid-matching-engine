#pragma once

#include "types.h"
#include <algorithm>
#include <cstddef>

namespace hyperliquid {

// intrusive order node for fifo queues
struct OrderNode {
  OrderId id;
  UserId user;
  Quantity qty;
  Timestamp ts;
  uint32_t flags;

  Quantity display_qty{0}; // iceberg visible qty
  Quantity hidden_qty{0};  // iceberg hidden qty
  Timestamp expiry_ts{0};  // gtd expiry
  Tick stop_price{0};      // stop trigger

  OrderNode *prev{nullptr};
  OrderNode *next{nullptr};
  uint8_t alloc_kind{0}; // 0=heap, 1=pool

  OrderNode() = default;
  OrderNode(OrderId id_, UserId user_, Quantity qty_, Timestamp ts_,
            uint32_t flags_)
      : id(id_), user(user_), qty(qty_), ts(ts_), flags(flags_) {}

  bool is_iceberg() const noexcept {
    return (flags & OrderFlags::ICEBERG) != 0;
  }

  Quantity replenish() noexcept {
    if (hidden_qty > 0 && display_qty > 0) {
      Quantity r = std::min(hidden_qty, display_qty);
      qty = r;
      hidden_qty -= r;
      return r;
    }
    return 0;
  }
};

// fifo queue at a price level
struct LevelFIFO {
  OrderNode *head{nullptr};
  OrderNode *tail{nullptr};
  Quantity total_qty{0};

  bool empty() const noexcept { return head == nullptr; }

  void enqueue(OrderNode *node) noexcept {
    node->next = nullptr;
    node->prev = tail;
    if (tail)
      tail->next = node;
    else
      head = node;
    tail = node;
    total_qty += node->qty;
  }

  void erase(OrderNode *node) noexcept {
    if (node->prev)
      node->prev->next = node->next;
    else
      head = node->next;
    if (node->next)
      node->next->prev = node->prev;
    else
      tail = node->prev;
    total_qty -= node->qty;
    node->prev = node->next = nullptr;
  }

  void reduce_qty(OrderNode *node, Quantity reduction) noexcept {
    node->qty -= reduction;
    total_qty -= reduction;
  }
};

} // namespace hyperliquid
