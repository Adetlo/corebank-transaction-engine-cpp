// Exact money value object: int64 minor units + currency, overflow-checked arithmetic.
#pragma once

#include "corebank/domain/currency.hpp"
#include "corebank/domain/result.hpp"

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

namespace corebank {

class Money {
 public:
  constexpr Money(std::int64_t minor, CurrencyCode currency) noexcept
      : minor_(minor), currency_(currency) {}

  [[nodiscard]] static Money zero(CurrencyCode currency) noexcept { return {0, currency}; }

  // Parses a decimal amount such as "1250.00" or "-3" in the currency's minor units.
  // Rejects more decimals than the currency supports, exponent notation, and overflow.
  [[nodiscard]] static Result<Money> parse(std::string_view text, CurrencyCode currency);

  [[nodiscard]] constexpr std::int64_t minor() const noexcept { return minor_; }
  [[nodiscard]] constexpr CurrencyCode currency() const noexcept { return currency_; }

  [[nodiscard]] constexpr bool is_zero() const noexcept { return minor_ == 0; }
  [[nodiscard]] constexpr bool is_positive() const noexcept { return minor_ > 0; }
  [[nodiscard]] constexpr bool is_negative() const noexcept { return minor_ < 0; }

  [[nodiscard]] Result<Money> add(const Money& other) const;
  [[nodiscard]] Result<Money> subtract(const Money& other) const;
  [[nodiscard]] Result<Money> negated() const;
  [[nodiscard]] Result<Money> multiply(std::int64_t factor) const;

  [[nodiscard]] bool same_currency(const Money& other) const noexcept {
    return currency_ == other.currency_;
  }

  // Ordering is only meaningful between amounts of the same currency; callers must
  // check same_currency() first (the service layer does this before any comparison).
  [[nodiscard]] Result<std::strong_ordering> compare(const Money& other) const;

  [[nodiscard]] bool operator==(const Money& other) const noexcept = default;

  // "1250.00" (no currency).
  [[nodiscard]] std::string format_amount() const;
  // "1250.00 GBP".
  [[nodiscard]] std::string to_string() const;

 private:
  std::int64_t minor_;
  CurrencyCode currency_;
};

}  // namespace corebank
