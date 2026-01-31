#include "corebank/application/id_generator.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <random>
#include <string>
#include <string_view>

namespace corebank {
namespace {

std::uint64_t entropy_seed() {
  std::random_device device;
  const auto now =
      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  return (static_cast<std::uint64_t>(device()) << 32U) ^ device() ^ now;
}

}  // namespace

IdGenerator::IdGenerator() : IdGenerator(entropy_seed()) {}

IdGenerator::IdGenerator(std::uint64_t seed) : engine_(seed) {}

std::string IdGenerator::next_token(std::string_view prefix) {
  std::uint64_t random = 0;
  {
    const std::lock_guard lock(mutex_);
    random = engine_();
  }
  const std::uint64_t sequence = counter_.fetch_add(1, std::memory_order_relaxed);
  std::array<char, 48> buffer{};
  const int written = std::snprintf(buffer.data(), buffer.size(), "%.*s-%016llX-%06llu",
                                    static_cast<int>(prefix.size()), prefix.data(),
                                    static_cast<unsigned long long>(random),
                                    static_cast<unsigned long long>(sequence % 1000000ULL));
  return {buffer.data(), static_cast<std::size_t>(written > 0 ? written : 0)};
}

TransactionId IdGenerator::next_transaction_id() {
  return TransactionId::unchecked(next_token("TXN"));
}

BatchId IdGenerator::next_batch_id(const std::string& business_date) {
  return BatchId::unchecked("EOD-" + business_date + "-" + next_token("R").substr(2));
}

AccountId IdGenerator::next_account_id() {
  return AccountId::unchecked(next_token("ACC"));
}

IdempotencyKey IdGenerator::next_idempotency_key() {
  return IdempotencyKey::unchecked(next_token("KEY"));
}

}  // namespace corebank
