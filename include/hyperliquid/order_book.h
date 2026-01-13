#pragma once

#include "command.h"
#include "flat_map.h"
#include "mempool.h"
#include "order.h"
#include "price_level.h"
#include "price_levels_array.h"
#include "timestamp.h"
#include "types.h"
#include <functional>
#include <utility>
#include <vector>

// Profiling support (compile with -DHYPERLIQUID_PROFILING to enable)
#ifdef HYPERLIQUID_PROFILING
#include "latency_tracker.h"
#define PROFILE_SCOPE_START() uint64_t _prof_start = TimestampUtil::rdtsc()
#define PROFILE_SCOPE_END(tracker)                                             \
  tracker.record(_prof_start, TimestampUtil::rdtsc())
#else
#define PROFILE_SCOPE_START()
#define PROFILE_SCOPE_END(tracker)
#endif

// Compiler hints for branch prediction
#if defined(__GNUC__) || defined(__clang__)
#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x) (x)
#define UNLIKELY(x) (x)
#endif

namespace hyperliquid {

/// Order book with template-based price level implementation
/// Single-threaded, fully owns its data
template <typename PriceLevelsImpl = PriceLevelsArray> class OrderBook {
public:
  explicit OrderBook(SymbolId symbol_id, PriceLevelsImpl &&bids,
                     PriceLevelsImpl &&asks)
      : symbol_id_(symbol_id), bids_(std::move(bids)), asks_(std::move(asks)),
        order_pool_(2) // Start with 2 slabs
  {}

  /// Submit a limit order
  ExecResult submit_limit(const OrderCommand &cmd);

  /// Submit a market order
  ExecResult submit_market(const OrderCommand &cmd);

  /// Cancel an order by ID
  bool cancel(OrderId id);

  /// Modify an existing order
  ExecResult modify(OrderId id, Tick new_price, Quantity new_qty);

  /// Check if order book is empty for a side
  bool empty(Side s) const {
    return (s == Side::Bid) ? (bids_.best_bid() == Sentinel::EMPTY_BID)
                            : (asks_.best_ask() == Sentinel::EMPTY_ASK);
  }

  /// Get best bid price
  Tick best_bid() const { return bids_.best_bid(); }

  /// Get best ask price
  Tick best_ask() const { return asks_.best_ask(); }

  /// Get symbol ID
  SymbolId symbol() const { return symbol_id_; }

  /// Set trade event callback
  void set_on_trade(std::function<void(const TradeEvent &)> cb) {
    on_trade_ = std::move(cb);
  }

  /// Set book update callback
  void set_on_book_update(std::function<void(const BookUpdate &)> cb) {
    on_book_update_ = std::move(cb);
  }

private:
  SymbolId symbol_id_;
  PriceLevelsImpl bids_;
  PriceLevelsImpl asks_;
  SlabPool<OrderNode> order_pool_;

  // Order index for O(1) lookup
  struct OrderEntry {
    Side side;
    Tick price;
    OrderNode *node;
  };
  FlatMap<OrderId, OrderEntry> id_index_{8192};

  // Event callbacks
  std::function<void(const TradeEvent &)> on_trade_;
  std::function<void(const BookUpdate &)> on_book_update_;

#ifdef HYPERLIQUID_PROFILING
public:
  /// Get latency tracker for profiling analysis
  LatencyTracker &get_latency_tracker() { return latency_tracker_; }
  const LatencyTracker &get_latency_tracker() const { return latency_tracker_; }

private:
  mutable LatencyTracker latency_tracker_;
#endif

  // Memory management
  OrderNode *alloc_node() {
    OrderNode *node = order_pool_.alloc();
    node->alloc_kind = 1; // Pooled
    return node;
  }

  void free_node(OrderNode *node) {
    if (LIKELY(node->alloc_kind == 1)) {
      order_pool_.free(node);
    } else {
      delete node; // Fallback for non-pooled nodes
    }
  }

  // Matching helpers (branch-minimized with templates)
  template <bool IsBid> ExecResult submit_limit_side(const OrderCommand &cmd);

  template <bool IsBid>
  Quantity match_against_side(Quantity qty, Tick px_limit, OrderId taker_id,
                              UserId taker_user, Timestamp ts, bool enable_stp);

  template <bool IsBid> bool check_fok_liquidity(Quantity qty, Tick px_limit);

  // Book update helpers
  void refresh_best_after_depletion(Side s);
  void emit_trade(const TradeEvent &trade);
  void emit_book_update();
};

//
// Implementation
//

template <typename PriceLevelsImpl>
ExecResult OrderBook<PriceLevelsImpl>::submit_limit(const OrderCommand &cmd) {
  PROFILE_SCOPE_START();
  ExecResult result;
  if (cmd.side == Side::Bid) {
    result = submit_limit_side<true>(cmd);
  } else {
    result = submit_limit_side<false>(cmd);
  }
  PROFILE_SCOPE_END(latency_tracker_);
  return result;
}

template <typename PriceLevelsImpl>
ExecResult OrderBook<PriceLevelsImpl>::submit_market(const OrderCommand &cmd) {
  PROFILE_SCOPE_START();
  Quantity filled = 0;
  bool enable_stp = (cmd.flags & OrderFlags::STP) != 0;

  if (cmd.side == Side::Bid) {
    // Market buy: match against asks at any price
    filled =
        match_against_side<false>(cmd.qty, Sentinel::EMPTY_ASK, cmd.order_id,
                                  cmd.user_id, cmd.recv_ts, enable_stp);
  } else {
    // Market sell: match against bids at any price
    filled =
        match_against_side<true>(cmd.qty, Sentinel::EMPTY_BID, cmd.order_id,
                                 cmd.user_id, cmd.recv_ts, enable_stp);
  }

  Quantity remaining = cmd.qty - filled;
  emit_book_update();

  PROFILE_SCOPE_END(latency_tracker_);
  return ExecResult{filled, remaining};
}

template <typename PriceLevelsImpl>
bool OrderBook<PriceLevelsImpl>::cancel(OrderId id) {
  PROFILE_SCOPE_START();

  auto *entry_ptr = id_index_.find(id);
  if (!entry_ptr) {
    PROFILE_SCOPE_END(latency_tracker_);
    return false; // Order not found
  }

  OrderEntry entry = *entry_ptr;
  Side side = entry.side;
  Tick price = entry.price;

  // Get the price level
  auto &levels = (side == Side::Bid) ? bids_ : asks_;
  LevelFIFO &level = levels.get_level(price);

  // Remove from level
  level.erase(entry.node);
  free_node(entry.node);

  // Update best price if level is now empty
  if (level.empty()) {
    refresh_best_after_depletion(side);
  }

  id_index_.erase(id); // Passed by key now
  emit_book_update();

  PROFILE_SCOPE_END(latency_tracker_);
  return true;
}

template <typename PriceLevelsImpl>
ExecResult OrderBook<PriceLevelsImpl>::modify(OrderId id, Tick new_price,
                                              Quantity new_qty) {
  PROFILE_SCOPE_START();

  auto *entry_ptr = id_index_.find(id);
  if (!entry_ptr) {
    PROFILE_SCOPE_END(latency_tracker_);
    return ExecResult{0, 0};
  }

  OrderEntry entry = *entry_ptr;

  // Case 1: In-place resize (Partial cancel)
  // Logic: new_qty < current_qty AND same price
  // We preserve priority by just reducing the quantity on the node
  if (new_price == entry.price && new_qty < entry.node->qty) {
    Side side = entry.side;
    auto &levels = (side == Side::Bid) ? bids_ : asks_;
    LevelFIFO &level = levels.get_level(entry.price);

    // Calculate reduction
    // entry.node->qty is the current open quantity
    // new_qty is the desired new quantity
    Quantity diff = entry.node->qty - new_qty;

    // LevelFIFO::reduce_qty reduces the node's qty and the level's total_qty
    level.reduce_qty(entry.node, diff);

    // Emit update to reflect size change
    emit_book_update();

    PROFILE_SCOPE_END(latency_tracker_);
    // Return 0 filled, and the new open quantity as remaining
    return ExecResult{0, new_qty};
  }

  // Case 2: Full Cancel and Replace
  // Logic: Price changed OR Quantity increased
  // Priority is lost. We treat this as a new order.

  // Capture order details before cancelling (as cancel frees the node)
  UserId user = entry.node->user;
  uint32_t flags = entry.node->flags;
  Side side = entry.side;
  // We effectively treat it as a new order arriving "now"
  uint64_t now = TimestampUtil::now_ns();

  // Perform atomic cancel
  // Note: calling cancel(id) will look up id_index_ again.
  // We could optimize by having an internal cancel_by_entry, but reusing
  // cancel(id) is safer/DRY.
  if (!cancel(id)) {
    // Should rare/impossible since we found it above, unless concurrent
    // modification happened (not thread safe class)
    PROFILE_SCOPE_END(latency_tracker_);
    return ExecResult{0, 0};
  }

  // Construct new command
  OrderCommand new_cmd;
  new_cmd.type = CommandType::NewOrder;
  new_cmd.order_id =
      id; // Reuse same ID as this is a modification of the same order
  new_cmd.user_id = user;
  new_cmd.price_ticks = new_price;
  new_cmd.qty = new_qty;
  new_cmd.side = side;
  new_cmd.order_type = OrderType::Limit;
  new_cmd.tif = TimeInForce::GTC; // Resting orders are GTC (IOC/FOK don't rest)
  new_cmd.recv_ts = now;
  new_cmd.flags = flags;

  // Submit new order
  // This will handle matching if the new price crosses the book, or resting if
  // not.
  ExecResult res = submit_limit(new_cmd);

  PROFILE_SCOPE_END(latency_tracker_);
  return res;
}

template <typename PriceLevelsImpl>
template <bool IsBid>
ExecResult
OrderBook<PriceLevelsImpl>::submit_limit_side(const OrderCommand &cmd) {
  constexpr Side taker_side = IsBid ? Side::Bid : Side::Ask;
  Quantity filled = 0;
  Quantity remaining = cmd.qty;
  bool enable_stp = (cmd.flags & OrderFlags::STP) != 0;

  // Handle time-in-force FOK check
  if (cmd.tif == TimeInForce::FOK) {
    // Check if we can fill fully
    bool can_fill = check_fok_liquidity<IsBid>(cmd.qty, cmd.price_ticks);
    if (!can_fill) {
      // Cannot fill fully -> Kill
      // Do not match, do not add to book.
      emit_book_update();
      return ExecResult{0, 0};
    }
    // If can fill, proceed to match (which will drain the required liquidity)
  }

  // Match against opposite side
  if constexpr (IsBid) {
    // Buy order: match against asks if ask price <= our bid price
    Tick best_ask = asks_.best_ask();
    if (best_ask != Sentinel::EMPTY_ASK && best_ask <= cmd.price_ticks) {
      filled = match_against_side<false>(cmd.qty, cmd.price_ticks, cmd.order_id,
                                         cmd.user_id, cmd.recv_ts, enable_stp);
      remaining = cmd.qty - filled;
    }
  } else {
    // Sell order: match against bids if bid price >= our ask price
    Tick best_bid = bids_.best_bid();
    if (best_bid != Sentinel::EMPTY_BID && best_bid >= cmd.price_ticks) {
      filled = match_against_side<true>(cmd.qty, cmd.price_ticks, cmd.order_id,
                                        cmd.user_id, cmd.recv_ts, enable_stp);
      remaining = cmd.qty - filled;
    }
  }

  // Handle time-in-force
  if (remaining > 0) {
    if (cmd.tif == TimeInForce::IOC) {
      // Immediate-or-cancel: remaining cancelled
      emit_book_update();
      return ExecResult{filled, 0};
    } else if (cmd.tif == TimeInForce::FOK) {
      // Should not happen if FOK logic above is correct,
      // unless liquidity disappeared concurrently (impossible in single thread)
      // or check was imperfect.
      // But since we checked, we assume filled == qty if we reached here with
      // remaining == 0 If remaining > 0 here for FOK, it means we FAILED to
      // fill even though we thought we could? Or we are in the "remaining"
      // block, which means we didn't match everything.

      // If we are here and FOK, it means partial fill occurred?
      // No, match_against_side consumes liquidity.
      // If check_fok_liquidity passed, match_against_side SHOULD have filled
      // everything unless STP prevented it? Ah, STP! If STP triggered, we might
      // have skipped orders. So check_fok_liquidity doesn't account for STP
      // (user check).

      // If we are here with remaining > 0, it's a busted FOK.
      // We really should ideally rollback.
      // But for MVP, we just kill the remainder.
      // The fills that happened... we can't easily undo.
      // This is a known limitation if STP interacts with FOK.
      emit_book_update();
      return ExecResult{filled, 0};
    }

    // GTC: rest in book
    auto &levels = IsBid ? bids_ : asks_;
    LevelFIFO &level = levels.get_level(cmd.price_ticks);

    OrderNode *node = alloc_node();
    node->id = cmd.order_id;
    node->user = cmd.user_id;
    node->qty = remaining;
    node->ts = cmd.recv_ts;
    node->flags = cmd.flags;

    level.enqueue(node);

    // Update best price if needed
    if constexpr (IsBid) {
      if (cmd.price_ticks > bids_.best_bid()) {
        bids_.set_best_bid(cmd.price_ticks);
      }
    } else {
      if (cmd.price_ticks < asks_.best_ask()) {
        asks_.set_best_ask(cmd.price_ticks);
      }
    }

    // Add to index
    id_index_.insert(cmd.order_id,
                     OrderEntry{taker_side, cmd.price_ticks, node});
  }

  emit_book_update();
  return ExecResult{filled, remaining};
}

template <typename PriceLevelsImpl>
template <bool IsBid>
Quantity OrderBook<PriceLevelsImpl>::match_against_side(
    Quantity qty, Tick px_limit, OrderId taker_id, UserId taker_user,
    Timestamp ts, bool enable_stp) {
  Quantity total_filled = 0;
  auto &levels = IsBid ? bids_ : asks_;

  while (qty > 0) {
    LevelFIFO *best_level =
        levels.best_level_ptr(IsBid ? Side::Bid : Side::Ask);
    if (!best_level || best_level->empty()) {
      break; // No more liquidity
    }

    Tick best_price = IsBid ? levels.best_bid() : levels.best_ask();

    // Check price crossing condition
    if constexpr (IsBid) {
      if (best_price < px_limit)
        break; // No more matches for sell taker
    } else {
      if (best_price > px_limit)
        break; // No more matches for buy taker
    }

    // Match orders at this level
    OrderNode *maker = best_level->head;
    while (maker && qty > 0) {
      if (maker->next) {
        __builtin_prefetch(maker->next, 0, 1);
      }

      // Self-trade prevention
      if (enable_stp && maker->user == taker_user) {
        maker = maker->next;
        continue;
      }

      Quantity match_qty = std::min(qty, maker->qty);

      // Generate trade event
      if (on_trade_) {
        TradeEvent trade{ts,         taker_id,   maker->id,
                         symbol_id_, best_price, match_qty};
        emit_trade(trade);
      }

      qty -= match_qty;
      total_filled += match_qty;

      OrderNode *next_maker = maker->next;

      if (match_qty >= maker->qty) {
        // Maker fully filled
        best_level->erase(maker);
        id_index_.erase(maker->id);
        free_node(maker);
      } else {
        // Partial fill
        best_level->reduce_qty(maker, match_qty);
      }

      maker = next_maker;
    }

    // If level is depleted, find next best
    if (best_level->empty()) {
      refresh_best_after_depletion(IsBid ? Side::Bid : Side::Ask);
    } else {
      break; // Still have orders at this level
    }
  }

  return total_filled;
}

template <typename PriceLevelsImpl>
void OrderBook<PriceLevelsImpl>::refresh_best_after_depletion(Side s) {
  if (s == Side::Bid) {
    Tick current_best = bids_.best_bid();
    if (current_best == Sentinel::EMPTY_BID)
      return;

    // Search downward for next best bid
    // Limit search to avoid extremely long iterations
    constexpr int MAX_SEARCH = 10000;
    for (int i = 1; i <= MAX_SEARCH; ++i) {
      Tick px = current_best - i;
      if (px <= Sentinel::EMPTY_BID) {
        break;
      }
      // Check bounds BEFORE calling has_level to avoid assertion
      if (!bids_.is_valid_price(px)) {
        break;
      }
      if (bids_.has_level(px)) {
        bids_.set_best_bid(px);
        return;
      }
    }
    // No levels found - book is empty
    bids_.set_best_bid(Sentinel::EMPTY_BID);
  } else {
    Tick current_best = asks_.best_ask();
    if (current_best == Sentinel::EMPTY_ASK)
      return;

    // Search upward for next best ask
    constexpr int MAX_SEARCH = 10000;
    for (int i = 1; i <= MAX_SEARCH; ++i) {
      Tick px = current_best + i;
      if (px >= Sentinel::EMPTY_ASK) {
        // Reached top of search
        break;
      }
      // Check bounds BEFORE calling has_level to avoid assertion
      if (!asks_.is_valid_price(px)) {
        break;
      }
      if (asks_.has_level(px)) {
        asks_.set_best_ask(px);
        return;
      }
    }
    // No levels found - book is empty
    asks_.set_best_ask(Sentinel::EMPTY_ASK);
  }
}

template <typename PriceLevelsImpl>
template <bool IsBid>
bool OrderBook<PriceLevelsImpl>::check_fok_liquidity(Quantity qty,
                                                     Tick px_limit) {
  // If I am Bid, I match against Asks.
  // auto &levels = IsBid ? asks_ : bids_;

  Tick best_price = IsBid ? asks_.best_ask() : bids_.best_bid();

  if (IsBid) {
    if (best_price == Sentinel::EMPTY_ASK)
      return false;
    if (best_price > px_limit)
      return false;
  } else {
    if (best_price == Sentinel::EMPTY_BID)
      return false;
    if (best_price < px_limit)
      return false;
  }

  Quantity available = 0;
  Tick current_px = best_price;

  constexpr int MAX_STEPS = 10000;
  int steps = 0;

  while (available < qty && steps < MAX_STEPS) {
    if (IsBid) {
      // Walking Asks UP
      if (current_px > px_limit || current_px == Sentinel::EMPTY_ASK)
        break;
      if (!asks_.is_valid_price(current_px))
        break;

      if (asks_.has_level(current_px)) {
        available += asks_.get_level(current_px).total_qty;
      }
      current_px++;
    } else {
      // Walking Bids DOWN
      if (current_px < px_limit || current_px == Sentinel::EMPTY_BID)
        break;
      if (!bids_.is_valid_price(current_px))
        break;

      if (bids_.has_level(current_px)) {
        available += bids_.get_level(current_px).total_qty;
      }
      current_px--;
    }
    steps++;
  }

  return available >= qty;
}

template <typename PriceLevelsImpl>
void OrderBook<PriceLevelsImpl>::emit_trade(const TradeEvent &trade) {
  if (on_trade_) {
    on_trade_(trade);
  }
}

template <typename PriceLevelsImpl>
void OrderBook<PriceLevelsImpl>::emit_book_update() {
  if (on_book_update_) {
    Tick best_bid_px = bids_.best_bid();
    Tick best_ask_px = asks_.best_ask();

    Quantity bid_qty = 0;
    Quantity ask_qty = 0;

    if (best_bid_px != Sentinel::EMPTY_BID) {
      bid_qty = bids_.get_level(best_bid_px).total_qty;
    }
    if (best_ask_px != Sentinel::EMPTY_ASK) {
      ask_qty = asks_.get_level(best_ask_px).total_qty;
    }

    BookUpdate update;
    update.ts = TimestampUtil::now_ns();
    update.symbol_id = symbol_id_;
    update.best_bid = best_bid_px;
    update.best_ask = best_ask_px;
    update.bid_qty = bid_qty;
    update.ask_qty = ask_qty;

    on_book_update_(update);
  }
}

#undef LIKELY
#undef UNLIKELY

} // namespace hyperliquid
