#include "corebank/domain/result.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace corebank {
namespace {

Result<int> half(int value) {
  if (value % 2 != 0) {
    return make_error(ErrorCode::ValidationError, "odd");
  }
  return value / 2;
}

TEST(Result, HoldsValueOrError) {
  const Result<int> ok_value = 4;
  EXPECT_TRUE(ok_value.has_value());
  EXPECT_TRUE(static_cast<bool>(ok_value));
  EXPECT_EQ(ok_value.value(), 4);

  const Result<int> failed = make_error(ErrorCode::InsufficientFunds, "no");
  EXPECT_FALSE(failed.has_value());
  EXPECT_EQ(failed.error().code, ErrorCode::InsufficientFunds);
  EXPECT_EQ(failed.error().message, "no");
  EXPECT_EQ(failed.value_or(-1), -1);
}

TEST(Result, MonadicHelpersPropagateErrors) {
  EXPECT_EQ(half(8).and_then(half).value(), 2);
  EXPECT_EQ(half(6).and_then(half).error().code, ErrorCode::ValidationError);
  EXPECT_EQ(half(7).and_then(half).error().message, "odd");
  EXPECT_EQ(half(8).map([](int v) { return std::to_string(v); }).value(), "4");
  EXPECT_FALSE(half(3).map([](int v) { return v; }).has_value());
}

TEST(Result, VoidSpecialisation) {
  const Result<void> good = ok();
  EXPECT_TRUE(good.has_value());
  const Result<void> bad = make_error(ErrorCode::PersistenceError, "db");
  EXPECT_TRUE(bad.has_error());
  EXPECT_EQ(bad.error().code, ErrorCode::PersistenceError);
}

TEST(Result, MoveOnlyPayloads) {
  Result<std::unique_ptr<int>> boxed = std::make_unique<int>(5);
  auto taken = std::move(boxed).value();
  EXPECT_EQ(*taken, 5);
}

TEST(ErrorCode, EveryCodeHasAStableName) {
  for (int i = 0; i <= static_cast<int>(ErrorCode::Internal); ++i) {
    const auto name = to_string(static_cast<ErrorCode>(i));
    EXPECT_FALSE(name.empty());
    EXPECT_NE(name, "UNKNOWN_ERROR") << i;
  }
  EXPECT_EQ(to_string(ErrorCode::InsufficientFunds), "INSUFFICIENT_FUNDS");
}

}  // namespace
}  // namespace corebank
