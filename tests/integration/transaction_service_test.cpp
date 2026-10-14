#include <string>

#include "test_support.hpp"

namespace corebank::testing {
namespace {

class TransactionServiceTest : public EngineFixture {};

TEST_F(TransactionServiceTest, CreatesAccountsWithZeroBalanceAndAudit) {
  const auto account = make_account("CUST-A");
  EXPECT_EQ(account.balance.minor(), 0);
  EXPECT_EQ(account.status, AccountStatus::Active);
  const auto stored = service->get_account(acc("CUST-A")).value();
  EXPECT_EQ(stored.name, "Account CUST-A");
  const auto events = service->audit_trail(AuditQuery{.entity_id = std::string{"CUST-A"}}).value();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().type, AuditEventType::AccountCreated);
}

TEST_F(TransactionServiceTest, DuplicateAccountIsRejected) {
  (void)make_account("CUST-A");
  const auto again =
      service->create_account(CreateAccountRequest{.id = acc("CUST-A"),
                                                   .name = "Again",
                                                   .type = AccountType::Customer,
                                                   .currency = CurrencyCode::unchecked("GBP"),
                                                   .allow_overdraft = false});
  ASSERT_FALSE(again.has_value());
  EXPECT_EQ(again.error().code, ErrorCode::AccountExists);
  EXPECT_EQ(service->get_account(acc("NOPE")).error().code, ErrorCode::AccountNotFound);
}

TEST_F(TransactionServiceTest, CreditAndDebitMoveBalanceWithPostings) {
  (void)make_account("CUST-A");
  const auto credit = fund("CUST-A", 10'000);
  EXPECT_TRUE(credit.posted());
  EXPECT_EQ(balance("CUST-A"), 10'000);
  const auto debit = service->debit(DebitRequest{
      .key = key(), .account = acc("CUST-A"), .amount = gbp(2'500), .reference = ref("atm")});
  ASSERT_TRUE(debit.has_value());
  EXPECT_TRUE(debit.value().posted());
  EXPECT_EQ(balance("CUST-A"), 7'500);
  const auto history = service->account_history(acc("CUST-A")).value();
  ASSERT_EQ(history.size(), 2U);
  EXPECT_EQ(history[0].direction, PostingDirection::Debit);  // newest first
  EXPECT_EQ(history[0].balance_after_minor, 7'500);
  EXPECT_EQ(history[1].direction, PostingDirection::Credit);
  EXPECT_EQ(history[1].balance_after_minor, 10'000);
}

TEST_F(TransactionServiceTest, TransferIsAtomicAndDoubleEntry) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  (void)fund("CUST-A", 5'000);
  const auto receipt = transfer("CUST-A", "CUST-B", 1'250);
  EXPECT_TRUE(receipt.posted());
  EXPECT_EQ(balance("CUST-A"), 3'750);
  EXPECT_EQ(balance("CUST-B"), 1'250);
  const auto legs = service->transaction_postings(receipt.transaction.id).value();
  ASSERT_EQ(legs.size(), 2U);
  EXPECT_EQ(legs[0].direction, PostingDirection::Debit);
  EXPECT_EQ(legs[0].account_id, acc("CUST-A"));
  EXPECT_EQ(legs[1].direction, PostingDirection::Credit);
  EXPECT_EQ(legs[1].account_id, acc("CUST-B"));
  EXPECT_EQ(legs[0].amount, legs[1].amount);
}

TEST_F(TransactionServiceTest, InsufficientFundsIsRecordedAsRejectedTransaction) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  (void)fund("CUST-A", 100);
  const auto receipt = transfer("CUST-A", "CUST-B", 101);
  EXPECT_FALSE(receipt.posted());
  EXPECT_EQ(receipt.transaction.status, TransactionStatus::Rejected);
  ASSERT_TRUE(receipt.transaction.rejection.has_value());
  EXPECT_EQ(receipt.transaction.rejection->code, ErrorCode::InsufficientFunds);
  EXPECT_EQ(balance("CUST-A"), 100);
  EXPECT_EQ(balance("CUST-B"), 0);
  EXPECT_TRUE(service->transaction_postings(receipt.transaction.id).value().empty());
  const auto stored = service->get_transaction(receipt.transaction.id).value();
  EXPECT_EQ(stored.status, TransactionStatus::Rejected);
}

TEST_F(TransactionServiceTest, OverdraftAccountMayGoNegative) {
  (void)make_account("SETTLE", "GBP", true, AccountType::Settlement);
  (void)make_account("CUST-A");
  EXPECT_TRUE(transfer("SETTLE", "CUST-A", 500).posted());
  EXPECT_EQ(balance("SETTLE"), -500);
  EXPECT_EQ(balance("CUST-A"), 500);
}

TEST_F(TransactionServiceTest, ValidationErrorsAreNotPersisted) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  const auto zero = service->transfer(TransferRequest{.key = key(),
                                                      .source = acc("CUST-A"),
                                                      .destination = acc("CUST-B"),
                                                      .amount = gbp(0),
                                                      .reference = ref("zero")});
  EXPECT_EQ(zero.error().code, ErrorCode::InvalidAmount);
  const auto negative = service->credit(CreditRequest{
      .key = key(), .account = acc("CUST-A"), .amount = gbp(-5), .reference = ref("neg")});
  EXPECT_EQ(negative.error().code, ErrorCode::InvalidAmount);
  const auto same = service->transfer(TransferRequest{.key = key(),
                                                      .source = acc("CUST-A"),
                                                      .destination = acc("CUST-A"),
                                                      .amount = gbp(5),
                                                      .reference = ref("self")});
  EXPECT_EQ(same.error().code, ErrorCode::SameAccountTransfer);
  EXPECT_TRUE(service->account_transactions(acc("CUST-A")).value().empty());
}

TEST_F(TransactionServiceTest, TransactionLimitIsEnforced) {
  TransactionService limited{*repository, clock, ids,
                             ServiceLimits{.max_transaction_minor = 1'000}};
  (void)make_account("CUST-A");
  const auto too_big = limited.credit(CreditRequest{
      .key = key(), .account = acc("CUST-A"), .amount = gbp(1'001), .reference = ref("big")});
  EXPECT_EQ(too_big.error().code, ErrorCode::InvalidAmount);
  EXPECT_TRUE(
      limited
          .credit(CreditRequest{
              .key = key(), .account = acc("CUST-A"), .amount = gbp(1'000), .reference = ref("ok")})
          .value()
          .posted());
}

TEST_F(TransactionServiceTest, UnknownAccountAndCurrencyMismatchAreRejected) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-USD", "USD");
  const auto missing = transfer("CUST-A", "GHOST", 10);
  EXPECT_EQ(missing.transaction.status, TransactionStatus::Rejected);
  EXPECT_EQ(missing.transaction.rejection->code, ErrorCode::AccountNotFound);
  const auto mismatch = service->credit(CreditRequest{
      .key = key(), .account = acc("CUST-USD"), .amount = gbp(10), .reference = ref("x")});
  EXPECT_EQ(mismatch.value().transaction.rejection->code, ErrorCode::CurrencyMismatch);
  EXPECT_EQ(balance("CUST-USD"), 0);
}

TEST_F(TransactionServiceTest, LookupAndHistoryQueries) {
  (void)make_account("CUST-A");
  (void)make_account("CUST-B");
  (void)fund("CUST-A", 1'000);
  const auto t1 = transfer("CUST-A", "CUST-B", 100);
  const auto t2 = transfer("CUST-A", "CUST-B", 200);
  EXPECT_EQ(service->get_transaction(t1.transaction.id).value().amount.minor(), 100);
  EXPECT_EQ(service->get_transaction(TransactionId::parse("TXN-NONE").value()).error().code,
            ErrorCode::TransactionNotFound);
  const auto txns = service->account_transactions(acc("CUST-B")).value();
  ASSERT_EQ(txns.size(), 2U);
  EXPECT_EQ(txns[0].id, t2.transaction.id);  // newest first
  EXPECT_EQ(service->account_history(acc("CUST-A"), 2).value().size(), 2U);
  EXPECT_EQ(service->account_history(acc("GHOST")).error().code, ErrorCode::AccountNotFound);
  EXPECT_EQ(service->list_accounts().value().size(), 2U);
}

TEST_F(TransactionServiceTest, BusinessDateFollowsClockUnlessOverridden) {
  (void)make_account("CUST-A");
  const auto today = BusinessDate::from_timestamp(clock.now());
  EXPECT_EQ(fund("CUST-A", 1).transaction.business_date, today);
  service->set_business_date_override(BusinessDate::parse("2030-01-02").value());
  EXPECT_EQ(fund("CUST-A", 1).transaction.business_date.str(), "2030-01-02");
  EXPECT_EQ(service->current_business_date().str(), "2030-01-02");
}

}  // namespace
}  // namespace corebank::testing
