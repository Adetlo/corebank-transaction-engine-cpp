#include "corebank/batch/eod_service.hpp"

#include "corebank/domain/money.hpp"
#include "corebank/infrastructure/json_writer.hpp"
#include "corebank/infrastructure/logger.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace corebank {
namespace {

Result<void> write_file_0640(const std::filesystem::path& path, const std::string& content) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  if (ec) {
    return make_error(
        ErrorCode::BatchStageFailed,
        "cannot create batch directory '" + path.parent_path().string() + "': " + ec.message());
  }
  {
    std::ofstream file{path, std::ios::trunc};
    if (!file.is_open()) {
      return make_error(ErrorCode::BatchStageFailed, "cannot write '" + path.string() + "'");
    }
    file << content;
  }
  // Batch output is operational data: owner read/write, group read, nothing for others.
  std::filesystem::permissions(path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write |
                                   std::filesystem::perms::group_read,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    return make_error(ErrorCode::BatchStageFailed,
                      "cannot set permissions on '" + path.string() + "': " + ec.message());
  }
  return ok();
}

std::string csv_escape(const std::string& text) {
  if (text.find_first_of(",\"\n") == std::string::npos) {
    return text;
  }
  std::string out = "\"";
  for (const char ch : text) {
    if (ch == '"') {
      out += '"';
    }
    out += ch;
  }
  out += '"';
  return out;
}

}  // namespace

EodService::EodService(Repository& repository, const Clock& clock, IdGenerator& ids,
                       EodOptions options)
    : repository_(repository),
      clock_(clock),
      ids_(ids),
      options_(std::move(options)),
      reconciliation_(repository, clock) {}

Result<void> EodService::audit(UnitOfWork& uow, const BatchRun& run, AuditEventType type,
                               std::string detail) {
  AuditEvent event{.id = 0,
                   .occurred_at = clock_.now(),
                   .type = type,
                   .entity_type = "batch",
                   .entity_id = run.id.str(),
                   .detail = std::move(detail),
                   .batch_id = run.id};
  return uow.append_audit(event);
}

Result<void> EodService::persist_run(UnitOfWork& uow, const BatchRun& run) {
  if (auto updated = uow.update_batch(run); !updated) {
    return updated.error();
  }
  for (const auto& stage : run.stages) {
    if (auto upserted = uow.upsert_stage(stage); !upserted) {
      return upserted.error();
    }
  }
  return ok();
}

Result<BatchRun> EodService::open_or_resume(const BusinessDate& date, bool& resumed) {
  auto uow_result = repository_.begin(TransactionMode::ReadWrite);
  if (!uow_result) {
    return uow_result.error();
  }
  auto& uow = *uow_result.value();
  auto latest = uow.latest_batch_for_date(date);
  if (!latest) {
    return latest.error();
  }
  const auto now = clock_.now();
  if (latest.value() && latest.value()->status != BatchStatus::Completed) {
    // Resume: keep COMPLETED stages, reset RUNNING/FAILED ones to PENDING.
    BatchRun run = *latest.value();
    run.status = BatchStatus::Running;
    run.attempt += 1;
    run.failure_reason.clear();
    run.updated_at = now;
    int reset = 0;
    for (auto& stage : run.stages) {
      if (stage.status == StageStatus::Running || stage.status == StageStatus::Failed) {
        stage.status = StageStatus::Pending;
        ++reset;
      }
    }
    if (auto persisted = persist_run(uow, run); !persisted) {
      return persisted.error();
    }
    JsonWriter detail;
    detail.number("attempt", run.attempt).number("stages_reset", reset);
    if (auto audited = audit(uow, run, AuditEventType::BatchResumed, detail.str()); !audited) {
      return audited.error();
    }
    if (auto committed = uow.commit(); !committed) {
      return committed.error();
    }
    resumed = true;
    return run;
  }

  BatchRun run{.id = ids_.next_batch_id(date.str()),
               .business_date = date,
               .status = BatchStatus::Running,
               .attempt = 1,
               .current_stage = std::nullopt,
               .cutoff_at = std::nullopt,
               .started_at = now,
               .updated_at = now,
               .completed_at = std::nullopt,
               .failure_reason = {},
               .stages = {}};
  for (const auto stage : kBatchStages) {
    run.stages.push_back(BatchStageRecord{.batch_id = run.id,
                                          .stage = stage,
                                          .status = StageStatus::Pending,
                                          .attempts = 0,
                                          .started_at = std::nullopt,
                                          .completed_at = std::nullopt,
                                          .detail = {}});
  }
  if (auto inserted = uow.insert_batch(run); !inserted) {
    return inserted.error();
  }
  JsonWriter detail;
  detail.string("business_date", date.str());
  if (auto audited = audit(uow, run, AuditEventType::BatchStarted, detail.str()); !audited) {
    return audited.error();
  }
  if (auto committed = uow.commit(); !committed) {
    return committed.error();
  }
  resumed = false;
  return run;
}

