// End-of-day batch run and its stages.
#pragma once

#include "corebank/domain/identifiers.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corebank {

enum class BatchStage {
  Precheck,
  TransactionCutoff,
  ValidatePending,
  BalanceCheck,
  Reconciliation,
  DailySummary,
  Archive,
  Complete,
};

inline constexpr std::array<BatchStage, 8> kBatchStages{
    BatchStage::Precheck,     BatchStage::TransactionCutoff, BatchStage::ValidatePending,
    BatchStage::BalanceCheck, BatchStage::Reconciliation,    BatchStage::DailySummary,
    BatchStage::Archive,      BatchStage::Complete,
};

enum class BatchStatus { Running, Failed, Completed };
enum class StageStatus { Pending, Running, Completed, Failed };

[[nodiscard]] std::string_view to_string(BatchStage stage) noexcept;
[[nodiscard]] std::string_view to_string(BatchStatus status) noexcept;
[[nodiscard]] std::string_view to_string(StageStatus status) noexcept;
[[nodiscard]] Result<BatchStage> parse_batch_stage(std::string_view text);
[[nodiscard]] Result<BatchStatus> parse_batch_status(std::string_view text);
[[nodiscard]] Result<StageStatus> parse_stage_status(std::string_view text);

struct BatchStageRecord {
  BatchId batch_id;
  BatchStage stage{BatchStage::Precheck};
  StageStatus status{StageStatus::Pending};
  int attempts{0};
  std::optional<Timestamp> started_at;
  std::optional<Timestamp> completed_at;
  std::string detail;
};

struct BatchRun {
  BatchId id;
  BusinessDate business_date;
  BatchStatus status{BatchStatus::Running};
  int attempt{1};
  std::optional<BatchStage> current_stage;
  std::optional<Timestamp> cutoff_at;
  Timestamp started_at;
  Timestamp updated_at;
  std::optional<Timestamp> completed_at;
  std::string failure_reason;
  std::vector<BatchStageRecord> stages;

  [[nodiscard]] const BatchStageRecord* stage(BatchStage wanted) const noexcept;
};

struct DailySummary {
  BusinessDate business_date;
  std::string currency;
  std::int64_t posted_count{0};
  std::int64_t rejected_count{0};
  std::int64_t reversed_count{0};
  std::int64_t credit_total_minor{0};
  std::int64_t debit_total_minor{0};
  std::int64_t transfer_total_minor{0};
  Timestamp generated_at;
  BatchId batch_id;
};

}  // namespace corebank
