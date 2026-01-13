#pragma once

#include <chrono>
#include <cstdint>

#if defined(__x86_64__) || defined(_M_X64)
#include <x86intrin.h>
#endif

namespace hyperliquid {

/// High-resolution timestamp utilities using RDTSC for nanosecond precision
class TimestampUtil {
public:
  /// Initialize TSC frequency calibration
  static void calibrate() {
    auto start = std::chrono::steady_clock::now();
    uint64_t tsc_start = rdtsc();

    // Busy wait for ~100ms
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
               .count() < 100)
      ;

    uint64_t tsc_end = rdtsc();
    auto end = std::chrono::steady_clock::now();

    uint64_t tsc_diff = tsc_end - tsc_start;
    auto ns_diff =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count();

    tsc_to_ns_factor_ =
        static_cast<double>(ns_diff) / static_cast<double>(tsc_diff);
    ns_to_tsc_factor_ =
        static_cast<double>(tsc_diff) / static_cast<double>(ns_diff);
  }

  /// Get current timestamp in CPU cycles (RDTSC)
  static inline uint64_t rdtsc() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
    return __rdtsc();
#else
    // Fallback for non-x86 architectures
    return std::chrono::steady_clock::now().time_since_epoch().count();
#endif
  }

  /// Get current timestamp in nanoseconds
  static inline uint64_t now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  /// Convert TSC cycles to nanoseconds
  static inline uint64_t cycles_to_ns(uint64_t cycles) noexcept {
    return static_cast<uint64_t>(static_cast<double>(cycles) *
                                 tsc_to_ns_factor_);
  }

  /// Convert nanoseconds to TSC cycles
  static inline uint64_t ns_to_cycles(uint64_t ns) noexcept {
    return static_cast<uint64_t>(static_cast<double>(ns) * ns_to_tsc_factor_);
  }

  /// Get TSC to nanosecond conversion factor
  static double get_tsc_to_ns_factor() noexcept { return tsc_to_ns_factor_; }

private:
  static inline double tsc_to_ns_factor_ = 1.0; // Default 1:1 if not calibrated
  static inline double ns_to_tsc_factor_ = 1.0;
};

/// RAII timer for measuring latency
class LatencyTimer {
public:
  LatencyTimer() : start_(TimestampUtil::rdtsc()) {}

  uint64_t elapsed_cycles() const noexcept {
    return TimestampUtil::rdtsc() - start_;
  }

  uint64_t elapsed_ns() const noexcept {
    return TimestampUtil::cycles_to_ns(elapsed_cycles());
  }

  void reset() noexcept { start_ = TimestampUtil::rdtsc(); }

private:
  uint64_t start_;
};

} // namespace hyperliquid
