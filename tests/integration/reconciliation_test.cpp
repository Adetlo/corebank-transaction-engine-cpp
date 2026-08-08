#include "corebank/application/reconciliation_service.hpp"
#include "corebank/infrastructure/sqlite/connection.hpp"

#include <string>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class ReconciliationTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    (void)make_account("SETTLE", "GBP", true, AccountType::Settlement);
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)make_account("CUST-USD", "USD");
    (void)fund("SETTLE", 100'000);
    (void)transfer("SETTLE", "CUST-A", 10'000);
    (void)transfer("CUST-A", "CUST-B", 2'500);
    (void)transfer("CUST-B", "CUST-A", 99'999);  // rejected
    (void)fund("CUST-USD", 777, "USD");
  }
  ReconciliationResult run() {
    ReconciliationService svc{*repository, clock};
    auto result = svc.run(std::nullopt);
    EXPECT_TRUE(result.has_value()) << result.error().message;
    return result.value();
  }
  void raw(const std::string& sql) {
    auto connection = sqlite::Connection::open(temp.db_path()).value();
    ASSERT_TRUE(connection->exec(sql).has_value());
  }
  bool check(const ReconciliationResult& result, const std::string& name) {
    for (const auto& c : result.checks) {
      if (c.name == name) {
        return c.passed;
      }
    }
    ADD_FAILURE() << "check not found: " << name;
    return false;
  }
};

TEST_F(ReconciliationTest, CleanLedgerPassesEveryCheck) {
  const auto result = run();
  EXPECT_TRUE(result.passed) << result.summary();
  EXPECT_EQ(result.failed_count(), 0U);
  EXPECT_EQ(result.checks.size(), 7U);
  ASSERT_EQ(result.totals.size(), 2U);
  EXPECT_EQ(result.totals[0].currency, "GBP");
  EXPECT_EQ(result.totals[0].posted_count, 3);
  EXPECT_EQ(result.totals[0].rejected_count, 1);
  EXPECT_EQ(result.totals[0].credit_total_minor, 100'000);
  EXPECT_EQ(result.totals[0].transfer_total_minor, 12'500);
  EXPECT_EQ(result.totals[1].currency, "USD");
  EXPECT_EQ(result.totals[1].credit_total_minor, 777);
  const auto events =
      service->audit_trail(AuditQuery{.type = AuditEventType::ReconciliationRun}).value();
  EXPECT_EQ(events.size(), 1U);
}

TEST_F(ReconciliationTest, DetectsTamperedBalance) {
  // Bypass the engine: the account table drifts from the postings.
  raw("UPDATE accounts SET balance_minor = balance_minor + 1 WHERE account_id = 'CUST-A'");
  const auto result = run();
  EXPECT_FALSE(result.passed);
  EXPECT_FALSE(check(result, "account_balance_vs_postings"));
  EXPECT_FALSE(check(result, "posting_chain_balance_after"));
  EXPECT_TRUE(check(result, "journal_consistency"));
}

TEST_F(ReconciliationTest, DetectsOverdraftBreach) {
  raw("UPDATE accounts SET balance_minor = -1, allow_overdraft = 1 WHERE account_id = 'CUST-B'");
  const auto result = run();
  EXPECT_TRUE(check(result, "overdraft_policy"));  // overdraft allowed
  raw("UPDATE accounts SET balance_minor = 0 WHERE account_id = 'CUST-B'");
  raw("PRAGMA ignore_check_constraints = ON");
  // Force an impossible state through a direct write that the CHECK would normally block.
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  ASSERT_TRUE(connection->exec("PRAGMA ignore_check_constraints = ON").has_value());
  ASSERT_TRUE(connection
                  ->exec("UPDATE accounts SET balance_minor = -5, allow_overdraft = 0 WHERE "
                         "account_id = 'CUST-B'")
                  .has_value());
  const auto breached = run();
  EXPECT_FALSE(check(breached, "overdraft_policy"));
}

TEST_F(ReconciliationTest, DetectsOrphanPostingsAndMissingLegs) {
  // A posted transaction whose second leg vanished would violate double entry. Postings are
  // immutable, so simulate the corruption by inserting a stray posting for a REJECTED txn.
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  ASSERT_TRUE(connection
                  ->exec("INSERT INTO postings(transaction_id, account_id, direction, currency, "
                         "amount_minor, balance_after_minor, created_at) SELECT transaction_id, "
                         "'CUST-A', 'CREDIT', 'GBP', 1, 1, created_at FROM transactions WHERE "
                         "status = 'REJECTED' LIMIT 1")
                  .has_value());
  const auto result = run();
  EXPECT_FALSE(result.passed);
  EXPECT_FALSE(check(result, "journal_consistency"));
  EXPECT_FALSE(check(result, "account_balance_vs_postings"));
}

TEST_F(ReconciliationTest, ScopesTotalsByBusinessDate) {
  ReconciliationService svc{*repository, clock};
  const auto other_day = svc.run(BusinessDate::parse("1999-01-01").value()).value();
  EXPECT_TRUE(other_day.passed);
  EXPECT_TRUE(other_day.totals.empty());
  EXPECT_EQ(other_day.business_date, "1999-01-01");
}

}  // namespace
}  // namespace corebank::testing
