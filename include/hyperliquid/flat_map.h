#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hyperliquid {

/// High-performance open-addressing hash map for OrderId -> Value
/// Uses linear probing and power-of-2 sizing
template <typename Key, typename Value, Key EmptyKey = 0> class FlatMap {
public:
  struct Entry {
    Key key;
    Value value;
  };

  explicit FlatMap(size_t initial_capacity = 16) {
    size_t cap = 16;
    while (cap < initial_capacity)
      cap <<= 1;
    resize(cap);
  }

  Value *find(Key key) {
    if (key == EmptyKey)
      return nullptr;

    size_t buf_mask = capacity_ - 1;
    size_t idx = hash(key) & buf_mask;

    size_t probes = 0;
    while (entries_[idx].key != EmptyKey) {
      if (entries_[idx].key == key) {
        return &entries_[idx].value;
      }
      idx = (idx + 1) & buf_mask;
      probes++;
      if (probes > capacity_)
        return nullptr; // Should not happen if strictly managed
    }
    return nullptr;
  }

  void insert(Key key, const Value &value) {
    if (size_ >= capacity_ * 0.7) {
      resize(capacity_ * 2);
    }

    insert_internal(key, value);
  }

  void erase(Key key) {
    size_t buf_mask = capacity_ - 1;
    size_t idx = hash(key) & buf_mask;

    while (entries_[idx].key != EmptyKey) {
      if (entries_[idx].key == key) {
        entries_[idx].key = EmptyKey;
        size_--;

        // Linear probing cleanup (shift back logic)
        // This is a simplified deletion (lazy or tombstones usually easier, but
        // let's shift) Actually, strict shifting is needed to maintain chain
        // validty.
        size_t probe_idx = (idx + 1) & buf_mask;
        while (entries_[probe_idx].key != EmptyKey) {
          Entry &probe_entry = entries_[probe_idx];
          size_t desired_idx = hash(probe_entry.key) & buf_mask;

          // Check if probe_entry belongs between idx (now empty) and probe_idx
          // Handles wrapping around buffer end
          bool wrapped = probe_idx < idx;
          // bool desired_wrapped = (desired_idx < idx);
          // (desired_idx <= probe_idx) ||
          //    (desired_idx >
          //     idx); // if desired_idx is "before" probe_idx logically

          bool can_move;
          if (!wrapped) {
            // Regular case: idx < probe_idx
            // Move if desired hash is <= idx OR > probe_idx
            can_move = (desired_idx <= idx) || (desired_idx > probe_idx);
          } else {
            // Wrapped case: probe_idx < idx
            // Move if desired index is in the "gap" [desired ... idx ... probe]
            // Logic: desired <= idx results in move?
            // It's tricky. Let's rely on standard logic:
            // if desired <= idx it belongs earlier.
            // if desired > probe_idx it belongs earlier (wrapped).
            can_move = (desired_idx <= idx) || (desired_idx > probe_idx);
          }

          if (can_move) {
            entries_[idx] = probe_entry;
            entries_[probe_idx].key = EmptyKey;
            idx = probe_idx;
          }
          probe_idx = (probe_idx + 1) & buf_mask;
        }
        return;
      }
      idx = (idx + 1) & buf_mask;
    }
  }

  // STL compatibility for finding
  Entry *end() { return nullptr; }

private:
  std::vector<Entry> entries_;
  size_t capacity_;
  size_t size_ = 0;

  void resize(size_t new_cap) {
    std::vector<Entry> old_entries = std::move(entries_);

    capacity_ = new_cap;
    entries_.resize(capacity_);
    // Init with EmptyKey
    if (EmptyKey != 0) {
      for (auto &e : entries_)
        e.key = EmptyKey;
    } else {
      std::memset(entries_.data(), 0, capacity_ * sizeof(Entry));
    }

    size_ = 0;
    for (const auto &e : old_entries) {
      if (e.key != EmptyKey) {
        insert_internal(e.key, e.value);
      }
    }
  }

  void insert_internal(Key key, const Value &val) {
    size_t buf_mask = capacity_ - 1;
    size_t idx = hash(key) & buf_mask;

    while (entries_[idx].key != EmptyKey) {
      if (entries_[idx].key == key) {
        entries_[idx].value = val;
        return;
      }
      idx = (idx + 1) & buf_mask;
    }
    entries_[idx].key = key;
    entries_[idx].value = val;
    size_++;
  }

  static size_t hash(Key k) {
    // Simple mixer for integers
    uint64_t x = static_cast<uint64_t>(k);
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<size_t>(x);
  }
};

} // namespace hyperliquid
