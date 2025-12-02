// Explicit error model: domain outcomes are values, never exceptions.
#pragma once

#include <string>
#include <string_view>

namespace corebank {

enum class ErrorCode {
  // Validation
  InvalidAmount,
  InvalidCurrency,
  CurrencyMismatch,
  InvalidIdentifier,
  InvalidReference,
  ValidationError,
  // Domain outcomes
  AccountNotFound,
  AccountExists,
  AccountInactive,
  InsufficientFunds,
  SameAccountTransfer,
  TransactionNotFound,
  InvalidTransition,
  InvalidReversal,
  DuplicateRequest,
  IdempotencyConflict,
  Overflow,
  // Batch
  BatchNotFound,
  BatchStageFailed,
  BatchAlreadyRunning,
  ReconciliationFailed,
  // Infrastructure
  PersistenceError,
  ConfigurationError,
  QueueFull,
  ShuttingDown,
  Timeout,
  // Messaging
  MalformedMessage,
  MissingField,
  UnknownMessageType,
  PayloadTooLarge,
  TooManyConnections,
  NetworkError,
  Internal,
};

[[nodiscard]] std::string_view to_string(ErrorCode code) noexcept;

struct Error {
  ErrorCode code{ErrorCode::Internal};
  std::string message;

  [[nodiscard]] bool operator==(const Error& other) const noexcept = default;
};

[[nodiscard]] Error make_error(ErrorCode code, std::string message);

}  // namespace corebank
