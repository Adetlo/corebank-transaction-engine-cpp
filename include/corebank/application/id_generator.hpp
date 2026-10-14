// Thread-safe generator for engine-assigned identifiers.
#pragma once

#include "corebank/domain/identifiers.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <random>
#include <string>
#include <string_view>

namespace corebank {

class IdGenerator {
 public:
  IdGenerator();
  explicit IdGenerator(std::uint64_t seed);

  [[nodiscard]] TransactionId next_transaction_id();
  [[nodiscard]] BatchId next_batch_id(const std::string& business_date);
  [[nodiscard]] AccountId next_account_id();
  [[nodiscard]] IdempotencyKey next_idempotency_key();

 private:
  [[nodiscard]] std::string next_token(std::string_view prefix);

  std::mutex mutex_;
  std::mt19937_64 engine_;
  std::atomic<std::uint64_t> counter_{0};
};

}  // namespace corebank
