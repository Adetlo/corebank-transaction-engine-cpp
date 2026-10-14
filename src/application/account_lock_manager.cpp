#include "corebank/application/account_lock_manager.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace corebank {

AccountLockManager::AccountLockManager(std::size_t stripes) {
  stripes_.reserve(stripes == 0 ? 1 : stripes);
  for (std::size_t i = 0; i < (stripes == 0 ? 1 : stripes); ++i) {
    stripes_.push_back(std::make_unique<std::shared_mutex>());
  }
}

std::size_t AccountLockManager::stripe_of(const AccountId& id) const noexcept {
  return std::hash<std::string>{}(id.str()) % stripes_.size();
}

AccountLockManager::ExclusiveGuard AccountLockManager::lock_exclusive(
    std::span<const AccountId> ids) {
  std::vector<std::size_t> indices;
  indices.reserve(ids.size());
  for (const auto& id : ids) {
    indices.push_back(stripe_of(id));
  }
  // Fixed global order + de-duplication is what makes this deadlock-free.
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

  std::vector<std::unique_lock<std::shared_mutex>> locks;
  locks.reserve(indices.size());
  for (const auto index : indices) {
    locks.emplace_back(*stripes_[index]);
  }
  return ExclusiveGuard{std::move(locks)};
}

AccountLockManager::SharedGuard AccountLockManager::lock_shared(const AccountId& id) {
  return SharedGuard{std::shared_lock<std::shared_mutex>{*stripes_[stripe_of(id)]}};
}

}  // namespace corebank
