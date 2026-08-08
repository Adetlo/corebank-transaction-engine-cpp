#include "corebank/infrastructure/sqlite/connection.hpp"
#include "corebank/infrastructure/sqlite/statement.hpp"

#include <string>
#include <vector>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class AuditTest : public EngineFixture {};

TEST_F(AuditTest, EveryImportantStateChangeIsAudited) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  (void)fund("CUST-A", 1'000);
  (void)transfer("CUST-A", "CUST-B", 5'000);  // rejected
  const auto events = service->audit_trail(AuditQuery{}).value();
  std::vector<AuditEventType> types;
  for (const auto& e : events) {
    types.push_back(e.type);
  }
  const std::vector<AuditEventType> expected{
      AuditEventType::AccountCreated,      AuditEventType::AccountCreated,
      AuditEventType::TransactionReceived, AuditEventType::TransactionPosted,
      AuditEventType::TransactionReceived, AuditEventType::TransactionRejected};
  EXPECT_EQ(types, expected);
  EXPECT_NE(events.back().detail.find("INSUFFICIENT_FUNDS"), std::string::npos);
}

TEST_F(AuditTest, AuditEventsCannotBeUpdatedOrDeleted) {
  (void)make_account("CUST-A");
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  const auto update = connection->exec("UPDATE audit_events SET detail = '{}'");
  ASSERT_FALSE(update.has_value());
  EXPECT_NE(update.error().message.find("immutable"), std::string::npos);
  const auto remove = connection->exec("DELETE FROM audit_events");
  ASSERT_FALSE(remove.has_value());
  EXPECT_EQ(service->audit_trail(AuditQuery{}).value().size(), 1U);
}

TEST_F(AuditTest, PostingsAndIdempotencyRecordsAreImmutableToo) {
  (void)make_account("CUST-A");
  (void)fund("CUST-A", 10);
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  EXPECT_FALSE(connection->exec("UPDATE postings SET amount_minor = 1").has_value());
  EXPECT_FALSE(connection->exec("DELETE FROM postings").has_value());
  EXPECT_FALSE(connection->exec("DELETE FROM transactions").has_value());
  EXPECT_FALSE(connection->exec("DELETE FROM accounts").has_value());
  EXPECT_FALSE(
      connection->exec("UPDATE idempotency_records SET request_signature = 'x'").has_value());
}

TEST_F(AuditTest, TerminalTransactionsCannotChangeStatusAtStorageLevel) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  const auto rejected = transfer("CUST-A", "CUST-B", 5);
  auto connection = sqlite::Connection::open(temp.db_path()).value();
  auto stmt =
      connection->prepare("UPDATE transactions SET status = 'POSTED' WHERE transaction_id = ?")
          .value();
  ASSERT_TRUE(stmt.bind(1, rejected.transaction.id.view()).has_value());
  const auto executed = stmt.execute();
  ASSERT_FALSE(executed.has_value());
  EXPECT_NE(executed.error().message.find("terminal"), std::string::npos);
}

TEST_F(AuditTest, QueriesFilterByEntityAndType) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  (void)fund("CUST-A", 1);
  EXPECT_EQ(service->audit_trail(AuditQuery{.entity_id = std::string{"CUST-B"}}).value().size(),
            1U);
  EXPECT_EQ(
      service->audit_trail(AuditQuery{.type = AuditEventType::TransactionPosted}).value().size(),
      1U);
  EXPECT_EQ(service->audit_trail(AuditQuery{.limit = 2}).value().size(), 2U);
}

}  // namespace
}  // namespace corebank::testing
