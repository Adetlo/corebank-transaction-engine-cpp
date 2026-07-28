#include "corebank/domain/money.hpp"

#include <gtest/gtest.h>

#include <compare>
#include <cstdint>
#include <limits>

namespace corebank {
namespace {

const CurrencyCode kGbp = CurrencyCode::unchecked("GBP");
const CurrencyCode kJpy = CurrencyCode::unchecked("JPY");
const CurrencyCode kUsd = CurrencyCode::unchecked("USD");

TEST(Money, ParsesWholeAndFractionalAmounts) {
  EXPECT_EQ(Money::parse("1250.00", kGbp).value().minor(), 125000);
  EXPECT_EQ(Money::parse("1250", kGbp).value().minor(), 125000);
  EXPECT_EQ(Money::parse("0.05", kGbp).value().minor(), 5);
  EXPECT_EQ(Money::parse("0.5", kGbp).value().minor(), 50);
  EXPECT_EQ(Money::parse("-3.25", kGbp).value().minor(), -325);
  EXPECT_EQ(Money::parse("+7", kGbp).value().minor(), 700);
}

TEST(Money, ZeroDecimalCurrencyHasNoFraction) {
  EXPECT_EQ(Money::parse("1500", kJpy).value().minor(), 1500);
  const auto rejected = Money::parse("1500.5", kJpy);
  ASSERT_FALSE(rejected.has_value());
  EXPECT_EQ(rejected.error().code, ErrorCode::InvalidAmount);
}

TEST(Money, RejectsInvalidText) {
  for (const auto* text : {"", "abc", "1.", ".5", "1.234", "1e3", "12,5", "- 1", "1 "}) {
    const auto parsed = Money::parse(text, kGbp);
    EXPECT_FALSE(parsed.has_value()) << text;
    if (!parsed) {
      EXPECT_EQ(parsed.error().code, ErrorCode::InvalidAmount) << text;
    }
  }
}

TEST(Money, RejectsOverflowOnParse) {
  EXPECT_FALSE(Money::parse("99999999999999999999", kGbp).has_value());
  EXPECT_FALSE(Money::parse("92233720368547758.08", kGbp).has_value());
  EXPECT_TRUE(Money::parse("92233720368547758.07", kGbp).has_value());
}

TEST(Money, AddsAndSubtractsExactly) {
  const Money a{1050, kGbp};
  const Money b{275, kGbp};
  EXPECT_EQ(a.add(b).value().minor(), 1325);
  EXPECT_EQ(a.subtract(b).value().minor(), 775);
  EXPECT_EQ(b.subtract(a).value().minor(), -775);
}

TEST(Money, RefusesCrossCurrencyArithmetic) {
  const Money a{100, kGbp};
  const Money b{100, kUsd};
  EXPECT_EQ(a.add(b).error().code, ErrorCode::CurrencyMismatch);
  EXPECT_EQ(a.subtract(b).error().code, ErrorCode::CurrencyMismatch);
  EXPECT_EQ(a.compare(b).error().code, ErrorCode::CurrencyMismatch);
  EXPECT_NE(a, b);
}

TEST(Money, DetectsArithmeticOverflow) {
  const Money max{std::numeric_limits<std::int64_t>::max(), kGbp};
  const Money min{std::numeric_limits<std::int64_t>::min(), kGbp};
  EXPECT_EQ(max.add(Money{1, kGbp}).error().code, ErrorCode::Overflow);
  EXPECT_EQ(min.subtract(Money{1, kGbp}).error().code, ErrorCode::Overflow);
  EXPECT_EQ(min.negated().error().code, ErrorCode::Overflow);
  EXPECT_EQ(max.multiply(2).error().code, ErrorCode::Overflow);
  EXPECT_EQ(max.negated().value().minor(), -std::numeric_limits<std::int64_t>::max());
}

TEST(Money, FormatsWithCurrencyScale) {
  EXPECT_EQ(Money(125050, kGbp).to_string(), "1250.50 GBP");
  EXPECT_EQ(Money(5, kGbp).format_amount(), "0.05");
  EXPECT_EQ(Money(-5, kGbp).format_amount(), "-0.05");
  EXPECT_EQ(Money(0, kGbp).format_amount(), "0.00");
  EXPECT_EQ(Money(1500, kJpy).to_string(), "1500 JPY");
  EXPECT_EQ(Money(std::numeric_limits<std::int64_t>::min(), kGbp).format_amount(),
            "-92233720368547758.08");
}

TEST(Money, ParseAndFormatRoundTrip) {
  for (const auto* text : {"0.00", "0.01", "1.00", "19.99", "123456789.12", "-42.42"}) {
    EXPECT_EQ(Money::parse(text, kGbp).value().format_amount(), text);
  }
}

TEST(Money, ComparesSameCurrency) {
  const Money a{100, kGbp};
  const Money b{200, kGbp};
  EXPECT_EQ(a.compare(b).value(), std::strong_ordering::less);
  EXPECT_EQ(b.compare(a).value(), std::strong_ordering::greater);
  EXPECT_EQ(a.compare(a).value(), std::strong_ordering::equal);
  EXPECT_TRUE(a.is_positive());
  EXPECT_TRUE(Money::zero(kGbp).is_zero());
  EXPECT_TRUE(Money(-1, kGbp).is_negative());
}

}  // namespace
}  // namespace corebank
