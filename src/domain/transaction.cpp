#include "corebank/domain/transaction.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace corebank {

std::string_view to_string(TransactionType type) noexcept {
  switch (type) {
    case TransactionType::Credit:
      return "CREDIT";
    case TransactionType::Debit:
      return "DEBIT";
    case TransactionType::Transfer:
      return "TRANSFER";
    case TransactionType::Reversal:
      return "REVERSAL";
  }
  return "UNKNOWN";
}

std::string_view to_string(TransactionStatus status) noexcept {
  switch (status) {
    case TransactionStatus::Received:
      return "RECEIVED";
    case TransactionStatus::Validated:
      return "VALIDATED";
    case TransactionStatus::Processing:
      return "PROCESSING";
    case TransactionStatus::Posted:
      return "POSTED";
    case TransactionStatus::Rejected:
      return "REJECTED";
    case TransactionStatus::Reversed:
      return "REVERSED";
  }
  return "UNKNOWN";
}

Result<TransactionType> parse_transaction_type(std::string_view text) {
  for (const auto type : {TransactionType::Credit, TransactionType::Debit,
                          TransactionType::Transfer, TransactionType::Reversal}) {
    if (to_string(type) == text) {
      return type;
    }
  }
  return make_error(ErrorCode::ValidationError,
                    "unknown transaction type '" + std::string{text} + "'");
}

Result<TransactionStatus> parse_transaction_status(std::string_view text) {
  for (const auto status :
       {TransactionStatus::Received, TransactionStatus::Validated, TransactionStatus::Processing,
        TransactionStatus::Posted, TransactionStatus::Rejected, TransactionStatus::Reversed}) {
    if (to_string(status) == text) {
      return status;
    }
  }
  return make_error(ErrorCode::ValidationError,
                    "unknown transaction status '" + std::string{text} + "'");
}

bool can_transition(TransactionStatus from, TransactionStatus to) noexcept {
  switch (from) {
    case TransactionStatus::Received:
      return to == TransactionStatus::Validated || to == TransactionStatus::Rejected;
    case TransactionStatus::Validated:
      return to == TransactionStatus::Processing || to == TransactionStatus::Rejected;
    case TransactionStatus::Processing:
      return to == TransactionStatus::Posted || to == TransactionStatus::Rejected;
    case TransactionStatus::Posted:
      return to == TransactionStatus::Reversed;
    case TransactionStatus::Rejected:
    case TransactionStatus::Reversed:
      return false;
  }
  return false;
}

bool is_terminal(TransactionStatus status) noexcept {
  return status == TransactionStatus::Rejected || status == TransactionStatus::Reversed;
}

Result<void> Transaction::transition_to(TransactionStatus next, Timestamp at) {
  if (!can_transition(status, next)) {
    return make_error(ErrorCode::InvalidTransition,
                      "transaction " + id.str() + " cannot move from " +
                          std::string{to_string(status)} + " to " + std::string{to_string(next)});
  }
  status = next;
  updated_at = at;
  return ok();
}

Result<void> Transaction::reject(Error reason, Timestamp at) {
  if (auto moved = transition_to(TransactionStatus::Rejected, at); !moved) {
    return moved.error();
  }
  rejection = std::move(reason);
  return ok();
}

}  // namespace corebank
