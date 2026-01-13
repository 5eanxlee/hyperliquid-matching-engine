#pragma once

#include "order.h"
#include "types.h"
#include <functional>

namespace hyperliquid {

// price level storage interface
class IPriceLevels {
public:
  virtual ~IPriceLevels() = default;

  virtual LevelFIFO &get_level(Tick px) = 0;
  virtual bool has_level(Tick px) const = 0;
  virtual bool is_valid_price(Tick px) const = 0;
  virtual Tick best_bid() const = 0;
  virtual Tick best_ask() const = 0;
  virtual LevelFIFO *best_level_ptr(Side s) = 0;
  virtual void set_best_bid(Tick px) = 0;
  virtual void set_best_ask(Tick px) = 0;
  virtual void
  for_each_order(const std::function<void(Tick, OrderNode *)> &fn) const = 0;
  virtual void for_each_nonempty(
      const std::function<void(Tick, const LevelFIFO &)> &fn) const = 0;
};

} // namespace hyperliquid
