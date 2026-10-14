// Structured JSON-lines logger: one object per line, thread-safe, level-filtered.
#pragma once

#include "corebank/domain/result.hpp"

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>

namespace corebank {

enum class LogLevel { Debug, Info, Warn, Error };

[[nodiscard]] std::string_view to_string(LogLevel level) noexcept;
[[nodiscard]] Result<LogLevel> parse_log_level(std::string_view text);

struct LogField {
  std::string key;
  std::variant<std::string, std::int64_t, double, bool> value;

  LogField(std::string_view k, std::string_view v) : key(k), value(std::string{v}) {}
  LogField(std::string_view k, const std::string& v) : key(k), value(v) {}
  LogField(std::string_view k, const char* v) : key(k), value(std::string{v}) {}
  LogField(std::string_view k, std::int64_t v) : key(k), value(v) {}
  LogField(std::string_view k, int v) : key(k), value(static_cast<std::int64_t>(v)) {}
  LogField(std::string_view k, std::uint64_t v) : key(k), value(static_cast<std::int64_t>(v)) {}
  LogField(std::string_view k, unsigned v) : key(k), value(static_cast<std::int64_t>(v)) {}
  LogField(std::string_view k, double v) : key(k), value(v) {}
  LogField(std::string_view k, bool v) : key(k), value(v) {}
};

class Logger {
 public:
  using Sink = std::function<void(std::string_view line)>;

  [[nodiscard]] static Logger& instance();

  void set_level(LogLevel level);
  [[nodiscard]] LogLevel level() const;
  // Default sink writes to stderr. A file sink appends to `path`.
  [[nodiscard]] Result<void> set_file(const std::string& path);
  void set_sink(Sink sink);
  void reset_sink();

  void log(LogLevel level, std::string_view event, std::initializer_list<LogField> fields);
  void debug(std::string_view event, std::initializer_list<LogField> fields = {}) {
    log(LogLevel::Debug, event, fields);
  }
  void info(std::string_view event, std::initializer_list<LogField> fields = {}) {
    log(LogLevel::Info, event, fields);
  }
  void warn(std::string_view event, std::initializer_list<LogField> fields = {}) {
    log(LogLevel::Warn, event, fields);
  }
  void error(std::string_view event, std::initializer_list<LogField> fields = {}) {
    log(LogLevel::Error, event, fields);
  }

  [[nodiscard]] static std::string format_line(LogLevel level, std::string_view event,
                                               std::initializer_list<LogField> fields);

 private:
  Logger() = default;

  mutable std::mutex mutex_;
  LogLevel level_{LogLevel::Info};
  Sink sink_;
  std::unique_ptr<std::ostream> file_;
};

}  // namespace corebank
