// ISO-4217-style currency code with its minor-unit exponent.
#pragma once

#include "corebank/domain/result.hpp"

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace corebank {

class CurrencyCode {
 public:
  // Parses an upper-case three-letter code from the supported table.
  [[nodiscard]] static Result<CurrencyCode> parse(std::string_view text);
  // Convenience for literals that are known to be valid (asserts in debug).
  [[nodiscard]] static CurrencyCode unchecked(std::string_view text);

  [[nodiscard]] std::string_view code() const noexcept { return {code_.data(), 3}; }
  [[nodiscard]] std::string str() const { return std::string{code()}; }
  // Number of decimal places in the major unit (2 for GBP, 0 for JPY).
  [[nodiscard]] int minor_units() const noexcept { return exponent_; }
  // 10^minor_units, the number of minor units in one major unit.
  [[nodiscard]] std::int64_t minor_per_major() const noexcept;

  [[nodiscard]] bool operator==(const CurrencyCode& other) const noexcept = default;
  [[nodiscard]] std::strong_ordering operator<=>(const CurrencyCode& other) const noexcept {
    return code_ <=> other.code_;
  }

  [[nodiscard]] static std::string_view supported_codes_text();

 private:
  constexpr CurrencyCode(std::array<char, 3> code, int exponent) noexcept
      : code_(code), exponent_(exponent) {}

  std::array<char, 3> code_{};
  int exponent_{2};
};

}  // namespace corebank

template <>
struct std::hash<corebank::CurrencyCode> {
  std::size_t operator()(const corebank::CurrencyCode& ccy) const noexcept {
    return std::hash<std::string_view>{}(ccy.code());
  }
};
