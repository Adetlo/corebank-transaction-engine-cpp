// Striped account locks acquired in a fixed order so that transfers touching the same
// accounts serialise without deadlocking. See docs/CONCURRENCY.md.
#pragma once

#include "corebank/domain/identifiers.hpp"

#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <utility>
#include <vector>

namespace corebank {

class AccountLockManager {
 public:
  static constexpr std::size_t kDefaultStripes = 64;

  explicit AccountLockManager(std::size_t stripes = kDefaultStripes);

  class ExclusiveGuard {
   public:
    ExclusiveGuard() = default;
    explicit ExclusiveGuard(std::vector<std::unique_lock<std::shared_mutex>> locks)
        : locks_(std::move(locks)) {}
    [[nodiscard]] std::size_t lock_count() const noexcept { return locks_.size(); }

   private:
    std::vector<std::unique_lock<std::shared_mutex>> locks_;
  };

  class SharedGuard {
   public:
    SharedGuard() = default;
    explicit SharedGuard(std::shared_lock<std::shared_mutex> lock) : lock_(std::move(lock)) {}

   private:
    std::shared_lock<std::shared_mutex> lock_;
  };

  // Locks every stripe covering `ids` exclusively, in ascending stripe order,
  // de-duplicating stripes shared by several ids.
  [[nodiscard]] ExclusiveGuard lock_exclusive(std::span<const AccountId> ids);
  [[nodiscard]] SharedGuard lock_shared(const AccountId& id);

  [[nodiscard]] std::size_t stripe_of(const AccountId& id) const noexcept;
  [[nodiscard]] std::size_t stripe_count() const noexcept { return stripes_.size(); }

 private:
  std::vector<std::unique_ptr<std::shared_mutex>> stripes_;
};

}  // namespace corebank