Result<EodOutcome> EodService::run(const BusinessDate& date) {
  {
    auto uow_result = repository_.begin(TransactionMode::ReadOnly);
    if (!uow_result) {
      return uow_result.error();
    }
    auto latest = uow_result.value()->latest_batch_for_date(date);
    if (!latest) {
      return latest.error();
    }
    if (latest.value() && latest.value()->status == BatchStatus::Completed) {
      Logger::instance().info("eod.already_completed", {{"business_date", date.str()},
                                                        {"batch_id", latest.value()->id.str()}});
      return EodOutcome{.run = *latest.value(), .already_completed = true, .resumed = false};
    }
  }

  bool resumed = false;
  auto opened = open_or_resume(date, resumed);
  if (!opened) {
    return opened.error();
  }
  BatchRun run = opened.value();
  Logger::instance().info(
      resumed ? "eod.resumed" : "eod.started",
      {{"batch_id", run.id.str()}, {"business_date", date.str()}, {"attempt", run.attempt}});

  for (const auto stage : kBatchStages) {
    const auto* record = run.stage(stage);
    if (record != nullptr && record->status == StageStatus::Completed) {
      Logger::instance().info("eod.stage_skipped",
                              {{"batch_id", run.id.str()}, {"stage", to_string(stage)}});
      continue;
    }
    if (auto outcome = run_stage(run, stage); !outcome) {
      return outcome.error();
    }
    if (run.status == BatchStatus::Failed) {
      return make_error(ErrorCode::BatchStageFailed, "batch " + run.id.str() + " failed at " +
                                                         std::string{to_string(stage)} + ": " +
                                                         run.failure_reason);
    }
  }
  return EodOutcome{.run = run, .already_completed = false, .resumed = resumed};
}

Result<void> EodService::mark_stage(BatchRun& run, BatchStage stage, StageStatus status,
                                    std::string detail) {
  auto it = std::find_if(run.stages.begin(), run.stages.end(),
                         [stage](const BatchStageRecord& r) { return r.stage == stage; });
  if (it == run.stages.end()) {
    return make_error(ErrorCode::Internal, "stage record missing");
  }
  const auto now = clock_.now();
  it->status = status;
  it->detail = std::move(detail);
  if (status == StageStatus::Running) {
    it->attempts += 1;
    it->started_at = now;
    it->completed_at = std::nullopt;
  } else {
    it->completed_at = now;
  }
  run.updated_at = now;
  return ok();
}

