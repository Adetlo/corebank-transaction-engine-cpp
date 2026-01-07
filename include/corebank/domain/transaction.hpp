// Transaction aggregate and its state machine.
#pragma once

#include "corebank/domain/error.hpp"
#include "corebank/domain/identifiers.hpp"
#include "corebank/domain/money.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"

#include <optional>
#include <string_view>

namespace corebank {

enum class TransactionType { Credit, Debit, Transfer, Reversal };

enum class TransactionStatus { Received, Validated, Processing, Posted, Rejected, Reversed };

[[nodiscard]] std::string_view to_string(TransactionType type) noexcept;
[[nodiscard]] std::string_view to_string(TransactionStatus status) noexcept;
[[nodiscard]] Result<TransactionType> parse_transaction_type(std::string_view text);
[[nodiscard]] Result<TransactionStatus> parse_transaction_status(std::string_view text);

// The only legal moves:
//   RECEIVED  -> VALIDATED | REJECTED
//   VALIDATED -> PROCESSING | REJECTED
//   PROCESSING-> POSTED | REJECTED
//   POSTED    -> REVERSED
//   REJECTED, REVERSED are terminal.
[[nodiscard]] bool can_transition(TransactionStatus from, TransactionStatus to) noexcept;
[[nodiscard]] bool is_terminal(TransactionStatus status) noexcept;

struct Transaction {
  TransactionId id;
  TransactionType type{TransactionType::Transfer};
  TransactionStatus status{TransactionStatus::Received};
  Money amount;
  std::optional<AccountId> source;       // debited account (DEBIT, TRANSFER)
  std::optional<AccountId> destination;  // credited account (CREDIT, TRANSFER)
  JournalReference reference;
  std::optional<IdempotencyKey> idempotency_key;
  std::optional<TransactionId> reversal_of;
  std::optional<TransactionId> reversed_by;
  std::optional<Error> rejection;
  BusinessDate business_date;
  Timestamp created_at;
  Timestamp updated_at;

  // Applies the state machine; returns InvalidTransition without mutating on failure.
  [[nodiscard]] Result<void> transition_to(TransactionStatus next, Timestamp at);
  [[nodiscard]] Result<void> reject(Error reason, Timestamp at);
};

}  // namespace corebank
