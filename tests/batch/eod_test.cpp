#include "corebank/batch/eod_service.hpp"
#include "corebank/infrastructure/sqlite/connection.hpp"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class EodTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    (void)make_account("SETTLE", "GBP", true, AccountType::Settlement);
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)fund("SETTLE", 50'000);
    (void)transfer("SETTLE", "CUST-A", 10'000);
    (void)transfer("CUST-A", "CUST-B", 2'500);
    (void)transfer("CUST-B", "CUST-A", 99'999);  // rejected
    date = service->current_business_date();
  }
  EodService make(std::optional<std::string> fail_stage = std::nullopt) {
    return EodService{*repository, clock, ids,
                      EodOptions{.batch_dir = (temp.path() / "batch").string(),
                                 .fail_stage = std::move(fail_stage),
                                 .stale_pending_after = std::chrono::seconds{0}}};
  }
  std::vector<AuditEvent> batch_audit(const BatchId& id) {
    return service->audit_trail(AuditQuery{.batch_id = id, .limit = 1000}).value();
  }
  BusinessDate date = BusinessDate::parse("2000-01-01").value();
};

TEST_F(EodTest, RunsEveryStageAndProducesArtifacts) {
  auto eod = make();
  const auto outcome = eod.run(date);
  ASSERT_TRUE(outcome.has_value()) << outcome.error().message;
  const auto& run = outcome.value().run;
  EXPECT_EQ(run.status, BatchStatus::Completed);
  EXPECT_FALSE(outcome.value().resumed);
  EXPECT_FALSE(outcome.value().already_completed);
  ASSERT_EQ(run.stages.size(), kBatchStages.size());
  for (const auto& stage : run.stages) {
    EXPECT_EQ(stage.status, StageStatus::Completed) << to_string(stage.stage);
    EXPECT_EQ(stage.attempts, 1);
  }
  EXPECT_TRUE(run.cutoff_at.has_value());
  EXPECT_TRUE(run.completed_at.has_value());
  const auto dir = temp.path() / "batch" / date.str();
  EXPECT_TRUE(std::filesystem::exists(dir / "daily_summary.json"));
  EXPECT_TRUE(std::filesystem::exists(dir / "transactions.csv"));
  const auto perms = std::filesystem::status(dir / "transactions.csv").permissions();
  EXPECT_EQ(perms & std::filesystem::perms::others_all, std::filesystem::perms::none);
  std::ifstream csv{dir / "transactions.csv"};
  std::string line;
  int lines = 0;
  while (std::getline(csv, line)) {
    ++lines;
  }
  EXPECT_EQ(lines, 1 + 4);  // header + 4 transactions dated today
  auto uow = repository->begin(TransactionMode::ReadOnly).value();
  const auto summaries = uow->daily_summaries(date).value();
  ASSERT_EQ(summaries.size(), 1U);
  EXPECT_EQ(summaries[0].posted_count, 3);
  EXPECT_EQ(summaries[0].rejected_count, 1);
  EXPECT_EQ(summaries[0].transfer_total_minor, 12'500);
}

TEST_F(EodTest, SecondRunForSameDateIsIdempotent) {
  auto eod = make();
  const auto first = eod.run(date).value();
  const auto second = eod.run(date).value();
  EXPECT_TRUE(second.already_completed);
  EXPECT_EQ(second.run.id, first.run.id);
  EXPECT_EQ(eod.history(10).value().size(), 1U);
  EXPECT_EQ(batch_audit(first.run.id).size(), 1 + 2 * kBatchStages.size() + 1 + 1);
}

TEST_F(EodTest, InjectedFailureIsRecordedAndRestartSkipsCompletedStages) {
  auto failing = make(std::string{"RECONCILIATION"});
  const auto failed = failing.run(date);
  ASSERT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error().code, ErrorCode::BatchStageFailed);

  const auto status = failing.status(date).value();
  ASSERT_TRUE(status.has_value());
  EXPECT_EQ(status->status, BatchStatus::Failed);
  EXPECT_NE(status->failure_reason.find("RECONCILIATION"), std::string::npos);
  EXPECT_EQ(status->stage(BatchStage::Precheck)->status, StageStatus::Completed);
  EXPECT_EQ(status->stage(BatchStage::BalanceCheck)->status, StageStatus::Completed);
  EXPECT_EQ(status->stage(BatchStage::Reconciliation)->status, StageStatus::Failed);
  EXPECT_EQ(status->stage(BatchStage::DailySummary)->status, StageStatus::Pending);
  const auto failed_audit = batch_audit(status->id);
  int stage_failed = 0;
  int batch_failed = 0;
  for (const auto& e : failed_audit) {
    stage_failed += e.type == AuditEventType::BatchStageFailed ? 1 : 0;
    batch_failed += e.type == AuditEventType::BatchFailed ? 1 : 0;
  }
  EXPECT_EQ(stage_failed, 1);
  EXPECT_EQ(batch_failed, 1);

  // Restart without the injected failure: completed stages are not re-run.
  auto healthy = make();
  const auto resumed = healthy.run(date);
  ASSERT_TRUE(resumed.has_value()) << resumed.error().message;
  EXPECT_TRUE(resumed.value().resumed);
  EXPECT_EQ(resumed.value().run.id, status->id);
  EXPECT_EQ(resumed.value().run.status, BatchStatus::Completed);
  EXPECT_EQ(resumed.value().run.attempt, 2);
  EXPECT_EQ(resumed.value().run.stage(BatchStage::Precheck)->attempts, 1);
  EXPECT_EQ(resumed.value().run.stage(BatchStage::Reconciliation)->attempts, 2);
  EXPECT_EQ(resumed.value().run.stage(BatchStage::Archive)->attempts, 1);

  // Audit history of the failed attempt is retained alongside the resume.
  const auto full_audit = batch_audit(status->id);
  EXPECT_GT(full_audit.size(), failed_audit.size());
  int resumed_events = 0;
  for (const auto& e : full_audit) {
    resumed_events += e.type == AuditEventType::BatchResumed ? 1 : 0;
  }
  EXPECT_EQ(resumed_events, 1);
  EXPECT_EQ(full_audit.front().type, AuditEventType::BatchStarted);
}

TEST_F(EodTest, FailureInEveryStageIsRecoverable) {
  for (std::size_t i = 0; i < kBatchStages.size(); ++i) {
    const auto stage = kBatchStages[i];
    const auto day = BusinessDate::parse("2031-01-0" + std::to_string(i + 1)).value();
    auto failing = make(std::string{to_string(stage)});
    ASSERT_FALSE(failing.run(day).has_value()) << to_string(stage);
    auto healthy = make();
    const auto resumed = healthy.run(day);
    ASSERT_TRUE(resumed.has_value()) << to_string(stage) << ": " << resumed.error().message;
    EXPECT_EQ(resumed.value().run.status, BatchStatus::Completed);
    EXPECT_EQ(resumed.value().run.attempt, 2);
  }
}

TEST_F(EodTest, ReconciliationFailureStopsTheBatch) {
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  ASSERT_TRUE(
      connection
          ->exec(
              "UPDATE accounts SET balance_minor = balance_minor + 7 WHERE account_id = 'CUST-A'")
          .has_value());
  auto eod = make();
  const auto outcome = eod.run(date);
  ASSERT_FALSE(outcome.has_value());
  const auto status = eod.status(date).value();
  EXPECT_EQ(status->status, BatchStatus::Failed);
  EXPECT_EQ(status->stage(BatchStage::BalanceCheck)->status, StageStatus::Failed);
}

TEST_F(EodTest, StalePendingTransactionsAreRejectedAtCutoff) {
  // Simulate a transaction stuck in PROCESSING (e.g. crash mid-flight).
  const auto stuck = TransactionId::parse("TXN-STUCK").value();
  {
    auto uow = repository->begin(TransactionMode::ReadWrite).value();
    const auto now = clock.now() - std::chrono::minutes{5};
    ASSERT_TRUE(
        uow->insert_transaction(Transaction{.id = stuck,
                                            .type = TransactionType::Transfer,
                                            .status = TransactionStatus::Processing,
                                            .amount = gbp(5),
                                            .source = acc("CUST-A"),
                                            .destination = acc("CUST-B"),
                                            .reference = JournalReference::unchecked("stuck"),
                                            .idempotency_key = std::nullopt,
                                            .reversal_of = std::nullopt,
                                            .reversed_by = std::nullopt,
                                            .rejection = std::nullopt,
                                            .business_date = date,
                                            .created_at = now,
                                            .updated_at = now})
            .has_value());
    ASSERT_TRUE(uow->commit().has_value());
  }
  auto eod = make();
  const auto outcome = eod.run(date);
  ASSERT_TRUE(outcome.has_value()) << outcome.error().message;
  const auto stored = service->get_transaction(stuck).value();
  EXPECT_EQ(stored.status, TransactionStatus::Rejected);
  EXPECT_EQ(stored.rejection->code, ErrorCode::Timeout);
  EXPECT_NE(outcome.value().run.stage(BatchStage::ValidatePending)->detail.find("1 stale"),
            std::string::npos);
}

TEST_F(EodTest, StatusAndHistoryWithoutRuns) {
  auto eod = make();
  EXPECT_FALSE(eod.status(std::nullopt).value().has_value());
  EXPECT_TRUE(eod.history(5).value().empty());
}

}  // namespace
}  // namespace corebank::testing
