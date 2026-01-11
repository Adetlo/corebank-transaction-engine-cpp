// Append-only audit trail entries.
#pragma once

#include "corebank/domain/identifiers.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace corebank {

enum class AuditEventType {
  AccountCreated,
  TransactionReceived,
  TransactionPosted,
  TransactionRejected,
  TransactionReversed,
  ReconciliationRun,
  BatchStarted,
  BatchResumed,
  BatchStageStarted,
  BatchStageCompleted,
  BatchStageFailed,
  BatchCompleted,
  BatchFailed,
  EngineStarted,
  EngineStopped,
};

[[nodiscard]] std::string_view to_string(AuditEventType type) noexcept;
[[nodiscard]] Result<AuditEventType> parse_audit_event_type(std::string_view text);

struct AuditEvent {
  std::int64_t id{0};
  Timestamp occurred_at;
  AuditEventType type{AuditEventType::AccountCreated};
  std::string entity_type;  // "account", "transaction", "batch", "engine"
  std::string entity_id;
  std::string detail;  // compact JSON object
  std::optional<BatchId> batch_id;
};

}  // namespace corebank
