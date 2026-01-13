#pragma once

#include <cassert>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sys/mman.h>
#include <vector>
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

namespace hyperliquid {

// slab allocator for o(1) alloc/free
template <typename T, size_t SLAB_SIZE = (1 << 20)> class SlabPool {
  struct Node {
    Node *next;
  };

  std::vector<std::pair<void *, size_t>> raw_slabs_;
  Node *free_list_{nullptr};
  size_t capacity_{0};
  size_t in_use_{0};

  void add_slab() {
    const size_t num_objects = SLAB_SIZE / sizeof(T);
    assert(num_objects > 0 && "slab too small");
    size_t size_bytes = num_objects * sizeof(T);

    void *ptr = nullptr;
#if defined(__linux__)
    ptr = mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
    if (ptr == MAP_FAILED) {
      ptr = mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (ptr != MAP_FAILED)
        madvise(ptr, size_bytes, MADV_HUGEPAGE);
    }
#else
    ptr = mmap(nullptr, size_bytes, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif

    if (ptr == MAP_FAILED) {
      perror("mmap failed");
      std::abort();
    }

    T *base = reinterpret_cast<T *>(ptr);
    for (size_t i = 0; i < num_objects; ++i) {
      auto node = reinterpret_cast<Node *>(base + i);
      node->next = free_list_;
      free_list_ = node;
    }

    capacity_ += num_objects;
    raw_slabs_.push_back({ptr, size_bytes});
  }

public:
  explicit SlabPool(size_t initial_slabs = 1) {
    for (size_t i = 0; i < initial_slabs; ++i)
      add_slab();
  }

  ~SlabPool() {
    for (auto &slab : raw_slabs_)
      munmap(slab.first, slab.second);
  }

  SlabPool(const SlabPool &) = delete;
  SlabPool &operator=(const SlabPool &) = delete;
  SlabPool(SlabPool &&) = delete;
  SlabPool &operator=(SlabPool &&) = delete;

  T *alloc() {
    if (!free_list_)
      add_slab();
    Node *node = free_list_;
    free_list_ = free_list_->next;
    ++in_use_;
    return reinterpret_cast<T *>(node);
  }

  void free(T *ptr) noexcept {
    assert(ptr && "null free");
    assert(in_use_ > 0 && "empty pool");
    auto node = reinterpret_cast<Node *>(ptr);
    node->next = free_list_;
    free_list_ = node;
    --in_use_;
  }

  size_t in_use() const noexcept { return in_use_; }
  size_t capacity() const noexcept { return capacity_; }
  size_t num_slabs() const noexcept { return raw_slabs_.size(); }
};

// stl-compatible allocator wrapper
template <typename T> class PoolAllocator {
public:
  using value_type = T;

  PoolAllocator() = default;
  explicit PoolAllocator(SlabPool<T> *pool) : pool_(pool) {}

  template <typename U>
  PoolAllocator(const PoolAllocator<U> &other)
      : pool_(reinterpret_cast<SlabPool<T> *>(other.pool_)) {}

  T *allocate(size_t n) {
    if (n == 1 && pool_)
      return pool_->alloc();
    return static_cast<T *>(::operator new(n * sizeof(T)));
  }

  void deallocate(T *ptr, size_t n) noexcept {
    if (n == 1 && pool_)
      pool_->free(ptr);
    else
      ::operator delete(ptr);
  }

  template <typename U> struct rebind {
    using other = PoolAllocator<U>;
  };
  bool operator==(const PoolAllocator &rhs) const { return pool_ == rhs.pool_; }
  bool operator!=(const PoolAllocator &rhs) const { return pool_ != rhs.pool_; }

private:
  template <typename U> friend class PoolAllocator;
  SlabPool<T> *pool_{nullptr};
};

} // namespace hyperliquid
