#include "corebank/infrastructure/config.hpp"

#include "corebank/infrastructure/json_writer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace corebank {
namespace {

std::string trim(std::string_view text) {
  const auto begin = text.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = text.find_last_not_of(" \t\r\n");
  return std::string{text.substr(begin, end - begin + 1)};
}

std::string strip_quotes(const std::string& value) {
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') ||
                            (value.front() == '\'' && value.back() == '\''))) {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

Result<std::int64_t> parse_int(std::string_view key, std::string_view value, std::int64_t min,
                               std::int64_t max) {
  if (value.empty()) {
    return make_error(ErrorCode::ConfigurationError, std::string{key} + " must be a number");
  }
  std::int64_t out = 0;
  std::size_t i = 0;
  bool negative = false;
  if (value[0] == '-') {
    negative = true;
    i = 1;
  }
  if (i >= value.size()) {
    return make_error(ErrorCode::ConfigurationError, std::string{key} + " must be a number");
  }
  for (; i < value.size(); ++i) {
    if (std::isdigit(static_cast<unsigned char>(value[i])) == 0) {
      return make_error(ErrorCode::ConfigurationError, std::string{key} + " must be a number");
    }
    if (out > (std::numeric_limits<std::int64_t>::max() - 9) / 10) {
      return make_error(ErrorCode::ConfigurationError, std::string{key} + " is out of range");
    }
    out = out * 10 + (value[i] - '0');
  }
  if (negative) {
    out = -out;
  }
  if (out < min || out > max) {
    return make_error(ErrorCode::ConfigurationError, std::string{key} + " must be between " +
                                                         std::to_string(min) + " and " +
                                                         std::to_string(max));
  }
  return out;
}

}  // namespace

Config::EnvReader Config::system_env() {
  return [](std::string_view name) -> std::optional<std::string> {
    const std::string key{name};
    // NOLINTNEXTLINE(concurrency-mt-unsafe) getenv is only called during start-up
    const char* value = std::getenv(key.c_str());
    if (value == nullptr) {
      return std::nullopt;
    }
    return std::string{value};
  };
}

