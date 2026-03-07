#include "corebank/application/idempotency_gate.hpp"

#include <cstddef>
#include <mutex>
#include <string>

namespace corebank {

IdempotencyGate::Token::~Token() {
  if (gate_ != nullptr) {
    gate_->release(key_);
  }
}

IdempotencyGate::Token IdempotencyGate::acquire(const IdempotencyKey& key) {
  std::unique_lock lock(mutex_);
  released_.wait(lock, [&] { return !in_flight_.contains(key.str()); });
  in_flight_.insert(key.str());
  return Token{*this, key.str()};
}

std::size_t IdempotencyGate::in_flight() const {
  const std::lock_guard lock(mutex_);
  return in_flight_.size();
}

void IdempotencyGate::release(const std::string& key) {
  {
    const std::lock_guard lock(mutex_);
    in_flight_.erase(key);
  }
  released_.notify_all();
}

}  // namespace corebank
