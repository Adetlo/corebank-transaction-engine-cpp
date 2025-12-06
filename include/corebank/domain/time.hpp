// Timestamps and business dates. All times are UTC.
#pragma once

#include "corebank/domain/result.hpp"

#include <chrono>
#include <compare>
#include <string>
#include <string_view>
#include <utility>

namespace corebank {

using Timestamp = std::chrono::sys_time<std::chrono::microseconds>;

// ISO-8601 "YYYY-MM-DDTHH:MM:SS.ffffffZ".
[[nodiscard]] std::string format_timestamp(Timestamp ts);
[[nodiscard]] Result<Timestamp> parse_timestamp(std::string_view text);

// Calendar day a transaction belongs to for batch purposes ("YYYY-MM-DD").
class BusinessDate {
 public:
  [[nodiscard]] static Result<BusinessDate> parse(std::string_view text);
  [[nodiscard]] static BusinessDate from_timestamp(Timestamp ts);

  [[nodiscard]] const std::string& str() const noexcept { return iso_; }
  [[nodiscard]] bool operator==(const BusinessDate& other) const noexcept = default;
  [[nodiscard]] std::strong_ordering operator<=>(const BusinessDate& other) const noexcept =
      default;

 private:
  explicit BusinessDate(std::string iso) : iso_(std::move(iso)) {}
  std::string iso_;
};

// Abstract clock so that tests can freeze time.
class Clock {
 public:
  Clock() = default;
  Clock(const Clock&) = delete;
  Clock& operator=(const Clock&) = delete;
  Clock(Clock&&) = delete;
  Clock& operator=(Clock&&) = delete;
  virtual ~Clock() = default;
  [[nodiscard]] virtual Timestamp now() const = 0;
};

class SystemClock final : public Clock {
 public:
  [[nodiscard]] Timestamp now() const override;
};

}  // namespace corebank
