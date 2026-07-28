#include "corebank/domain/identifiers.hpp"

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

namespace corebank {
namespace {

TEST(Identifiers, AcceptsWellFormedIds) {
  EXPECT_TRUE(AccountId::parse("ACC-001").has_value());
  EXPECT_TRUE(AccountId::parse("cust.a_1").has_value());
  EXPECT_TRUE(TransactionId::parse("TXN-ABCDEF0123456789-000001").has_value());
  EXPECT_TRUE(IdempotencyKey::parse("order:2026-10-14:0001").has_value());
  EXPECT_TRUE(AccountId::parse(std::string(64, 'A')).has_value());
}

TEST(Identifiers, RejectsMalformedIds) {
  EXPECT_EQ(AccountId::parse("").error().code, ErrorCode::InvalidIdentifier);
  EXPECT_EQ(AccountId::parse("-leading").error().code, ErrorCode::InvalidIdentifier);
  EXPECT_EQ(AccountId::parse("has space").error().code, ErrorCode::InvalidIdentifier);
  EXPECT_EQ(AccountId::parse("pipe|char").error().code, ErrorCode::InvalidIdentifier);
  EXPECT_EQ(AccountId::parse(std::string(65, 'A')).error().code, ErrorCode::InvalidIdentifier);
  EXPECT_EQ(AccountId::parse("colon:no").error().code, ErrorCode::InvalidIdentifier);
  EXPECT_EQ(IdempotencyKey::parse(std::string(129, 'k')).error().code,
            ErrorCode::InvalidIdentifier);
}

TEST(Identifiers, StrongTypesDoNotMix) {
  static_assert(!std::is_convertible_v<AccountId, TransactionId>);
  static_assert(!std::is_convertible_v<TransactionId, AccountId>);
  static_assert(!std::is_convertible_v<std::string, AccountId>);
  const auto a = AccountId::parse("ACC-1").value();
  const auto b = AccountId::parse("ACC-1").value();
  const auto c = AccountId::parse("ACC-2").value();
  EXPECT_EQ(a, b);
  EXPECT_NE(a, c);
  EXPECT_LT(a, c);
  std::unordered_set<AccountId> set{a, b, c};
  EXPECT_EQ(set.size(), 2U);
}

TEST(JournalReference, ValidatesLengthAndCharacters) {
  EXPECT_TRUE(JournalReference::parse("").has_value());
  EXPECT_TRUE(JournalReference::parse("Invoice 42 / payroll").has_value());
  EXPECT_TRUE(JournalReference::parse(std::string(140, 'x')).has_value());
  EXPECT_EQ(JournalReference::parse(std::string(141, 'x')).error().code,
            ErrorCode::InvalidReference);
  EXPECT_EQ(JournalReference::parse("a|b").error().code, ErrorCode::InvalidReference);
  EXPECT_EQ(JournalReference::parse("line\nbreak").error().code, ErrorCode::InvalidReference);
  EXPECT_EQ(JournalReference::parse(std::string("nul\0byte", 8)).error().code,
            ErrorCode::InvalidReference);
}

}  // namespace
}  // namespace corebank
