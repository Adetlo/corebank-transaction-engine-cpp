#include <string>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class ReversalTest : public EngineFixture {
 protected:
  void SetUp() override {
    EngineFixture::SetUp();
    (void)make_account("CUST-A");
    (void)make_account("CUST-B");
    (void)fund("CUST-A", 10'000);
  }
  Result<TransactionReceipt> reverse(const TransactionId& id, const std::string& k = "") {
    return service->reverse(
        ReverseRequest{.key = key(k), .original = id, .reference = ref("undo")});
  }
};

TEST_F(ReversalTest, SingleReversalRestoresBalancesAndLinksBothSides) {
  const auto original = transfer("CUST-A", "CUST-B", 2'500);
  const auto reversal = reverse(original.transaction.id);
  ASSERT_TRUE(reversal.has_value()) << reversal.error().message;
  EXPECT_TRUE(reversal.value().posted());
  EXPECT_EQ(reversal.value().transaction.type, TransactionType::Reversal);
  EXPECT_EQ(reversal.value().transaction.reversal_of, original.transaction.id);
  EXPECT_EQ(balance("CUST-A"), 10'000);
  EXPECT_EQ(balance("CUST-B"), 0);

  const auto stored_original = service->get_transaction(original.transaction.id).value();
  EXPECT_EQ(stored_original.status, TransactionStatus::Reversed);
  EXPECT_EQ(stored_original.reversed_by, reversal.value().transaction.id);
  // History is preserved: four postings in total, nothing deleted.
  EXPECT_EQ(service->account_history(acc("CUST-B")).value().size(), 2U);
  const auto legs = service->transaction_postings(reversal.value().transaction.id).value();
  ASSERT_EQ(legs.size(), 2U);
  EXPECT_EQ(legs[0].account_id, acc("CUST-B"));
  EXPECT_EQ(legs[0].direction, PostingDirection::Debit);
}

TEST_F(ReversalTest, DuplicateReversalIsRejected) {
  const auto original = transfer("CUST-A", "CUST-B", 100);
  ASSERT_TRUE(reverse(original.transaction.id).value().posted());
  const auto again = reverse(original.transaction.id);
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error().code, ErrorCode::InvalidReversal);
  EXPECT_EQ(balance("CUST-A"), 10'000);
}

TEST_F(ReversalTest, DuplicateReversalWithSameKeyReplays) {
  const auto original = transfer("CUST-A", "CUST-B", 100);
  const auto first = reverse(original.transaction.id, "UNDO-1").value();
  const auto second = reverse(original.transaction.id, "UNDO-1").value();
  EXPECT_TRUE(second.replayed);
  EXPECT_EQ(first.transaction.id, second.transaction.id);
}

TEST_F(ReversalTest, InvalidReversalTargets) {
  EXPECT_EQ(reverse(TransactionId::parse("TXN-MISSING").value()).error().code,
            ErrorCode::InvalidReversal);
  const auto rejected = transfer("CUST-B", "CUST-A", 5);  // CUST-B has no funds
  EXPECT_EQ(rejected.transaction.status, TransactionStatus::Rejected);
  EXPECT_EQ(reverse(rejected.transaction.id).error().code, ErrorCode::InvalidReversal);
  const auto original = transfer("CUST-A", "CUST-B", 100);
  const auto reversal = reverse(original.transaction.id).value();
  EXPECT_EQ(reverse(reversal.transaction.id).error().code, ErrorCode::InvalidReversal);
}

TEST_F(ReversalTest, ReversingACreditNeedsFundsAndRecordsRejection) {
  const auto credit = service->credit(CreditRequest{
      .key = key(), .account = acc("CUST-B"), .amount = gbp(300), .reference = ref("c")});
  (void)transfer("CUST-B", "CUST-A", 300);  // money moved on
  const auto reversal = reverse(credit.value().transaction.id).value();
  EXPECT_EQ(reversal.transaction.status, TransactionStatus::Rejected);
  EXPECT_EQ(reversal.transaction.rejection->code, ErrorCode::InsufficientFunds);
  // Original stays POSTED because the compensating transaction never posted.
  EXPECT_EQ(service->get_transaction(credit.value().transaction.id).value().status,
            TransactionStatus::Posted);
}

TEST_F(ReversalTest, ReversalOfDebitRecredits) {
  const auto debit = service->debit(DebitRequest{
      .key = key(), .account = acc("CUST-A"), .amount = gbp(400), .reference = ref("d")});
  EXPECT_EQ(balance("CUST-A"), 9'600);
  const auto reversal = reverse(debit.value().transaction.id).value();
  EXPECT_TRUE(reversal.posted());
  EXPECT_EQ(balance("CUST-A"), 10'000);
  const auto legs = service->transaction_postings(reversal.transaction.id).value();
  ASSERT_EQ(legs.size(), 1U);
  EXPECT_EQ(legs[0].direction, PostingDirection::Credit);
}

TEST_F(ReversalTest, AuditTrailRecordsReversal) {
  const auto original = transfer("CUST-A", "CUST-B", 10);
  (void)reverse(original.transaction.id);
  const auto events =
      service->audit_trail(AuditQuery{.entity_id = original.transaction.id.str()}).value();
  ASSERT_EQ(events.size(), 3U);
  EXPECT_EQ(events[0].type, AuditEventType::TransactionReceived);
  EXPECT_EQ(events[1].type, AuditEventType::TransactionPosted);
  EXPECT_EQ(events[2].type, AuditEventType::TransactionReversed);
}

}  // namespace
}  // namespace corebank::testing
