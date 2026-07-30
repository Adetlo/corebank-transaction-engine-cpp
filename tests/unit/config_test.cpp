#include "corebank/infrastructure/config.hpp"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace corebank {
namespace {

Config::EnvReader fake_env(std::map<std::string, std::string> values) {
  return [values = std::move(values)](std::string_view name) -> std::optional<std::string> {
    const auto it = values.find(std::string{name});
    if (it == values.end()) {
      return std::nullopt;
    }
    return it->second;
  };
}

TEST(Config, DefaultsAreSafe) {
  const Config config;
  EXPECT_EQ(config.listen_address, "127.0.0.1");
  EXPECT_EQ(config.worker_count, 4U);
  EXPECT_EQ(config.max_message_bytes, 4096U);
  EXPECT_FALSE(config.fail_stage.has_value());
  EXPECT_TRUE(config.validate().has_value());
}

TEST(Config, ParsesFlatYaml) {
  const auto parsed = Config::parse_yaml(R"(
# comment
database_path: /srv/corebank/engine.db
worker_count: 8   # inline comment
log_level: DEBUG
listen_port: "7500"
batch_dir: '/srv/corebank/batch'
)");
  ASSERT_TRUE(parsed.has_value()) << parsed.error().message;
  EXPECT_EQ(parsed.value().database_path, "/srv/corebank/engine.db");
  EXPECT_EQ(parsed.value().worker_count, 8U);
  EXPECT_EQ(parsed.value().log_level, LogLevel::Debug);
  EXPECT_EQ(parsed.value().listen_port, 7500);
  EXPECT_EQ(parsed.value().batch_dir, "/srv/corebank/batch");
}

TEST(Config, RejectsUnknownKeysNestedBlocksAndBadValues) {
  EXPECT_EQ(Config::parse_yaml("unknown_key: 1").error().code, ErrorCode::ConfigurationError);
  EXPECT_EQ(Config::parse_yaml("server:\n  port: 1").error().code, ErrorCode::ConfigurationError);
  EXPECT_EQ(Config::parse_yaml("worker_count: many").error().code, ErrorCode::ConfigurationError);
  EXPECT_EQ(Config::parse_yaml("worker_count: 0").error().code, ErrorCode::ConfigurationError);
  EXPECT_EQ(Config::parse_yaml("listen_port: 70000").error().code, ErrorCode::ConfigurationError);
  EXPECT_EQ(Config::parse_yaml("log_level: LOUD").error().code, ErrorCode::ConfigurationError);
  EXPECT_EQ(Config::parse_yaml("no colon here").error().code, ErrorCode::ConfigurationError);
}

TEST(Config, EnvironmentOverridesFile) {
  Config config = Config::parse_yaml("worker_count: 2\ndatabase_path: file.db").value();
  ASSERT_TRUE(config
                  .apply_env(fake_env({{"COREBANK_WORKERS", "6"},
                                       {"COREBANK_FAIL_STAGE", "RECONCILIATION"},
                                       {"COREBANK_PORT", "7401"}}))
                  .has_value());
  EXPECT_EQ(config.worker_count, 6U);
  EXPECT_EQ(config.database_path, "file.db");
  EXPECT_EQ(config.fail_stage.value(), "RECONCILIATION");
  EXPECT_EQ(config.listen_port, 7401);
  EXPECT_EQ(config.apply_env(fake_env({{"COREBANK_QUEUE_SIZE", "-5"}})).error().code,
            ErrorCode::ConfigurationError);
}

TEST(Config, LoadFailsForMissingFile) {
  const auto loaded = Config::load(std::string{"/nonexistent/corebank.yaml"}, fake_env({}));
  ASSERT_FALSE(loaded.has_value());
  EXPECT_EQ(loaded.error().code, ErrorCode::ConfigurationError);
}

TEST(Config, DescribeAndJsonListEveryKey) {
  const Config config;
  const auto text = config.describe();
  for (const auto* key : {"database_path", "worker_count", "queue_size", "log_level", "batch_dir",
                          "listen_port", "max_connections", "max_message_bytes"}) {
    EXPECT_NE(text.find(key), std::string::npos) << key;
    EXPECT_NE(config.to_json().find(key), std::string::npos) << key;
  }
}

}  // namespace
}  // namespace corebank
