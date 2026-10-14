// Engine configuration: flat YAML file (key: value) overridden by COREBANK_* variables.
#pragma once

#include "corebank/domain/result.hpp"
#include "corebank/infrastructure/logger.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace corebank {

struct Config {
  std::string database_path{"var/corebank.db"};
  std::size_t worker_count{4};
  std::size_t queue_size{1024};
  LogLevel log_level{LogLevel::Info};
  std::string log_file;  // empty: stderr
  std::string batch_dir{"var/batch"};
  std::int64_t max_transaction_minor{100'000'000'000};  // 1,000,000,000.00 at 2dp
  std::string listen_address{"127.0.0.1"};
  std::uint16_t listen_port{7400};
  std::size_t max_connections{64};
  int idle_timeout_seconds{30};
  std::size_t max_message_bytes{4096};
  int request_timeout_ms{5000};
  std::optional<std::string> fail_stage;     // COREBANK_FAIL_STAGE (testing only)
  std::optional<std::string> business_date;  // COREBANK_BUSINESS_DATE override

  using EnvReader = std::function<std::optional<std::string>(std::string_view name)>;

  // Loads defaults, then the file (if given), then environment overrides.
  [[nodiscard]] static Result<Config> load(const std::optional<std::string>& file_path,
                                           const EnvReader& env = system_env());
  [[nodiscard]] static Result<Config> parse_yaml(std::string_view text);
  [[nodiscard]] Result<void> apply_env(const EnvReader& env);
  [[nodiscard]] Result<void> validate() const;
  [[nodiscard]] static EnvReader system_env();

  // Human-readable dump with one key per line (no secrets exist in this config).
  [[nodiscard]] std::string describe() const;
  [[nodiscard]] std::string to_json() const;

 private:
  [[nodiscard]] Result<void> set(std::string_view key, std::string_view value);
};

}  // namespace corebank
