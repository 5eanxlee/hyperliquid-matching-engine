#pragma once

#include "price_level.h"
#include "types.h"
#include <cassert>
#include <vector>

namespace hyperliquid {

// array-indexed price levels, o(1) access for bounded ranges
class PriceLevelsArray final : public IPriceLevels {
public:
  explicit PriceLevelsArray(const PriceBand &band)
      : band_(band),
        levels_(static_cast<size_t>(band.max_tick - band.min_tick + 1)),
        best_bid_(Sentinel::EMPTY_BID), best_ask_(Sentinel::EMPTY_ASK),
        best_bid_ptr_(nullptr), best_ask_ptr_(nullptr) {}

  LevelFIFO &get_level(Tick px) override { return levels_[idx(px)]; }
  bool has_level(Tick px) const override { return !levels_[idx(px)].empty(); }
  bool is_valid_price(Tick px) const override {
    return px >= band_.min_tick && px <= band_.max_tick;
  }
  Tick best_bid() const override { return best_bid_; }
  Tick best_ask() const override { return best_ask_; }

  LevelFIFO *best_level_ptr(Side s) override {
    return (s == Side::Bid) ? best_bid_ptr_ : best_ask_ptr_;
  }

  void set_best_bid(Tick px) override {
    best_bid_ = px;
    best_bid_ptr_ = (px == Sentinel::EMPTY_BID) ? nullptr : &levels_[idx(px)];
  }

  void set_best_ask(Tick px) override {
    best_ask_ = px;
    best_ask_ptr_ = (px == Sentinel::EMPTY_ASK) ? nullptr : &levels_[idx(px)];
  }

  void for_each_order(
      const std::function<void(Tick, OrderNode *)> &fn) const override {
    for (Tick px = band_.min_tick; px <= band_.max_tick; ++px) {
      for (OrderNode *node = levels_[idx(px)].head; node; node = node->next)
        fn(px, node);
    }
  }

  void for_each_nonempty(
      const std::function<void(Tick, const LevelFIFO &)> &fn) const override {
    for (Tick px = band_.min_tick; px <= band_.max_tick; ++px) {
      if (!levels_[idx(px)].empty())
        fn(px, levels_[idx(px)]);
    }
  }

private:
  size_t idx(Tick px) const {
    assert(px >= band_.min_tick && px <= band_.max_tick);
    return static_cast<size_t>(px - band_.min_tick);
  }

  PriceBand band_;
  std::vector<LevelFIFO> levels_;
  Tick best_bid_;
  Tick best_ask_;
  LevelFIFO *best_bid_ptr_;
  LevelFIFO *best_ask_ptr_;
};

} // namespace hyperliquid
