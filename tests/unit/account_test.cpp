#include "corebank/domain/account.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace corebank {
namespace {

Account sample(std::int64_t balance, bool overdraft, AccountStatus status = AccountStatus::Active) {
  return Account{.id = AccountId::parse("ACC-1").value(),
                 .name = "Sample",
                 .type = AccountType::Customer,
                 .balance = Money{balance, CurrencyCode::unchecked("GBP")},
                 .allow_overdraft = overdraft,
                 .status = status,
                 .version = 0,
                 .created_at = {},
                 .updated_at = {}};
}

TEST(Account, DebitWithinFunds) {
  const auto account = sample(1000, false);
  EXPECT_EQ(account.balance_after_debit(Money{400, CurrencyCode::unchecked("GBP")}).value().minor(),
            600);
  EXPECT_EQ(
      account.balance_after_debit(Money{1000, CurrencyCode::unchecked("GBP")}).value().minor(), 0);
}

TEST(Account, DebitBeyondFundsNeedsOverdraft) {
  const auto strict = sample(100, false);
  const auto result = strict.balance_after_debit(Money{101, CurrencyCode::unchecked("GBP")});
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::InsufficientFunds);

  const auto lenient = sample(100, true);
  EXPECT_EQ(lenient.balance_after_debit(Money{101, CurrencyCode::unchecked("GBP")}).value().minor(),
            -1);
}

TEST(Account, InactiveAccountsRejectMovements) {
  for (const auto status : {AccountStatus::Frozen, AccountStatus::Closed}) {
    const auto account = sample(100, true, status);
    EXPECT_EQ(account.balance_after_debit(Money{1, CurrencyCode::unchecked("GBP")}).error().code,
              ErrorCode::AccountInactive);
    EXPECT_EQ(account.balance_after_credit(Money{1, CurrencyCode::unchecked("GBP")}).error().code,
              ErrorCode::AccountInactive);
  }
}

TEST(Account, CurrencyMustMatch) {
  const auto account = sample(100, true);
  EXPECT_EQ(account.balance_after_credit(Money{1, CurrencyCode::unchecked("USD")}).error().code,
            ErrorCode::CurrencyMismatch);
  EXPECT_EQ(account.balance_after_debit(Money{1, CurrencyCode::unchecked("USD")}).error().code,
            ErrorCode::CurrencyMismatch);
}

TEST(Account, NameValidation) {
  EXPECT_TRUE(validate_account_name("Customer A").has_value());
  EXPECT_FALSE(validate_account_name("").has_value());
  EXPECT_FALSE(validate_account_name(std::string(101, 'n')).has_value());
  EXPECT_FALSE(validate_account_name("bad|name").has_value());
  EXPECT_FALSE(validate_account_name("tab\tname").has_value());
}

TEST(Account, TypeAndStatusNamesRoundTrip) {
  for (const auto type : {AccountType::Customer, AccountType::Settlement, AccountType::FeeIncome,
                          AccountType::Suspense}) {
    EXPECT_EQ(parse_account_type(to_string(type)).value(), type);
  }
  for (const auto status : {AccountStatus::Active, AccountStatus::Frozen, AccountStatus::Closed}) {
    EXPECT_EQ(parse_account_status(to_string(status)).value(), status);
  }
  EXPECT_FALSE(parse_account_type("VAULT").has_value());
}

}  // namespace
}  // namespace corebank
