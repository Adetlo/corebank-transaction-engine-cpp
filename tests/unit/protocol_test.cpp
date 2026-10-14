#include "corebank/messaging/protocol.hpp"

#include <gtest/gtest.h>

#include <string>

namespace corebank::protocol {
namespace {

TEST(Protocol, ParsesTypeAndFields) {
  const auto msg = parse("TRANSFER|key=K1|from=ACC-A|to=ACC-B|amount=10.00|ccy=GBP|ref=").value();
  EXPECT_EQ(msg.type, "TRANSFER");
  EXPECT_EQ(msg.fields.size(), 6U);
  EXPECT_EQ(msg.get("from").value(), "ACC-A");
  EXPECT_EQ(msg.get("ref").value(), "");
  EXPECT_FALSE(msg.get("missing").has_value());
  EXPECT_EQ(msg.require("missing").error().code, ErrorCode::MissingField);
  EXPECT_EQ(parse("PING").value().type, "PING");
  EXPECT_EQ(parse("PING\r").value().type, "PING");
}

TEST(Protocol, RejectsMalformedInput) {
  EXPECT_EQ(parse("").error().code, ErrorCode::MalformedMessage);
  EXPECT_EQ(parse("ping").error().code, ErrorCode::MalformedMessage);
  EXPECT_EQ(parse("PING|novalue").error().code, ErrorCode::MalformedMessage);
  EXPECT_EQ(parse("PING|Key=v").error().code, ErrorCode::MalformedMessage);
  EXPECT_EQ(parse("PING|=v").error().code, ErrorCode::MalformedMessage);
  EXPECT_EQ(parse("PING|a=1|a=2").error().code, ErrorCode::MalformedMessage);
  EXPECT_EQ(parse("PING|a=\x01").error().code, ErrorCode::MalformedMessage);
  EXPECT_EQ(parse("|a=1").error().code, ErrorCode::MalformedMessage);
}

TEST(Protocol, EnforcesSizeLimits) {
  EXPECT_EQ(parse(std::string(5000, 'A'), 4096).error().code, ErrorCode::PayloadTooLarge);
  EXPECT_EQ(parse("PING|a=" + std::string(600, 'x')).error().code, ErrorCode::PayloadTooLarge);
  std::string many = "PING";
  for (int i = 0; i < 40; ++i) {
    many += "|k" + std::to_string(i) + "=v";
  }
  EXPECT_EQ(parse(many).error().code, ErrorCode::MalformedMessage);
}

TEST(Protocol, SerialisesAndSanitises) {
  EXPECT_EQ(ok_response({{"txn_id", "TXN-1"}, {"status", "POSTED"}}),
            "OK|txn_id=TXN-1|status=POSTED");
  EXPECT_EQ(error_response(make_error(ErrorCode::InsufficientFunds, "a|b\nc")),
            "ERR|code=INSUFFICIENT_FUNDS|message=a b c");
  EXPECT_EQ(sanitize_value("plain"), "plain");
  // Round trip through parse.
  const auto back = parse(ok_response({{"a", "1"}, {"b", "two words"}})).value();
  EXPECT_EQ(back.type, "OK");
  EXPECT_EQ(back.get("b").value(), "two words");
}

}  // namespace
}  // namespace corebank::protocol
