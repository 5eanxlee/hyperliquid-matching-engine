#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif

namespace hyperliquid {

// lock-free spsc ring buffer, size must be power of 2
template <typename T, size_t N> class SPSCQueue {
  static_assert((N & (N - 1)) == 0, "size must be power of 2");
  static_assert(std::is_trivially_copyable<T>::value,
                "T must be trivially copyable");

public:
  static void pause() {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__)
    asm volatile("yield");
#endif
  }

  SPSCQueue() : head_(0), tail_(0) {}

  SPSCQueue(const SPSCQueue &) = delete;
  SPSCQueue &operator=(const SPSCQueue &) = delete;
  SPSCQueue(SPSCQueue &&) = delete;
  SPSCQueue &operator=(SPSCQueue &&) = delete;

  bool push(const T &item) noexcept {
    const size_t current_tail = tail_.load(std::memory_order_relaxed);
    const size_t next_tail = (current_tail + 1) & MASK;
    if (next_tail == head_.load(std::memory_order_acquire))
      return false;
    buffer_[current_tail] = item;
    tail_.store(next_tail, std::memory_order_release);
    return true;
  }

  bool pop(T &item) noexcept {
    const size_t current_head = head_.load(std::memory_order_relaxed);
    if (current_head == tail_.load(std::memory_order_acquire))
      return false;
    item = buffer_[current_head];
    head_.store((current_head + 1) & MASK, std::memory_order_release);
    return true;
  }

  bool empty() const noexcept {
    return head_.load(std::memory_order_relaxed) ==
           tail_.load(std::memory_order_acquire);
  }

  size_t size() const noexcept {
    return (tail_.load(std::memory_order_acquire) -
            head_.load(std::memory_order_acquire)) &
           MASK;
  }

  static constexpr size_t capacity() noexcept { return N - 1; }

private:
  static constexpr size_t MASK = N - 1;
  alignas(64) std::atomic<size_t> head_;
  alignas(64) std::atomic<size_t> tail_;
  T buffer_[N];
};

} // namespace hyperliquid