Result<void> EodService::run_stage(BatchRun& run, BatchStage stage) {
  const auto started = std::chrono::steady_clock::now();
  // 1. Record that the stage is RUNNING in its own committed transaction, so that a crash
  //    mid-stage leaves evidence and the stage is re-run (not skipped) on restart.
  {
    auto uow_result = repository_.begin(TransactionMode::ReadWrite);
    if (!uow_result) {
      return uow_result.error();
    }
    auto& uow = *uow_result.value();
    run.current_stage = stage;
    if (auto marked = mark_stage(run, stage, StageStatus::Running, ""); !marked) {
      return marked.error();
    }
    if (auto persisted = persist_run(uow, run); !persisted) {
      return persisted.error();
    }
    JsonWriter detail;
    detail.string("stage", to_string(stage)).number("attempt", run.stage(stage)->attempts);
    if (auto audited = audit(uow, run, AuditEventType::BatchStageStarted, detail.str()); !audited) {
      return audited.error();
    }
    if (auto committed = uow.commit(); !committed) {
      return committed.error();
    }
  }

  // 2. Do the work and record the outcome atomically with the stage status.
  auto uow_result = repository_.begin(TransactionMode::ReadWrite);
  if (!uow_result) {
    return uow_result.error();
  }
  std::unique_ptr<UnitOfWork> uow_ptr = std::move(uow_result.value());
  auto outcome = execute_stage(*uow_ptr, run, stage);
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

  if (!outcome) {
    // Work-level failure: roll back the stage's writes (releasing the connection),
    // then durably record FAILED in a fresh transaction.
    if (auto rolled = uow_ptr->rollback(); !rolled) {
      return rolled.error();
    }
    uow_ptr.reset();
    auto fail_uow = repository_.begin(TransactionMode::ReadWrite);
    if (!fail_uow) {
      return fail_uow.error();
    }
    run.status = BatchStatus::Failed;
    run.failure_reason = std::string{to_string(stage)} + ": " + outcome.error().message;
    if (auto marked = mark_stage(run, stage, StageStatus::Failed, outcome.error().message);
        !marked) {
      return marked.error();
    }
    if (auto persisted = persist_run(*fail_uow.value(), run); !persisted) {
      return persisted.error();
    }
    JsonWriter detail;
    detail.string("stage", to_string(stage))
        .string("code", to_string(outcome.error().code))
        .string("reason", outcome.error().message);
    if (auto audited =
            audit(*fail_uow.value(), run, AuditEventType::BatchStageFailed, detail.str());
        !audited) {
      return audited.error();
    }
    if (auto audited = audit(*fail_uow.value(), run, AuditEventType::BatchFailed, detail.str());
        !audited) {
      return audited.error();
    }
    if (auto committed = fail_uow.value()->commit(); !committed) {
      return committed.error();
    }
    Logger::instance().error("eod.stage_failed", {{"batch_id", run.id.str()},
                                                  {"stage", to_string(stage)},
                                                  {"reason", outcome.error().message},
                                                  {"duration_ms", ms}});
    return ok();
  }

  auto& uow = *uow_ptr;
  if (auto marked = mark_stage(run, stage, StageStatus::Completed, outcome.value()); !marked) {
    return marked.error();
  }
  if (stage == BatchStage::Complete) {
    run.status = BatchStatus::Completed;
    run.completed_at = clock_.now();
    run.current_stage = std::nullopt;
  }
  if (auto persisted = persist_run(uow, run); !persisted) {
    return persisted.error();
  }
  JsonWriter detail;
  detail.string("stage", to_string(stage))
      .string("detail", outcome.value())
      .number("duration_ms", ms);
  if (auto audited = audit(uow, run, AuditEventType::BatchStageCompleted, detail.str()); !audited) {
    return audited.error();
  }
  if (stage == BatchStage::Complete) {
    JsonWriter done;
    done.number("attempt", run.attempt).string("business_date", run.business_date.str());
    if (auto audited = audit(uow, run, AuditEventType::BatchCompleted, done.str()); !audited) {
      return audited.error();
    }
  }
  if (auto committed = uow.commit(); !committed) {
    return committed.error();
  }
  Logger::instance().info("eod.stage_completed", {{"batch_id", run.id.str()},
                                                  {"stage", to_string(stage)},
                                                  {"detail", outcome.value()},
                                                  {"duration_ms", ms}});
  return ok();
}

Result<std::string> EodService::execute_stage(UnitOfWork& uow, BatchRun& run, BatchStage stage) {
  if (options_.fail_stage && *options_.fail_stage == to_string(stage)) {
    return make_error(ErrorCode::BatchStageFailed,
                      "injected failure (COREBANK_FAIL_STAGE=" + *options_.fail_stage + ")");
  }
  switch (stage) {
    case BatchStage::Precheck:
      return stage_precheck(uow, run);
    case BatchStage::TransactionCutoff:
      return stage_cutoff(uow, run);
    case BatchStage::ValidatePending:
      return stage_validate_pending(uow, run);
    case BatchStage::BalanceCheck:
      return stage_balance_check(uow, run);
    case BatchStage::Reconciliation:
      return stage_reconciliation(uow, run);
    case BatchStage::DailySummary:
      return stage_daily_summary(uow, run);
    case BatchStage::Archive:
      return stage_archive(uow, run);
    case BatchStage::Complete:
      return std::string{"batch completed"};
  }
  return make_error(ErrorCode::Internal, "unknown stage");
}

