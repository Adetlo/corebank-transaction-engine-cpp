// Minimal expected-like Result<T> for C++20 (std::expected is C++23).
#pragma once

#include "corebank/domain/error.hpp"

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace corebank {

template <typename T>
class [[nodiscard]] Result {
 public:
  using value_type = T;

  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  Result(T value) : storage_(std::in_place_index<0>, std::move(value)) {}
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  Result(Error error) : storage_(std::in_place_index<1>, std::move(error)) {}

  [[nodiscard]] bool has_value() const noexcept { return storage_.index() == 0; }
  [[nodiscard]] bool has_error() const noexcept { return !has_value(); }
  explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] T& value() & { return std::get<0>(storage_); }
  [[nodiscard]] const T& value() const& { return std::get<0>(storage_); }
  [[nodiscard]] T&& value() && { return std::get<0>(std::move(storage_)); }

  [[nodiscard]] const Error& error() const& { return std::get<1>(storage_); }
  [[nodiscard]] Error&& error() && { return std::get<1>(std::move(storage_)); }

  [[nodiscard]] T value_or(T fallback) const& {
    return has_value() ? value() : std::move(fallback);
  }

  template <typename F>
  [[nodiscard]] auto and_then(F&& fn) const& -> std::invoke_result_t<F, const T&> {
    if (has_value()) {
      return std::forward<F>(fn)(value());
    }
    return error();
  }

  template <typename F>
  [[nodiscard]] auto map(F&& fn) const& -> Result<std::invoke_result_t<F, const T&>> {
    if (has_value()) {
      return std::forward<F>(fn)(value());
    }
    return error();
  }

 private:
  std::variant<T, Error> storage_;
};

template <>
class [[nodiscard]] Result<void> {
 public:
  Result() = default;
  // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
  Result(Error error) : error_(std::move(error)) {}

  [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }
  [[nodiscard]] bool has_error() const noexcept { return error_.has_value(); }
  explicit operator bool() const noexcept { return has_value(); }

  [[nodiscard]] const Error& error() const& { return *error_; }
  [[nodiscard]] Error&& error() && { return std::move(*error_); }

 private:
  std::optional<Error> error_;
};

[[nodiscard]] inline Result<void> ok() {
  return {};
}

}  // namespace corebank
