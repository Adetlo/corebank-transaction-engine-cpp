#include "test_support.hpp"

namespace corebank::testing {
namespace {

class IdempotencyTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)fund("CUST-A", 10'000);
  }
};

TEST_F(IdempotencyTest, SameKeySameRequestReplaysWithoutReposting) {
  const auto first = transfer("CUST-A", "CUST-B", 1'000, "ORDER-1");
  const auto second = transfer("CUST-A", "CUST-B", 1'000, "ORDER-1");
  EXPECT_FALSE(first.replayed);
  EXPECT_TRUE(second.replayed);
  EXPECT_EQ(first.transaction.id, second.transaction.id);
  EXPECT_EQ(balance("CUST-A"), 9'000);
  EXPECT_EQ(balance("CUST-B"), 1'000);
  EXPECT_EQ(service->account_transactions(acc("CUST-B")).value().size(), 1U);
}

TEST_F(IdempotencyTest, SameKeyDifferentRequestIsAConflict) {
  (void)transfer("CUST-A", "CUST-B", 1'000, "ORDER-2");
  const auto changed_amount = service->transfer(TransferRequest{.key = key("ORDER-2"),
                                                                .source = acc("CUST-A"),
                                                                .destination = acc("CUST-B"),
                                                                .amount = gbp(2'000),
                                                                .reference = ref("transfer")});
  ASSERT_FALSE(changed_amount.has_value());
  EXPECT_EQ(changed_amount.error().code, ErrorCode::IdempotencyConflict);
  const auto changed_type = service->credit(CreditRequest{.key = key("ORDER-2"),
                                                          .account = acc("CUST-B"),
                                                          .amount = gbp(1'000),
                                                          .reference = ref("transfer")});
  EXPECT_EQ(changed_type.error().code, ErrorCode::IdempotencyConflict);
  EXPECT_EQ(balance("CUST-A"), 9'000);
}

TEST_F(IdempotencyTest, RejectedOutcomesAreReplayedToo) {
  const auto first = transfer("CUST-A", "CUST-B", 50'000, "ORDER-3");
  EXPECT_EQ(first.transaction.status, TransactionStatus::Rejected);
  const auto second = transfer("CUST-A", "CUST-B", 50'000, "ORDER-3");
  EXPECT_TRUE(second.replayed);
  EXPECT_EQ(second.transaction.id, first.transaction.id);
  EXPECT_EQ(second.transaction.status, TransactionStatus::Rejected);
}

TEST_F(IdempotencyTest, KeysAreScopedAcrossOperationTypes) {
  const auto credit = service->credit(CreditRequest{
      .key = key("K-SHARED"), .account = acc("CUST-A"), .amount = gbp(10), .reference = ref("c")});
  EXPECT_TRUE(credit.value().posted());
  const auto debit = service->debit(DebitRequest{
      .key = key("K-SHARED"), .account = acc("CUST-A"), .amount = gbp(10), .reference = ref("c")});
  EXPECT_EQ(debit.error().code, ErrorCode::IdempotencyConflict);
}

TEST_F(IdempotencyTest, StoredTransactionCarriesTheKey) {
  const auto receipt = transfer("CUST-A", "CUST-B", 1, "ORDER-4");
  const auto stored = service->get_transaction(receipt.transaction.id).value();
  ASSERT_TRUE(stored.idempotency_key.has_value());
  EXPECT_EQ(stored.idempotency_key->str(), "ORDER-4");
}

}  // namespace
}  // namespace corebank::testing