Result<std::string> EodService::stage_precheck(UnitOfWork& uow, const BatchRun& run) const {
  auto integrity = uow.integrity_check();
  if (!integrity) {
    return integrity.error();
  }
  if (integrity.value() != "ok") {
    return make_error(ErrorCode::BatchStageFailed, "storage integrity: " + integrity.value());
  }
  auto latest = uow.latest_batch();
  if (!latest) {
    return latest.error();
  }
  if (latest.value() && latest.value()->id != run.id &&
      latest.value()->status == BatchStatus::Running) {
    return make_error(ErrorCode::BatchAlreadyRunning,
                      "batch " + latest.value()->id.str() + " is still RUNNING");
  }
  std::error_code ec;
  std::filesystem::create_directories(options_.batch_dir, ec);
  if (ec) {
    return make_error(ErrorCode::BatchStageFailed,
                      "batch directory '" + options_.batch_dir + "' unavailable: " + ec.message());
  }
  return std::string{"storage ok, no other batch running, batch directory writable"};
}

Result<std::string> EodService::stage_cutoff(UnitOfWork& uow, BatchRun& run) {
  run.cutoff_at = clock_.now();
  TransactionQuery query;
  query.business_date = run.business_date;
  query.limit = 1'000'000;
  auto txns = uow.list_transactions(query);
  if (!txns) {
    return txns.error();
  }
  return "cutoff at " + format_timestamp(*run.cutoff_at) + ", " +
         std::to_string(txns.value().size()) + " transactions dated " + run.business_date.str();
}

Result<std::string> EodService::stage_validate_pending(UnitOfWork& uow, const BatchRun& run) {
  TransactionQuery query;
  query.pending_only = true;
  query.created_before =
      (run.cutoff_at ? *run.cutoff_at : clock_.now()) -
      std::chrono::duration_cast<std::chrono::microseconds>(options_.stale_pending_after);
  query.limit = 100'000;
  auto pending = uow.list_transactions(query);
  if (!pending) {
    return pending.error();
  }
  int rejected = 0;
  for (auto txn : pending.value()) {
    auto reason = make_error(ErrorCode::Timeout, "stale at end-of-day cutoff");
    if (auto done = txn.reject(reason, clock_.now()); !done) {
      return done.error();
    }
    if (auto updated = uow.update_transaction(txn); !updated) {
      return updated.error();
    }
    JsonWriter detail;
    detail.string("code", "TIMEOUT")
        .string("reason", reason.message)
        .string("batch_id", run.id.str());
    AuditEvent event{.id = 0,
                     .occurred_at = clock_.now(),
                     .type = AuditEventType::TransactionRejected,
                     .entity_type = "transaction",
                     .entity_id = txn.id.str(),
                     .detail = detail.str(),
                     .batch_id = run.id};
    if (auto audited = uow.append_audit(event); !audited) {
      return audited.error();
    }
    ++rejected;
  }
  return std::to_string(rejected) + " stale pending transactions rejected";
}

Result<std::string> EodService::stage_balance_check(UnitOfWork& uow, const BatchRun& /*run*/) {
  auto accounts = uow.list_accounts();
  if (!accounts) {
    return accounts.error();
  }
  auto totals = uow.posting_totals_by_account();
  if (!totals) {
    return totals.error();
  }
  int mismatches = 0;
  int overdrawn = 0;
  for (const auto& account : accounts.value()) {
    std::int64_t derived = 0;
    for (const auto& total : totals.value()) {
      if (total.account_id == account.id) {
        derived = total.credit_minor - total.debit_minor;
      }
    }
    if (derived != account.balance.minor()) {
      ++mismatches;
    }
    if (!account.allow_overdraft && account.balance.is_negative()) {
      ++overdrawn;
    }
  }
  if (mismatches != 0 || overdrawn != 0) {
    return make_error(ErrorCode::BatchStageFailed,
                      std::to_string(mismatches) + " balance mismatches, " +
                          std::to_string(overdrawn) + " overdraft breaches");
  }
  return std::to_string(accounts.value().size()) + " account balances verified against postings";
}

