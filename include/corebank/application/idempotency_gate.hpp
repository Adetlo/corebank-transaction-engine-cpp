// In-process gate that serialises concurrent requests carrying the same idempotency key:
// the first request runs, the others wait and then replay the stored outcome.
#pragma once

#include "corebank/domain/identifiers.hpp"

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>

namespace corebank {

class IdempotencyGate {
 public:
  class Token {
   public:
    Token(IdempotencyGate& gate, std::string key) : gate_(&gate), key_(std::move(key)) {}
    Token(const Token&) = delete;
    Token& operator=(const Token&) = delete;
    Token(Token&& other) noexcept : gate_(other.gate_), key_(std::move(other.key_)) {
      other.gate_ = nullptr;
    }
    Token& operator=(Token&&) = delete;
    ~Token();

   private:
    IdempotencyGate* gate_;
    std::string key_;
  };

  // Blocks while another thread holds `key`.
  [[nodiscard]] Token acquire(const IdempotencyKey& key);
  [[nodiscard]] std::size_t in_flight() const;

 private:
  void release(const std::string& key);

  mutable std::mutex mutex_;
  std::condition_variable released_;
  std::unordered_set<std::string> in_flight_;
};

}  // namespace corebank
