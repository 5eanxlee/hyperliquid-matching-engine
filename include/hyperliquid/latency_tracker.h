#pragma once

#include "timestamp.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace hyperliquid {

/// High-performance latency tracker using pre-allocated buffer
class LatencyTracker {
public:
  explicit LatencyTracker(size_t capacity = 1'000'000)
      : samples_(capacity), count_(0), capacity_(capacity) {}

  /// Record a latency sample (in TSC cycles)
  inline void record(uint64_t start_cycles, uint64_t end_cycles) {
    if (count_ < capacity_) {
      samples_[count_++] = end_cycles - start_cycles;
    }
  }

  /// Compute percentiles (must call before accessing percentiles)
  void compute_percentiles() {
    if (count_ == 0)
      return;

    // Partial sort for percentiles
    std::sort(samples_.begin(), samples_.begin() + count_);

    p50_ = samples_[count_ * 50 / 100];
    p90_ = samples_[count_ * 90 / 100];
    p95_ = samples_[count_ * 95 / 100];
    p99_ = samples_[count_ * 99 / 100];
    p99_9_ = samples_[count_ * 999 / 1000];
    p99_99_ = samples_[count_ * 9999 / 10000];
    max_ = samples_[count_ - 1];
    min_ = samples_[0];

    // Compute average
    uint64_t sum = 0;
    for (size_t i = 0; i < count_; ++i) {
      sum += samples_[i];
    }
    avg_ = sum / count_;
  }

  /// Export to CSV
  void export_csv(const std::string &path, double tsc_to_ns_factor) const {
    std::ofstream out(path);
    out << "percentile,cycles,nanoseconds\n";
    out << "min," << min_ << "," << min_ * tsc_to_ns_factor << "\n";
    out << "p50," << p50_ << "," << p50_ * tsc_to_ns_factor << "\n";
    out << "p90," << p90_ << "," << p90_ * tsc_to_ns_factor << "\n";
    out << "p95," << p95_ << "," << p95_ * tsc_to_ns_factor << "\n";
    out << "p99," << p99_ << "," << p99_ * tsc_to_ns_factor << "\n";
    out << "p99.9," << p99_9_ << "," << p99_9_ * tsc_to_ns_factor << "\n";
    out << "p99.99," << p99_99_ << "," << p99_99_ * tsc_to_ns_factor << "\n";
    out << "max," << max_ << "," << max_ * tsc_to_ns_factor << "\n";
    out << "avg," << avg_ << "," << avg_ * tsc_to_ns_factor << "\n";
  }

  // Accessors (in cycles)
  size_t count() const { return count_; }
  uint64_t p50() const { return p50_; }
  uint64_t p90() const { return p90_; }
  uint64_t p95() const { return p95_; }
  uint64_t p99() const { return p99_; }
  uint64_t p99_9() const { return p99_9_; }
  uint64_t p99_99() const { return p99_99_; }
  uint64_t max() const { return max_; }
  uint64_t min() const { return min_; }
  uint64_t avg() const { return avg_; }

private:
  std::vector<uint64_t> samples_;
  size_t count_;
  size_t capacity_;

  // Computed percentiles (in cycles)
  uint64_t p50_ = 0, p90_ = 0, p95_ = 0, p99_ = 0, p99_9_ = 0, p99_99_ = 0;
  uint64_t max_ = 0, min_ = 0, avg_ = 0;
};

} // namespace hyperliquid
