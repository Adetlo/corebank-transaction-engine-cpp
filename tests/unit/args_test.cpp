#include "corebank/cli/args.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace corebank::cli {
namespace {

Args parse(std::vector<const char*> argv, std::vector<std::string> flags = {"json"}) {
  argv.insert(argv.begin(), "corebank");
  return Args::parse(static_cast<int>(argv.size()), argv.data(), flags).value();
}

TEST(Args, SeparatesPositionalsOptionsAndFlags) {
  const auto args = parse({"account", "create", "--id", "ACC-1", "--json", "--name=Customer A"});
  EXPECT_EQ(args.positional(0), "account");
  EXPECT_EQ(args.positional(1), "create");
  EXPECT_EQ(args.positional(2), "");
  EXPECT_EQ(args.option("id").value(), "ACC-1");
  EXPECT_EQ(args.option("name").value(), "Customer A");
  EXPECT_TRUE(args.flag("json"));
  EXPECT_FALSE(args.flag("verbose"));
  EXPECT_FALSE(args.option("missing").has_value());
}

TEST(Args, RequiredAndIntegerHelpers) {
  const auto args = parse({"x", "--limit", "25", "--bad", "ten"});
  EXPECT_EQ(args.required("limit").value(), "25");
  EXPECT_EQ(args.required("nope").error().code, ErrorCode::ValidationError);
  EXPECT_EQ(args.integer("limit", 5).value(), 25);
  EXPECT_EQ(args.integer("absent", 5).value(), 5);
  EXPECT_EQ(args.integer("bad", 5).error().code, ErrorCode::ValidationError);
}

TEST(Args, OptionWithoutValueIsAnError) {
  std::vector<const char*> argv{"corebank", "send", "--host"};
  const auto parsed = Args::parse(static_cast<int>(argv.size()), argv.data(), {});
  ASSERT_FALSE(parsed.has_value());
  EXPECT_EQ(parsed.error().code, ErrorCode::ValidationError);
}

TEST(Args, DoubleDashEndsOptions) {
  const auto args = parse({"send", "--", "--not-an-option"});
  EXPECT_EQ(args.positional(1), "--not-an-option");
}

}  // namespace
}  // namespace corebank::cli