Result<void> Config::set(std::string_view key, std::string_view raw_value) {
  const std::string value = strip_quotes(trim(raw_value));
  if (key == "database_path") {
    database_path = value;
  } else if (key == "worker_count") {
    auto parsed = parse_int(key, value, 1, 256);
    if (!parsed) {
      return parsed.error();
    }
    worker_count = static_cast<std::size_t>(parsed.value());
  } else if (key == "queue_size") {
    auto parsed = parse_int(key, value, 1, 1'000'000);
    if (!parsed) {
      return parsed.error();
    }
    queue_size = static_cast<std::size_t>(parsed.value());
  } else if (key == "log_level") {
    auto parsed = parse_log_level(value);
    if (!parsed) {
      return parsed.error();
    }
    log_level = parsed.value();
  } else if (key == "log_file") {
    log_file = value;
  } else if (key == "batch_dir") {
    batch_dir = value;
  } else if (key == "max_transaction_minor") {
    auto parsed = parse_int(key, value, 1, std::numeric_limits<std::int64_t>::max());
    if (!parsed) {
      return parsed.error();
    }
    max_transaction_minor = parsed.value();
  } else if (key == "listen_address") {
    listen_address = value;
  } else if (key == "listen_port") {
    auto parsed = parse_int(key, value, 1, 65535);
    if (!parsed) {
      return parsed.error();
    }
    listen_port = static_cast<std::uint16_t>(parsed.value());
  } else if (key == "max_connections") {
    auto parsed = parse_int(key, value, 1, 10'000);
    if (!parsed) {
      return parsed.error();
    }
    max_connections = static_cast<std::size_t>(parsed.value());
  } else if (key == "idle_timeout_seconds") {
    auto parsed = parse_int(key, value, 1, 86'400);
    if (!parsed) {
      return parsed.error();
    }
    idle_timeout_seconds = static_cast<int>(parsed.value());
  } else if (key == "max_message_bytes") {
    auto parsed = parse_int(key, value, 64, 1'048'576);
    if (!parsed) {
      return parsed.error();
    }
    max_message_bytes = static_cast<std::size_t>(parsed.value());
  } else if (key == "request_timeout_ms") {
    auto parsed = parse_int(key, value, 10, 600'000);
    if (!parsed) {
      return parsed.error();
    }
    request_timeout_ms = static_cast<int>(parsed.value());
  } else if (key == "fail_stage") {
    fail_stage = value.empty() ? std::nullopt : std::optional<std::string>{value};
  } else if (key == "business_date") {
    business_date = value.empty() ? std::nullopt : std::optional<std::string>{value};
  } else {
    return make_error(ErrorCode::ConfigurationError,
                      "unknown configuration key '" + std::string{key} + "'");
  }
  return ok();
}

Result<Config> Config::parse_yaml(std::string_view text) {
  Config config;
  std::istringstream stream{std::string{text}};
  std::string line;
  int line_number = 0;
  while (std::getline(stream, line)) {
    ++line_number;
    const std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed.front() == '#' || trimmed == "---") {
      continue;
    }
    if (line.front() == ' ' || line.front() == '\t') {
      return make_error(ErrorCode::ConfigurationError,
                        "line " + std::to_string(line_number) +
                            ": nested keys are not supported (flat key: value only)");
    }
    const auto colon = trimmed.find(':');
    if (colon == std::string::npos) {
      return make_error(ErrorCode::ConfigurationError,
                        "line " + std::to_string(line_number) + ": expected 'key: value'");
    }
    std::string value = trimmed.substr(colon + 1);
    if (const auto hash = value.find(" #"); hash != std::string::npos) {
      value.resize(hash);
    }
    if (auto applied = config.set(trim(trimmed.substr(0, colon)), value); !applied) {
      return make_error(ErrorCode::ConfigurationError,
                        "line " + std::to_string(line_number) + ": " + applied.error().message);
    }
  }
  return config;
}

Result<void> Config::apply_env(const EnvReader& env) {
  struct Mapping {
    std::string_view env_name;
    std::string_view key;
  };
  static constexpr std::array<Mapping, 15> kMappings{{
      {"COREBANK_DB", "database_path"},
      {"COREBANK_WORKERS", "worker_count"},
      {"COREBANK_QUEUE_SIZE", "queue_size"},
      {"COREBANK_LOG_LEVEL", "log_level"},
      {"COREBANK_LOG_FILE", "log_file"},
      {"COREBANK_BATCH_DIR", "batch_dir"},
      {"COREBANK_MAX_TRANSACTION_MINOR", "max_transaction_minor"},
      {"COREBANK_LISTEN_ADDRESS", "listen_address"},
      {"COREBANK_PORT", "listen_port"},
      {"COREBANK_MAX_CONNECTIONS", "max_connections"},
      {"COREBANK_IDLE_TIMEOUT_SECONDS", "idle_timeout_seconds"},
      {"COREBANK_MAX_MESSAGE_BYTES", "max_message_bytes"},
      {"COREBANK_REQUEST_TIMEOUT_MS", "request_timeout_ms"},
      {"COREBANK_FAIL_STAGE", "fail_stage"},
      {"COREBANK_BUSINESS_DATE", "business_date"},
  }};
  for (const auto& mapping : kMappings) {
    if (const auto value = env(mapping.env_name)) {
      if (auto applied = set(mapping.key, *value); !applied) {
        return make_error(ErrorCode::ConfigurationError,
                          std::string{mapping.env_name} + ": " + applied.error().message);
      }
    }
  }
  return ok();
}

Result<void> Config::validate() const {
  if (database_path.empty()) {
    return make_error(ErrorCode::ConfigurationError, "database_path must not be empty");
  }
  if (batch_dir.empty()) {
    return make_error(ErrorCode::ConfigurationError, "batch_dir must not be empty");
  }
  if (listen_address.empty()) {
    return make_error(ErrorCode::ConfigurationError, "listen_address must not be empty");
  }
  return ok();
}

Result<Config> Config::load(const std::optional<std::string>& file_path, const EnvReader& env) {
  Config config;
  if (file_path) {
    std::ifstream file{*file_path};
    if (!file.is_open()) {
      return make_error(ErrorCode::ConfigurationError,
                        "cannot read configuration file '" + *file_path + "'");
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    auto parsed = parse_yaml(buffer.str());
    if (!parsed) {
      return make_error(ErrorCode::ConfigurationError, *file_path + ": " + parsed.error().message);
    }
    config = parsed.value();
  }
  if (auto applied = config.apply_env(env); !applied) {
    return applied.error();
  }
  if (auto valid = config.validate(); !valid) {
    return valid.error();
  }
  return config;
}

std::string Config::describe() const {
  std::string out;
  out += "database_path: " + database_path + "\n";
  out += "worker_count: " + std::to_string(worker_count) + "\n";
  out += "queue_size: " + std::to_string(queue_size) + "\n";
  out += "log_level: " + std::string{to_string(log_level)} + "\n";
  out += "log_file: " + (log_file.empty() ? std::string{"(stderr)"} : log_file) + "\n";
  out += "batch_dir: " + batch_dir + "\n";
  out += "max_transaction_minor: " + std::to_string(max_transaction_minor) + "\n";
  out += "listen_address: " + listen_address + "\n";
  out += "listen_port: " + std::to_string(listen_port) + "\n";
  out += "max_connections: " + std::to_string(max_connections) + "\n";
  out += "idle_timeout_seconds: " + std::to_string(idle_timeout_seconds) + "\n";
  out += "max_message_bytes: " + std::to_string(max_message_bytes) + "\n";
  out += "request_timeout_ms: " + std::to_string(request_timeout_ms) + "\n";
  out += "fail_stage: " + fail_stage.value_or("(none)") + "\n";
  out += "business_date: " + business_date.value_or("(clock)") + "\n";
  return out;
}

std::string Config::to_json() const {
  JsonWriter writer;
  writer.string("database_path", database_path)
      .number("worker_count", static_cast<std::int64_t>(worker_count))
      .number("queue_size", static_cast<std::int64_t>(queue_size))
      .string("log_level", to_string(log_level))
      .string("log_file", log_file)
      .string("batch_dir", batch_dir)
      .number("max_transaction_minor", max_transaction_minor)
      .string("listen_address", listen_address)
      .number("listen_port", static_cast<std::int64_t>(listen_port))
      .number("max_connections", static_cast<std::int64_t>(max_connections))
      .number("idle_timeout_seconds", static_cast<std::int64_t>(idle_timeout_seconds))
      .number("max_message_bytes", static_cast<std::int64_t>(max_message_bytes))
      .number("request_timeout_ms", static_cast<std::int64_t>(request_timeout_ms));
  if (fail_stage) {
    writer.string("fail_stage", *fail_stage);
  }
  if (business_date) {
    writer.string("business_date", *business_date);
  }
  return writer.str();
}

}  // namespace corebank
