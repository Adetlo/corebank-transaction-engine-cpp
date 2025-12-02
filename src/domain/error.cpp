#include "corebank/domain/error.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace corebank {

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::InvalidAmount:
      return "INVALID_AMOUNT";
    case ErrorCode::InvalidCurrency:
      return "INVALID_CURRENCY";
    case ErrorCode::CurrencyMismatch:
      return "CURRENCY_MISMATCH";
    case ErrorCode::InvalidIdentifier:
      return "INVALID_IDENTIFIER";
    case ErrorCode::InvalidReference:
      return "INVALID_REFERENCE";
    case ErrorCode::ValidationError:
      return "VALIDATION_ERROR";
    case ErrorCode::AccountNotFound:
      return "ACCOUNT_NOT_FOUND";
    case ErrorCode::AccountExists:
      return "ACCOUNT_EXISTS";
    case ErrorCode::AccountInactive:
      return "ACCOUNT_INACTIVE";
    case ErrorCode::InsufficientFunds:
      return "INSUFFICIENT_FUNDS";
    case ErrorCode::SameAccountTransfer:
      return "SAME_ACCOUNT_TRANSFER";
    case ErrorCode::TransactionNotFound:
      return "TRANSACTION_NOT_FOUND";
    case ErrorCode::InvalidTransition:
      return "INVALID_TRANSITION";
    case ErrorCode::InvalidReversal:
      return "INVALID_REVERSAL";
    case ErrorCode::DuplicateRequest:
      return "DUPLICATE_REQUEST";
    case ErrorCode::IdempotencyConflict:
      return "IDEMPOTENCY_CONFLICT";
    case ErrorCode::Overflow:
      return "OVERFLOW";
    case ErrorCode::BatchNotFound:
      return "BATCH_NOT_FOUND";
    case ErrorCode::BatchStageFailed:
      return "BATCH_STAGE_FAILED";
    case ErrorCode::BatchAlreadyRunning:
      return "BATCH_ALREADY_RUNNING";
    case ErrorCode::ReconciliationFailed:
      return "RECONCILIATION_FAILED";
    case ErrorCode::PersistenceError:
      return "PERSISTENCE_ERROR";
    case ErrorCode::ConfigurationError:
      return "CONFIGURATION_ERROR";
    case ErrorCode::QueueFull:
      return "QUEUE_FULL";
    case ErrorCode::ShuttingDown:
      return "SHUTTING_DOWN";
    case ErrorCode::Timeout:
      return "TIMEOUT";
    case ErrorCode::MalformedMessage:
      return "MALFORMED_MESSAGE";
    case ErrorCode::MissingField:
      return "MISSING_FIELD";
    case ErrorCode::UnknownMessageType:
      return "UNKNOWN_MESSAGE_TYPE";
    case ErrorCode::PayloadTooLarge:
      return "PAYLOAD_TOO_LARGE";
    case ErrorCode::TooManyConnections:
      return "TOO_MANY_CONNECTIONS";
    case ErrorCode::NetworkError:
      return "NETWORK_ERROR";
    case ErrorCode::Internal:
      return "INTERNAL_ERROR";
  }
  return "UNKNOWN_ERROR";
}

Error make_error(ErrorCode code, std::string message) {
  return Error{code, std::move(message)};
}

}  // namespace corebank
