// Tiny JSON emitter (objects and arrays, escaped strings). No parsing, no dependencies.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace corebank {

[[nodiscard]] std::string json_escape(std::string_view text);

class JsonWriter {
 public:
  JsonWriter& string(std::string_view name, std::string_view value);
  JsonWriter& number(std::string_view name, std::int64_t value);
  JsonWriter& number(std::string_view name, double value);
  template <typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool> && !std::is_same_v<T, std::int64_t>)
  JsonWriter& number(std::string_view name, T value) {
    return number(name, static_cast<std::int64_t>(value));
  }
  JsonWriter& boolean(std::string_view name, bool value);
  JsonWriter& null(std::string_view name);
  // Embeds pre-serialised JSON (nested object or array) verbatim.
  JsonWriter& raw(std::string_view name, std::string_view json);

  [[nodiscard]] std::string str() const { return "{" + body_ + "}"; }
  [[nodiscard]] bool empty() const noexcept { return body_.empty(); }

 private:
  void key(std::string_view name);
  std::string body_;
};

class JsonArrayWriter {
 public:
  JsonArrayWriter& string(std::string_view value);
  JsonArrayWriter& raw(std::string_view json);
  [[nodiscard]] std::string str() const { return "[" + body_ + "]"; }

 private:
  void separator();
  std::string body_;
};

[[nodiscard]] std::string format_double(double value);

}  // namespace corebank
