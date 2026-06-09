#include "corebank/domain/batch.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace corebank {

std::string_view to_string(BatchStage stage) noexcept {
  switch (stage) {
    case BatchStage::Precheck:
      return "PRECHECK";
    case BatchStage::TransactionCutoff:
      return "TRANSACTION_CUTOFF";
    case BatchStage::ValidatePending:
      return "VALIDATE_PENDING";
    case BatchStage::BalanceCheck:
      return "BALANCE_CHECK";
    case BatchStage::Reconciliation:
      return "RECONCILIATION";
    case BatchStage::DailySummary:
      return "DAILY_SUMMARY";
    case BatchStage::Archive:
      return "ARCHIVE";
    case BatchStage::Complete:
      return "COMPLETE";
  }
  return "UNKNOWN";
}

std::string_view to_string(BatchStatus status) noexcept {
  switch (status) {
    case BatchStatus::Running:
      return "RUNNING";
    case BatchStatus::Failed:
      return "FAILED";
    case BatchStatus::Completed:
      return "COMPLETED";
  }
  return "UNKNOWN";
}

std::string_view to_string(StageStatus status) noexcept {
  switch (status) {
    case StageStatus::Pending:
      return "PENDING";
    case StageStatus::Running:
      return "RUNNING";
    case StageStatus::Completed:
      return "COMPLETED";
    case StageStatus::Failed:
      return "FAILED";
  }
  return "UNKNOWN";
}

Result<BatchStage> parse_batch_stage(std::string_view text) {
  for (const auto stage : kBatchStages) {
    if (to_string(stage) == text) {
      return stage;
    }
  }
  return make_error(ErrorCode::ValidationError, "unknown batch stage '" + std::string{text} + "'");
}

Result<BatchStatus> parse_batch_status(std::string_view text) {
  for (const auto status : {BatchStatus::Running, BatchStatus::Failed, BatchStatus::Completed}) {
    if (to_string(status) == text) {
      return status;
    }
  }
  return make_error(ErrorCode::ValidationError, "unknown batch status '" + std::string{text} + "'");
}

Result<StageStatus> parse_stage_status(std::string_view text) {
  for (const auto status :
       {StageStatus::Pending, StageStatus::Running, StageStatus::Completed, StageStatus::Failed}) {
    if (to_string(status) == text) {
      return status;
    }
  }
  return make_error(ErrorCode::ValidationError, "unknown stage status '" + std::string{text} + "'");
}

const BatchStageRecord* BatchRun::stage(BatchStage wanted) const noexcept {
  const auto it = std::find_if(stages.begin(), stages.end(), [wanted](const BatchStageRecord& rec) {
    return rec.stage == wanted;
  });
  return it == stages.end() ? nullptr : &*it;
}

}  // namespace corebank
