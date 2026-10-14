#include "corebank/domain/currency.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace corebank {
namespace {

struct CurrencyEntry {
  std::string_view code;
  int exponent{2};
};

// Deliberately small, generic table. Adding a currency is a one-line change.
constexpr std::array<CurrencyEntry, 8> kCurrencies{{
    {"GBP", 2},
    {"USD", 2},
    {"EUR", 2},
    {"JPY", 0},
    {"IDR", 2},
    {"SGD", 2},
    {"CHF", 2},
    {"AUD", 2},
}};

constexpr std::string_view kSupportedText = "GBP, USD, EUR, JPY, IDR, SGD, CHF, AUD";

}  // namespace

Result<CurrencyCode> CurrencyCode::parse(std::string_view text) {
  if (text.size() != 3) {
    return make_error(ErrorCode::InvalidCurrency,
                      "currency code must be exactly three upper-case letters");
  }
  for (const char ch : text) {
    if (std::isupper(static_cast<unsigned char>(ch)) == 0) {
      return make_error(ErrorCode::InvalidCurrency,
                        "currency code must be exactly three upper-case letters");
    }
  }
  const auto* const it =
      std::find_if(kCurrencies.begin(), kCurrencies.end(),
                   [text](const CurrencyEntry& entry) { return entry.code == text; });
  if (it == kCurrencies.end()) {
    return make_error(ErrorCode::InvalidCurrency,
                      "unsupported currency '" + std::string{text} +
                          "' (supported: " + std::string{kSupportedText} + ")");
  }
  return CurrencyCode(std::array<char, 3>{text[0], text[1], text[2]}, it->exponent);
}

CurrencyCode CurrencyCode::unchecked(std::string_view text) {
  auto parsed = parse(text);
  assert(parsed.has_value() && "CurrencyCode::unchecked called with an unsupported code");
  if (!parsed) {
    return CurrencyCode(std::array<char, 3>{'X', 'X', 'X'}, 2);
  }
  return parsed.value();
}

std::int64_t CurrencyCode::minor_per_major() const noexcept {
  std::int64_t result = 1;
  for (int i = 0; i < exponent_; ++i) {
    result *= 10;
  }
  return result;
}

std::string_view CurrencyCode::supported_codes_text() {
  return kSupportedText;
}

}  // namespace corebank
