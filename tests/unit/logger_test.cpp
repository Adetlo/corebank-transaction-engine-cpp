#include "corebank/infrastructure/logger.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

namespace corebank {
namespace {

class LoggerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Logger::instance().set_level(LogLevel::Debug);
    Logger::instance().set_sink([this](std::string_view line) { lines.emplace_back(line); });
  }
  void TearDown() override {
    Logger::instance().reset_sink();
    Logger::instance().set_level(LogLevel::Info);
  }
  std::vector<std::string> lines;
};

TEST_F(LoggerTest, EmitsJsonLineWithStandardFields) {
  Logger::instance().info("transaction.posted", {{"transaction_id", "TXN-1"},
                                                 {"account_id", "ACC-1"},
                                                 {"duration_ms", 1.25},
                                                 {"legs", 2},
                                                 {"replayed", false}});
  ASSERT_EQ(lines.size(), 1U);
  const auto& line = lines.front();
  EXPECT_NE(line.find("\"ts\":\""), std::string::npos);
  EXPECT_NE(line.find("\"level\":\"INFO\""), std::string::npos);
  EXPECT_NE(line.find("\"event\":\"transaction.posted\""), std::string::npos);
  EXPECT_NE(line.find("\"transaction_id\":\"TXN-1\""), std::string::npos);
  EXPECT_NE(line.find("\"account_id\":\"ACC-1\""), std::string::npos);
  EXPECT_NE(line.find("\"duration_ms\":1.250"), std::string::npos);
  EXPECT_NE(line.find("\"legs\":2"), std::string::npos);
  EXPECT_NE(line.find("\"replayed\":false"), std::string::npos);
  EXPECT_EQ(line.front(), '{');
  EXPECT_EQ(line.back(), '}');
}

TEST_F(LoggerTest, FiltersBelowLevel) {
  Logger::instance().set_level(LogLevel::Warn);
  Logger::instance().debug("d");
  Logger::instance().info("i");
  Logger::instance().warn("w");
  Logger::instance().error("e");
  ASSERT_EQ(lines.size(), 2U);
  EXPECT_NE(lines[0].find("\"level\":\"WARN\""), std::string::npos);
  EXPECT_NE(lines[1].find("\"level\":\"ERROR\""), std::string::npos);
}

TEST_F(LoggerTest, EscapesQuotesAndControlCharacters) {
  Logger::instance().info("x", {{"reason", "said \"hi\"\n"}});
  EXPECT_NE(lines.front().find("\"reason\":\"said \\\"hi\\\"\\n\""), std::string::npos);
}

TEST(LogLevel, ParsesNames) {
  EXPECT_EQ(parse_log_level("DEBUG").value(), LogLevel::Debug);
  EXPECT_EQ(parse_log_level("ERROR").value(), LogLevel::Error);
  EXPECT_FALSE(parse_log_level("verbose").has_value());
}

}  // namespace
}  // namespace corebank