Result<std::string> EodService::stage_reconciliation(UnitOfWork& uow, BatchRun& run) {
  auto result = reconciliation_.run_in(uow, run.business_date);
  if (!result) {
    return result.error();
  }
  JsonWriter detail;
  detail.boolean("passed", result.value().passed)
      .number("checks", static_cast<std::int64_t>(result.value().checks.size()))
      .number("failed", static_cast<std::int64_t>(result.value().failed_count()));
  if (auto audited = audit(uow, run, AuditEventType::ReconciliationRun, detail.str()); !audited) {
    return audited.error();
  }
  if (!result.value().passed) {
    std::string failed;
    for (const auto& check : result.value().checks) {
      if (!check.passed) {
        failed += (failed.empty() ? "" : "; ") + check.name + ": " + check.detail;
      }
    }
    return make_error(ErrorCode::ReconciliationFailed, "reconciliation failed: " + failed);
  }
  return "reconciliation " + result.value().summary();
}

Result<std::string> EodService::stage_daily_summary(UnitOfWork& uow, BatchRun& run) {
  auto totals = uow.currency_totals(run.business_date);
  if (!totals) {
    return totals.error();
  }
  JsonArrayWriter rows;
  for (const auto& total : totals.value()) {
    const DailySummary summary{.business_date = run.business_date,
                               .currency = total.currency,
                               .posted_count = total.posted_count,
                               .rejected_count = total.rejected_count,
                               .reversed_count = total.reversed_count,
                               .credit_total_minor = total.credit_total_minor,
                               .debit_total_minor = total.debit_total_minor,
                               .transfer_total_minor = total.transfer_total_minor,
                               .generated_at = clock_.now(),
                               .batch_id = run.id};
    if (auto stored = uow.upsert_daily_summary(summary); !stored) {
      return stored.error();
    }
    JsonWriter row;
    row.string("currency", total.currency)
        .number("posted_count", total.posted_count)
        .number("rejected_count", total.rejected_count)
        .number("reversed_count", total.reversed_count)
        .number("credit_total_minor", total.credit_total_minor)
        .number("debit_total_minor", total.debit_total_minor)
        .number("transfer_total_minor", total.transfer_total_minor);
    rows.raw(row.str());
  }
  JsonWriter document;
  document.string("business_date", run.business_date.str())
      .string("batch_id", run.id.str())
      .string("generated_at", format_timestamp(clock_.now()))
      .raw("currencies", rows.str());
  const auto path =
      std::filesystem::path{options_.batch_dir} / run.business_date.str() / "daily_summary.json";
  if (auto written = write_file_0640(path, document.str() + "\n"); !written) {
    return written.error();
  }
  return std::to_string(totals.value().size()) + " currency summaries written to " + path.string();
}

Result<std::string> EodService::stage_archive(UnitOfWork& uow, const BatchRun& run) const {
  TransactionQuery query;
  query.business_date = run.business_date;
  query.limit = 10'000'000;
  auto txns = uow.list_transactions(query);
  if (!txns) {
    return txns.error();
  }
  std::string csv =
      "transaction_id,type,status,currency,amount,source_account_id,destination_account_id,"
      "reference,reversal_of,rejection_code,created_at\n";
  for (const auto& txn : txns.value()) {
    csv += txn.id.str() + ',' + std::string{to_string(txn.type)} + ',' +
           std::string{to_string(txn.status)} + ',' + txn.amount.currency().str() + ',' +
           txn.amount.format_amount() + ',' + (txn.source ? txn.source->str() : "") + ',' +
           (txn.destination ? txn.destination->str() : "") + ',' + csv_escape(txn.reference.str()) +
           ',' + (txn.reversal_of ? txn.reversal_of->str() : "") + ',' +
           (txn.rejection ? std::string{to_string(txn.rejection->code)} : "") + ',' +
           format_timestamp(txn.created_at) + '\n';
  }
  const auto path =
      std::filesystem::path{options_.batch_dir} / run.business_date.str() / "transactions.csv";
  if (auto written = write_file_0640(path, csv); !written) {
    return written.error();
  }
  // Archive is an export: nothing is deleted from the database, ever.
  return std::to_string(txns.value().size()) + " transactions archived to " + path.string();
}

Result<std::optional<BatchRun>> EodService::status(const std::optional<BusinessDate>& date) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  if (date) {
    return uow_result.value()->latest_batch_for_date(*date);
  }
  return uow_result.value()->latest_batch();
}

Result<std::vector<BatchRun>> EodService::history(std::size_t limit) {
  auto uow_result = repository_.begin(TransactionMode::ReadOnly);
  if (!uow_result) {
    return uow_result.error();
  }
  return uow_result.value()->list_batches(limit);
}

}  // namespace corebank
