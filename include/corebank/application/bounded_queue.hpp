// Bounded MPMC queue with cooperative cancellation through std::stop_token.
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <utility>

namespace corebank {

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : capacity_(capacity) {}

  // Non-blocking; false when the queue is full or closed.
  [[nodiscard]] bool try_push(T item) {
    {
      const std::lock_guard lock(mutex_);
      if (closed_ || items_.size() >= capacity_) {
        return false;
      }
      items_.push_back(std::move(item));
    }
    not_empty_.notify_one();
    return true;
  }

  // Waits up to `timeout` for room. Moves from `item` only on success so the caller
  // keeps ownership when the queue stays full or is closed.
  template <typename Rep, typename Period>
  [[nodiscard]] bool try_push_for(T& item, std::chrono::duration<Rep, Period> timeout) {
    {
      std::unique_lock lock(mutex_);
      if (!not_full_.wait_for(lock, timeout,
                              [this] { return closed_ || items_.size() < capacity_; })) {
        return false;
      }
      if (closed_) {
        return false;
      }
      items_.push_back(std::move(item));
    }
    not_empty_.notify_one();
    return true;
  }

  // Blocks until there is room, the queue is closed, or a stop is requested.
  [[nodiscard]] bool push(T item, std::stop_token stop) {
    {
      std::unique_lock lock(mutex_);
      not_full_.wait(lock, stop, [this] { return closed_ || items_.size() < capacity_; });
      if (closed_ || stop.stop_requested()) {
        return false;
      }
      items_.push_back(std::move(item));
    }
    not_empty_.notify_one();
    return true;
  }

  // Blocks until an item is available. Returns nullopt when stopped or closed-and-empty.
  [[nodiscard]] std::optional<T> pop(std::stop_token stop) {
    std::optional<T> item;
    {
      std::unique_lock lock(mutex_);
      not_empty_.wait(lock, stop, [this] { return closed_ || !items_.empty(); });
      if (items_.empty()) {
        return std::nullopt;
      }
      item.emplace(std::move(items_.front()));
      items_.pop_front();
    }
    not_full_.notify_one();
    return item;
  }

  // Drains everything that is still queued (used to fail pending work on shutdown).
  [[nodiscard]] std::deque<T> drain() {
    std::deque<T> out;
    {
      const std::lock_guard lock(mutex_);
      out.swap(items_);
    }
    not_full_.notify_all();
    return out;
  }

  // No further pushes succeed; consumers keep draining what is left.
  void close() {
    {
      const std::lock_guard lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

  [[nodiscard]] bool closed() const {
    const std::lock_guard lock(mutex_);
    return closed_;
  }

  [[nodiscard]] std::size_t size() const {
    const std::lock_guard lock(mutex_);
    return items_.size();
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

 private:
  mutable std::mutex mutex_;
  std::condition_variable_any not_empty_;
  std::condition_variable_any not_full_;
  std::deque<T> items_;
  std::size_t capacity_;
  bool closed_{false};
};

}  // namespace corebank
