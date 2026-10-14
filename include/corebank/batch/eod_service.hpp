// Synthetic end-of-day batch: restartable, idempotent, auditable stage pipeline.
#pragma once

#include "corebank/application/id_generator.hpp"
#include "corebank/application/reconciliation_service.hpp"
#include "corebank/application/repository.hpp"
#include "corebank/domain/batch.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace corebank {

struct EodOptions {
  std::string batch_dir{"var/batch"};
  // Stage name that fails deliberately (COREBANK_FAIL_STAGE), for recovery testing.
  std::optional<std::string> fail_stage;
  // Pending transactions older than this at cutoff are rejected as stale.
  std::chrono::seconds stale_pending_after{60};
};

struct EodOutcome {
  BatchRun run;
  bool already_completed{false};  // run() found a COMPLETED batch and did nothing
  bool resumed{false};            // continued a FAILED/RUNNING batch
};

class EodService {
 public:
  EodService(Repository& repository, const Clock& clock, IdGenerator& ids, EodOptions options);

  // Runs (or resumes) the batch for `date`. Completed stages are never re-executed.
  [[nodiscard]] Result<EodOutcome> run(const BusinessDate& date);
  [[nodiscard]] Result<std::optional<BatchRun>> status(const std::optional<BusinessDate>& date);
  [[nodiscard]] Result<std::vector<BatchRun>> history(std::size_t limit);

  [[nodiscard]] const EodOptions& options() const noexcept { return options_; }

 private:
  [[nodiscard]] Result<BatchRun> open_or_resume(const BusinessDate& date, bool& resumed);
  [[nodiscard]] Result<void> run_stage(BatchRun& run, BatchStage stage);
  [[nodiscard]] Result<std::string> execute_stage(UnitOfWork& uow, BatchRun& run, BatchStage stage);
  [[nodiscard]] Result<void> mark_stage(BatchRun& run, BatchStage stage, StageStatus status,
                                        std::string detail);
  [[nodiscard]] Result<void> audit(UnitOfWork& uow, const BatchRun& run, AuditEventType type,
                                   std::string detail);
  [[nodiscard]] Result<void> persist_run(UnitOfWork& uow, const BatchRun& run);

  [[nodiscard]] Result<std::string> stage_precheck(UnitOfWork& uow, const BatchRun& run) const;
  [[nodiscard]] Result<std::string> stage_cutoff(UnitOfWork& uow, BatchRun& run);
  [[nodiscard]] Result<std::string> stage_validate_pending(UnitOfWork& uow, BatchRun& run);
  [[nodiscard]] Result<std::string> stage_balance_check(UnitOfWork& uow, const BatchRun& run);
  [[nodiscard]] Result<std::string> stage_reconciliation(UnitOfWork& uow, BatchRun& run);
  [[nodiscard]] Result<std::string> stage_daily_summary(UnitOfWork& uow, BatchRun& run);
  [[nodiscard]] Result<std::string> stage_archive(UnitOfWork& uow, BatchRun& run) const;

  Repository& repository_;
  const Clock& clock_;
  IdGenerator& ids_;
  EodOptions options_;
  ReconciliationService reconciliation_;
};

}  // namespace corebank
