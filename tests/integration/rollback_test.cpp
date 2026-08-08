#include <cstddef>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class RollbackTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)fund("CUST-A", 1'000);
  }
  std::size_t audit_count() {
    return service->audit_trail(AuditQuery{.limit = 1000}).value().size();
  }
};

TEST_F(RollbackTest, CommitFailureLeavesNoTrace) {
  const auto before_audit = audit_count();
  repository->inject_fault(sqlite::Fault::FailCommit);
  const auto receipt = service->transfer(TransferRequest{.key = key("ROLL-1"),
                                                         .source = acc("CUST-A"),
                                                         .destination = acc("CUST-B"),
                                                         .amount = gbp(100),
                                                         .reference = ref("t")});
  repository->inject_fault(sqlite::Fault::None);
  ASSERT_FALSE(receipt.has_value());
  EXPECT_EQ(receipt.error().code, ErrorCode::PersistenceError);
  EXPECT_EQ(balance("CUST-A"), 1'000);
  EXPECT_EQ(balance("CUST-B"), 0);
  EXPECT_EQ(audit_count(), before_audit);
  EXPECT_TRUE(service->account_transactions(acc("CUST-B")).value().empty());
  // The key was not consumed: a retry succeeds and is not a replay.
  const auto retry = transfer("CUST-A", "CUST-B", 100, "ROLL-1");
  EXPECT_TRUE(retry.posted());
  EXPECT_FALSE(retry.replayed);
  EXPECT_EQ(balance("CUST-B"), 100);
}

TEST_F(RollbackTest, MidTransactionFailureRollsBackPartialLegs) {
  repository->inject_fault(sqlite::Fault::FailInsertPosting);
  const auto receipt = service->transfer(TransferRequest{.key = key("ROLL-2"),
                                                         .source = acc("CUST-A"),
                                                         .destination = acc("CUST-B"),
                                                         .amount = gbp(100),
                                                         .reference = ref("t")});
  repository->inject_fault(sqlite::Fault::None);
  ASSERT_FALSE(receipt.has_value());
  EXPECT_EQ(receipt.error().code, ErrorCode::PersistenceError);
  EXPECT_EQ(balance("CUST-A"), 1'000);
  EXPECT_EQ(balance("CUST-B"), 0);
  EXPECT_EQ(service->account_history(acc("CUST-A")).value().size(), 1U);  // only the funding credit
  EXPECT_TRUE(service->account_transactions(acc("CUST-B")).value().empty());
}

TEST_F(RollbackTest, EngineStaysUsableAfterFailure) {
  repository->inject_fault(sqlite::Fault::FailCommit);
  (void)service->credit(CreditRequest{
      .key = key(), .account = acc("CUST-A"), .amount = gbp(1), .reference = ref("x")});
  repository->inject_fault(sqlite::Fault::None);
  EXPECT_TRUE(fund("CUST-A", 1).posted());
  EXPECT_EQ(balance("CUST-A"), 1'001);
  EXPECT_TRUE(repository->health_check().has_value());
}

}  // namespace
}  // namespace corebank::testing
