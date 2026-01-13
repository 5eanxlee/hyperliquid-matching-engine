#pragma once

#include "order.h"
#include "price_level.h"
#include "types.h"
#include <functional>
#include <map>

namespace hyperliquid {

// tree-based price levels, o(log n) for sparse ranges
class PriceLevelsAVL final : public IPriceLevels {
public:
  PriceLevelsAVL()
      : best_bid_(Sentinel::EMPTY_BID), best_ask_(Sentinel::EMPTY_ASK),
        best_bid_ptr_(nullptr), best_ask_ptr_(nullptr) {}

  LevelFIFO &get_level(Tick px) override { return levels_[px]; }

  bool has_level(Tick px) const override {
    auto it = levels_.find(px);
    return it != levels_.end() && !it->second.empty();
  }

  bool is_valid_price(Tick px) const override {
    return px > Sentinel::EMPTY_BID && px < Sentinel::EMPTY_ASK;
  }

  Tick best_bid() const override { return best_bid_; }
  Tick best_ask() const override { return best_ask_; }

  LevelFIFO *best_level_ptr(Side s) override {
    return (s == Side::Bid) ? best_bid_ptr_ : best_ask_ptr_;
  }

  void set_best_bid(Tick px) override {
    best_bid_ = px;
    if (px == Sentinel::EMPTY_BID)
      best_bid_ptr_ = nullptr;
    else {
      auto it = levels_.find(px);
      best_bid_ptr_ = (it != levels_.end()) ? &it->second : nullptr;
    }
  }

  void set_best_ask(Tick px) override {
    best_ask_ = px;
    if (px == Sentinel::EMPTY_ASK)
      best_ask_ptr_ = nullptr;
    else {
      auto it = levels_.find(px);
      best_ask_ptr_ = (it != levels_.end()) ? &it->second : nullptr;
    }
  }

  void for_each_order(
      const std::function<void(Tick, OrderNode *)> &fn) const override {
    for (const auto &[px, level] : levels_)
      for (OrderNode *node = level.head; node; node = node->next)
        fn(px, node);
  }

  void for_each_nonempty(
      const std::function<void(Tick, const LevelFIFO &)> &fn) const override {
    for (const auto &[px, level] : levels_)
      if (!level.empty())
        fn(px, level);
  }

  Tick find_next_bid(Tick current) const {
    if (current == Sentinel::EMPTY_BID)
      return Sentinel::EMPTY_BID;
    auto it = levels_.lower_bound(current);
    while (it != levels_.begin()) {
      --it;
      if (!it->second.empty())
        return it->first;
    }
    return Sentinel::EMPTY_BID;
  }

  Tick find_next_ask(Tick current) const {
    if (current == Sentinel::EMPTY_ASK)
      return Sentinel::EMPTY_ASK;
    auto it = levels_.upper_bound(current);
    while (it != levels_.end()) {
      if (!it->second.empty())
        return it->first;
      ++it;
    }
    return Sentinel::EMPTY_ASK;
  }

  void cleanup_empty_levels() {
    for (auto it = levels_.begin(); it != levels_.end();)
      it = it->second.empty() ? levels_.erase(it) : std::next(it);
  }

  size_t num_levels() const { return levels_.size(); }

private:
  std::map<Tick, LevelFIFO> levels_;
  Tick best_bid_;
  Tick best_ask_;
  LevelFIFO *best_bid_ptr_;
  LevelFIFO *best_ask_ptr_;
};

} // namespace hyperliquid
