#include "corebank/domain/transaction.hpp"

#include <gtest/gtest.h>

#include <array>

namespace corebank {
namespace {

constexpr std::array<TransactionStatus, 6> kAll{
    TransactionStatus::Received, TransactionStatus::Validated, TransactionStatus::Processing,
    TransactionStatus::Posted,   TransactionStatus::Rejected,  TransactionStatus::Reversed};

Transaction sample(TransactionStatus status) {
  Transaction txn{
      .id = TransactionId::parse("TXN-1").value(),
      .type = TransactionType::Transfer,
      .status = status,
      .amount = Money{100, CurrencyCode::unchecked("GBP")},
      .source = AccountId::parse("A").value(),
      .destination = AccountId::parse("B").value(),
      .reference = JournalReference::unchecked("t"),
      .idempotency_key = std::nullopt,
      .reversal_of = std::nullopt,
      .reversed_by = std::nullopt,
      .rejection = std::nullopt,
      .business_date = BusinessDate::parse("2026-10-14").value(),
      .created_at = {},
      .updated_at = {},
  };
  return txn;
}

TEST(StateMachine, HappyPath) {
  EXPECT_TRUE(can_transition(TransactionStatus::Received, TransactionStatus::Validated));
  EXPECT_TRUE(can_transition(TransactionStatus::Validated, TransactionStatus::Processing));
  EXPECT_TRUE(can_transition(TransactionStatus::Processing, TransactionStatus::Posted));
  EXPECT_TRUE(can_transition(TransactionStatus::Posted, TransactionStatus::Reversed));
}

TEST(StateMachine, RejectionFromAnyPreTerminalState) {
  EXPECT_TRUE(can_transition(TransactionStatus::Received, TransactionStatus::Rejected));
  EXPECT_TRUE(can_transition(TransactionStatus::Validated, TransactionStatus::Rejected));
  EXPECT_TRUE(can_transition(TransactionStatus::Processing, TransactionStatus::Rejected));
  EXPECT_FALSE(can_transition(TransactionStatus::Posted, TransactionStatus::Rejected));
}

TEST(StateMachine, TerminalStatesNeverMove) {
  for (const auto to : kAll) {
    EXPECT_FALSE(can_transition(TransactionStatus::Rejected, to));
    EXPECT_FALSE(can_transition(TransactionStatus::Reversed, to));
  }
  EXPECT_TRUE(is_terminal(TransactionStatus::Rejected));
  EXPECT_TRUE(is_terminal(TransactionStatus::Reversed));
  EXPECT_FALSE(is_terminal(TransactionStatus::Posted));
}

TEST(StateMachine, ExhaustiveMatrix) {
  int allowed = 0;
  for (const auto from : kAll) {
    for (const auto to : kAll) {
      if (can_transition(from, to)) {
        ++allowed;
        EXPECT_NE(from, to);  // no self loops
      }
    }
  }
  EXPECT_EQ(allowed, 7);  // R->V, R->X, V->P, V->X, P->POSTED, P->X, POSTED->REVERSED
}

TEST(StateMachine, TransitionToMutatesOnlyWhenLegal) {
  auto txn = sample(TransactionStatus::Received);
  const auto at = parse_timestamp("2026-10-14T10:00:00Z").value();
  EXPECT_TRUE(txn.transition_to(TransactionStatus::Validated, at).has_value());
  EXPECT_EQ(txn.status, TransactionStatus::Validated);
  EXPECT_EQ(txn.updated_at, at);

  const auto illegal = txn.transition_to(TransactionStatus::Posted, at);
  ASSERT_FALSE(illegal.has_value());
  EXPECT_EQ(illegal.error().code, ErrorCode::InvalidTransition);
  EXPECT_EQ(txn.status, TransactionStatus::Validated);
}

TEST(StateMachine, RejectStoresReason) {
  auto txn = sample(TransactionStatus::Processing);
  EXPECT_TRUE(txn.reject(make_error(ErrorCode::InsufficientFunds, "nope"), {}).has_value());
  EXPECT_EQ(txn.status, TransactionStatus::Rejected);
  ASSERT_TRUE(txn.rejection.has_value());
  EXPECT_EQ(txn.rejection->code, ErrorCode::InsufficientFunds);
  auto posted = sample(TransactionStatus::Posted);
  EXPECT_EQ(posted.reject(make_error(ErrorCode::Internal, "x"), {}).error().code,
            ErrorCode::InvalidTransition);
  EXPECT_FALSE(posted.rejection.has_value());
}

TEST(StateMachine, NamesRoundTrip) {
  for (const auto status : kAll) {
    EXPECT_EQ(parse_transaction_status(to_string(status)).value(), status);
  }
  for (const auto type : {TransactionType::Credit, TransactionType::Debit,
                          TransactionType::Transfer, TransactionType::Reversal}) {
    EXPECT_EQ(parse_transaction_type(to_string(type)).value(), type);
  }
  EXPECT_FALSE(parse_transaction_status("DONE").has_value());
}

}  // namespace
}  // namespace corebank
