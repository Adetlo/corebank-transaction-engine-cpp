#include "corebank/domain/audit.hpp"

#include <array>
#include <string>
#include <string_view>

namespace corebank {
namespace {

constexpr std::array<AuditEventType, 15> kAllTypes{
    AuditEventType::AccountCreated,      AuditEventType::TransactionReceived,
    AuditEventType::TransactionPosted,   AuditEventType::TransactionRejected,
    AuditEventType::TransactionReversed, AuditEventType::ReconciliationRun,
    AuditEventType::BatchStarted,        AuditEventType::BatchResumed,
    AuditEventType::BatchStageStarted,   AuditEventType::BatchStageCompleted,
    AuditEventType::BatchStageFailed,    AuditEventType::BatchCompleted,
    AuditEventType::BatchFailed,         AuditEventType::EngineStarted,
    AuditEventType::EngineStopped,
};

}  // namespace

std::string_view to_string(AuditEventType type) noexcept {
  switch (type) {
    case AuditEventType::AccountCreated:
      return "ACCOUNT_CREATED";
    case AuditEventType::TransactionReceived:
      return "TRANSACTION_RECEIVED";
    case AuditEventType::TransactionPosted:
      return "TRANSACTION_POSTED";
    case AuditEventType::TransactionRejected:
      return "TRANSACTION_REJECTED";
    case AuditEventType::TransactionReversed:
      return "TRANSACTION_REVERSED";
    case AuditEventType::ReconciliationRun:
      return "RECONCILIATION_RUN";
    case AuditEventType::BatchStarted:
      return "BATCH_STARTED";
    case AuditEventType::BatchResumed:
      return "BATCH_RESUMED";
    case AuditEventType::BatchStageStarted:
      return "BATCH_STAGE_STARTED";
    case AuditEventType::BatchStageCompleted:
      return "BATCH_STAGE_COMPLETED";
    case AuditEventType::BatchStageFailed:
      return "BATCH_STAGE_FAILED";
    case AuditEventType::BatchCompleted:
      return "BATCH_COMPLETED";
    case AuditEventType::BatchFailed:
      return "BATCH_FAILED";
    case AuditEventType::EngineStarted:
      return "ENGINE_STARTED";
    case AuditEventType::EngineStopped:
      return "ENGINE_STOPPED";
  }
  return "UNKNOWN";
}

Result<AuditEventType> parse_audit_event_type(std::string_view text) {
  for (const auto type : kAllTypes) {
    if (to_string(type) == text) {
      return type;
    }
  }
  return make_error(ErrorCode::ValidationError,
                    "unknown audit event type '" + std::string{text} + "'");
}

}  // namespace corebank
