#include "corebank/domain/time.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <ctime>
#include <string>
#include <string_view>

namespace corebank {
namespace {

bool all_digits(std::string_view text) {
  return !text.empty() && std::all_of(text.begin(), text.end(), [](char ch) {
    return std::isdigit(static_cast<unsigned char>(ch)) != 0;
  });
}

int to_int(std::string_view digits) {
  int value = 0;
  for (const char ch : digits) {
    value = value * 10 + (ch - '0');
  }
  return value;
}

bool valid_date_parts(int year, int month, int day) {
  if (year < 1970 || year > 9999 || month < 1 || month > 12 || day < 1) {
    return false;
  }
  const std::chrono::year_month_day ymd{std::chrono::year{year},
                                        std::chrono::month{static_cast<unsigned>(month)},
                                        std::chrono::day{static_cast<unsigned>(day)}};
  return ymd.ok();
}

}  // namespace

std::string format_timestamp(Timestamp ts) {
  const auto secs = std::chrono::floor<std::chrono::seconds>(ts);
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(ts - secs).count();
  const std::time_t raw = std::chrono::system_clock::to_time_t(
      std::chrono::time_point_cast<std::chrono::system_clock::duration>(secs));
  std::tm parts{};
  gmtime_r(&raw, &parts);
  std::array<char, 40> buffer{};
  const int written =
      std::snprintf(buffer.data(), buffer.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%06lldZ",
                    parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour,
                    parts.tm_min, parts.tm_sec, static_cast<long long>(micros));
  if (written <= 0) {
    return {};
  }
  return {buffer.data(), static_cast<std::size_t>(written)};
}

Result<Timestamp> parse_timestamp(std::string_view text) {
  // Accepts YYYY-MM-DDTHH:MM:SS[.ffffff]Z
  const auto bad = [&]() {
    return make_error(ErrorCode::ValidationError,
                      "invalid timestamp '" + std::string{text} + "' (expected ISO-8601 UTC)");
  };
  if (text.size() < 20 || text[4] != '-' || text[7] != '-' || text[10] != 'T' || text[13] != ':' ||
      text[16] != ':' || text.back() != 'Z') {
    return bad();
  }
  const auto year = text.substr(0, 4);
  const auto month = text.substr(5, 2);
  const auto day = text.substr(8, 2);
  const auto hour = text.substr(11, 2);
  const auto minute = text.substr(14, 2);
  const auto second = text.substr(17, 2);
  if (!all_digits(year) || !all_digits(month) || !all_digits(day) || !all_digits(hour) ||
      !all_digits(minute) || !all_digits(second)) {
    return bad();
  }
  long long micros = 0;
  if (text.size() > 20) {
    if (text[19] != '.') {
      return bad();
    }
    const auto frac = text.substr(20, text.size() - 21);
    if (frac.empty() || frac.size() > 6 || !all_digits(frac)) {
      return bad();
    }
    micros = to_int(frac);
    for (std::size_t i = frac.size(); i < 6; ++i) {
      micros *= 10;
    }
  } else if (text.size() != 20) {
    return bad();
  }
  const int y = to_int(year);
  const int mo = to_int(month);
  const int d = to_int(day);
  const int h = to_int(hour);
  const int mi = to_int(minute);
  const int s = to_int(second);
  if (!valid_date_parts(y, mo, d) || h > 23 || mi > 59 || s > 60) {
    return bad();
  }
  std::tm parts{};
  parts.tm_year = y - 1900;
  parts.tm_mon = mo - 1;
  parts.tm_mday = d;
  parts.tm_hour = h;
  parts.tm_min = mi;
  parts.tm_sec = s;
  const std::time_t raw = timegm(&parts);
  const auto base = std::chrono::time_point_cast<std::chrono::microseconds>(
      std::chrono::system_clock::from_time_t(raw));
  return Timestamp{base + std::chrono::microseconds{micros}};
}

Result<BusinessDate> BusinessDate::parse(std::string_view text) {
  if (text.size() != 10 || text[4] != '-' || text[7] != '-' || !all_digits(text.substr(0, 4)) ||
      !all_digits(text.substr(5, 2)) || !all_digits(text.substr(8, 2))) {
    return make_error(ErrorCode::ValidationError,
                      "invalid business date '" + std::string{text} + "' (expected YYYY-MM-DD)");
  }
  if (!valid_date_parts(to_int(text.substr(0, 4)), to_int(text.substr(5, 2)),
                        to_int(text.substr(8, 2)))) {
    return make_error(ErrorCode::ValidationError,
                      "invalid business date '" + std::string{text} + "' (not a calendar day)");
  }
  return BusinessDate(std::string{text});
}

BusinessDate BusinessDate::from_timestamp(Timestamp ts) {
  return BusinessDate(format_timestamp(ts).substr(0, 10));
}

Timestamp SystemClock::now() const {
  return std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::system_clock::now());
}

}  // namespace corebank
