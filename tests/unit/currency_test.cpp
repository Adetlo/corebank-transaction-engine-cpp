#include "corebank/domain/currency.hpp"

#include <gtest/gtest.h>

#include <functional>

namespace corebank {
namespace {

TEST(CurrencyCode, ParsesSupportedCodes) {
  for (const auto* code : {"GBP", "USD", "EUR", "JPY", "IDR", "SGD", "CHF", "AUD"}) {
    const auto parsed = CurrencyCode::parse(code);
    ASSERT_TRUE(parsed.has_value()) << code;
    EXPECT_EQ(parsed.value().code(), code);
  }
  EXPECT_EQ(CurrencyCode::parse("GBP").value().minor_units(), 2);
  EXPECT_EQ(CurrencyCode::parse("JPY").value().minor_units(), 0);
  EXPECT_EQ(CurrencyCode::parse("GBP").value().minor_per_major(), 100);
  EXPECT_EQ(CurrencyCode::parse("JPY").value().minor_per_major(), 1);
}

TEST(CurrencyCode, RejectsUnknownAndMalformed) {
  for (const auto* code : {"", "gbp", "GB", "GBPP", "XXX", "12A", "G P"}) {
    const auto parsed = CurrencyCode::parse(code);
    EXPECT_FALSE(parsed.has_value()) << code;
    if (!parsed) {
      EXPECT_EQ(parsed.error().code, ErrorCode::InvalidCurrency);
    }
  }
}

TEST(CurrencyCode, EqualityAndOrdering) {
  const auto gbp = CurrencyCode::unchecked("GBP");
  const auto usd = CurrencyCode::unchecked("USD");
  EXPECT_EQ(gbp, CurrencyCode::unchecked("GBP"));
  EXPECT_NE(gbp, usd);
  EXPECT_LT(gbp, usd);
  EXPECT_EQ(std::hash<CurrencyCode>{}(gbp),
            std::hash<CurrencyCode>{}(CurrencyCode::unchecked("GBP")));
}

}  // namespace
}  // namespace corebank
