// Request and receipt types exchanged with the TransactionService.
#pragma once

#include "corebank/domain/account.hpp"
#include "corebank/domain/identifiers.hpp"
#include "corebank/domain/money.hpp"
#include "corebank/domain/transaction.hpp"

#include <string>

namespace corebank {

struct CreateAccountRequest {
  AccountId id;
  std::string name;
  AccountType type{AccountType::Customer};
  CurrencyCode currency;
  bool allow_overdraft{false};
};

struct CreditRequest {
  IdempotencyKey key;
  AccountId account;
  Money amount;
  JournalReference reference;
};

struct DebitRequest {
  IdempotencyKey key;
  AccountId account;
  Money amount;
  JournalReference reference;
};

struct TransferRequest {
  IdempotencyKey key;
  AccountId source;
  AccountId destination;
  Money amount;
  JournalReference reference;
};

struct ReverseRequest {
  IdempotencyKey key;
  TransactionId original;
  JournalReference reference;
};

// Outcome of a financial request. A fresh outcome is POSTED or REJECTED; a replayed
// outcome carries the transaction's current status, which may already be REVERSED.
// `replayed` is true when the result came from the idempotency store.
struct TransactionReceipt {
  // cppcheck-suppress uninitMemberVarNoCtor ; Transaction has no default state by design
  Transaction transaction;
  bool replayed{false};

  [[nodiscard]] bool posted() const noexcept {
    return transaction.status == TransactionStatus::Posted;
  }
};

// Canonical textual form of a request, stored with the idempotency record.
[[nodiscard]] std::string request_signature(const CreditRequest& request);
[[nodiscard]] std::string request_signature(const DebitRequest& request);
[[nodiscard]] std::string request_signature(const TransferRequest& request);
[[nodiscard]] std::string request_signature(const ReverseRequest& request);

}  // namespace corebank
