#include "corebank/domain/money.hpp"

#include <cctype>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>

namespace corebank {
namespace {

bool add_overflows(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept {
  return __builtin_add_overflow(a, b, &out);
}

bool sub_overflows(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept {
  return __builtin_sub_overflow(a, b, &out);
}

bool mul_overflows(std::int64_t a, std::int64_t b, std::int64_t& out) noexcept {
  return __builtin_mul_overflow(a, b, &out);
}

Error mismatch(const Money& lhs, const Money& rhs) {
  return make_error(ErrorCode::CurrencyMismatch,
                    "currency mismatch: " + lhs.currency().str() + " vs " + rhs.currency().str());
}

Error invalid(std::string_view text, std::string_view why) {
  return make_error(ErrorCode::InvalidAmount,
                    "invalid amount '" + std::string{text} + "': " + std::string{why});
}

}  // namespace

Result<Money> Money::parse(std::string_view text, CurrencyCode currency) {
  if (text.empty()) {
    return invalid(text, "empty");
  }
  std::size_t pos = 0;
  bool negative = false;
  if (text[pos] == '-' || text[pos] == '+') {
    negative = text[pos] == '-';
    ++pos;
  }
  if (pos >= text.size()) {
    return invalid(text, "no digits");
  }

  std::int64_t major = 0;
  std::size_t major_digits = 0;
  while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
    const auto digit = static_cast<std::int64_t>(text[pos] - '0');
    std::int64_t scaled = 0;
    if (mul_overflows(major, 10, scaled) || add_overflows(scaled, digit, major)) {
      return invalid(text, "integer part overflows");
    }
    ++pos;
    ++major_digits;
  }
  if (major_digits == 0) {
    return invalid(text, "integer part required");
  }

  std::int64_t fraction = 0;
  std::size_t fraction_digits = 0;
  if (pos < text.size() && text[pos] == '.') {
    ++pos;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
      fraction = fraction * 10 + static_cast<std::int64_t>(text[pos] - '0');
      ++pos;
      ++fraction_digits;
      if (fraction_digits > static_cast<std::size_t>(currency.minor_units())) {
        return invalid(text, "more decimal places than " + currency.str() + " allows (" +
                                 std::to_string(currency.minor_units()) + ")");
      }
    }
    if (fraction_digits == 0) {
      return invalid(text, "decimal point without digits");
    }
  }
  if (pos != text.size()) {
    return invalid(text, "unexpected character '" + std::string(1, text[pos]) + "'");
  }
  for (std::size_t i = fraction_digits; i < static_cast<std::size_t>(currency.minor_units()); ++i) {
    fraction *= 10;
  }

  std::int64_t minor = 0;
  if (mul_overflows(major, currency.minor_per_major(), minor) ||
      add_overflows(minor, fraction, minor)) {
    return invalid(text, "amount overflows 64-bit minor units");
  }
  if (negative) {
    minor = -minor;
  }
  return Money{minor, currency};
}

Result<Money> Money::add(const Money& other) const {
  if (!same_currency(other)) {
    return mismatch(*this, other);
  }
  std::int64_t out = 0;
  if (add_overflows(minor_, other.minor_, out)) {
    return make_error(ErrorCode::Overflow, "money addition overflows");
  }
  return Money{out, currency_};
}

Result<Money> Money::subtract(const Money& other) const {
  if (!same_currency(other)) {
    return mismatch(*this, other);
  }
  std::int64_t out = 0;
  if (sub_overflows(minor_, other.minor_, out)) {
    return make_error(ErrorCode::Overflow, "money subtraction overflows");
  }
  return Money{out, currency_};
}

Result<Money> Money::negated() const {
  if (minor_ == std::numeric_limits<std::int64_t>::min()) {
    return make_error(ErrorCode::Overflow, "money negation overflows");
  }
  return Money{-minor_, currency_};
}

Result<Money> Money::multiply(std::int64_t factor) const {
  std::int64_t out = 0;
  if (mul_overflows(minor_, factor, out)) {
    return make_error(ErrorCode::Overflow, "money multiplication overflows");
  }
  return Money{out, currency_};
}

Result<std::strong_ordering> Money::compare(const Money& other) const {
  if (!same_currency(other)) {
    return mismatch(*this, other);
  }
  return minor_ <=> other.minor_;
}

std::string Money::format_amount() const {
  const std::int64_t scale = currency_.minor_per_major();
  // Work on the magnitude as unsigned so that INT64_MIN formats correctly.
  const bool negative = minor_ < 0;
  const auto magnitude = negative ? (0ULL - static_cast<unsigned long long>(minor_))
                                  : static_cast<unsigned long long>(minor_);
  const auto uscale = static_cast<unsigned long long>(scale);
  std::string out = negative ? "-" : "";
  out += std::to_string(magnitude / uscale);
  if (currency_.minor_units() > 0) {
    std::string frac = std::to_string(magnitude % uscale);
    while (frac.size() < static_cast<std::size_t>(currency_.minor_units())) {
      frac.insert(frac.begin(), '0');
    }
    out += '.';
    out += frac;
  }
  return out;
}

std::string Money::to_string() const {
  return format_amount() + " " + currency_.str();
}

}  // namespace corebank
