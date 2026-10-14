#include "corebank/infrastructure/logger.hpp"

#include "corebank/domain/time.hpp"
#include "corebank/infrastructure/json_writer.hpp"

#include <fstream>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace corebank {

std::string_view to_string(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "INFO";
}

Result<LogLevel> parse_log_level(std::string_view text) {
  for (const auto level : {LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error}) {
    if (to_string(level) == text) {
      return level;
    }
  }
  return make_error(ErrorCode::ConfigurationError,
                    "unknown log level '" + std::string{text} + "' (DEBUG, INFO, WARN, ERROR)");
}

Logger& Logger::instance() {
  static Logger logger;
  return logger;
}

void Logger::set_level(LogLevel level) {
  const std::lock_guard lock(mutex_);
  level_ = level;
}

LogLevel Logger::level() const {
  const std::lock_guard lock(mutex_);
  return level_;
}

Result<void> Logger::set_file(const std::string& path) {
  auto stream = std::make_unique<std::ofstream>(path, std::ios::app);
  if (!stream->is_open()) {
    return make_error(ErrorCode::ConfigurationError, "cannot open log file '" + path + "'");
  }
  const std::lock_guard lock(mutex_);
  file_ = std::move(stream);
  sink_ = nullptr;
  return ok();
}

void Logger::set_sink(Sink sink) {
  const std::lock_guard lock(mutex_);
  sink_ = std::move(sink);
}

void Logger::reset_sink() {
  const std::lock_guard lock(mutex_);
  sink_ = nullptr;
  file_.reset();
}

std::string Logger::format_line(LogLevel level, std::string_view event,
                                std::initializer_list<LogField> fields) {
  JsonWriter writer;
  writer.string("ts", format_timestamp(SystemClock{}.now()))
      .string("level", to_string(level))
      .string("event", event);
  for (const auto& field : fields) {
    std::visit(
        [&](const auto& value) {
          using V = std::decay_t<decltype(value)>;
          if constexpr (std::is_same_v<V, std::string>) {
            writer.string(field.key, value);
          } else if constexpr (std::is_same_v<V, bool>) {
            writer.boolean(field.key, value);
          } else {
            writer.number(field.key, value);
          }
        },
        field.value);
  }
  return writer.str();
}

void Logger::log(LogLevel level, std::string_view event, std::initializer_list<LogField> fields) {
  const std::lock_guard lock(mutex_);
  if (level < level_) {
    return;
  }
  const std::string line = format_line(level, event, fields);
  if (sink_) {
    sink_(line);
  } else if (file_) {
    *file_ << line << '\n';
    file_->flush();
  } else {
    std::cerr << line << '\n';
  }
}

}  // namespace corebank
