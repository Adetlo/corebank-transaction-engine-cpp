#include "corebank/domain/time.hpp"

#include <gtest/gtest.h>

namespace corebank {
namespace {

TEST(Timestamp, FormatsAndParsesIsoUtc) {
  const auto ts = parse_timestamp("2026-10-14T09:30:15.123456Z").value();
  EXPECT_EQ(format_timestamp(ts), "2026-10-14T09:30:15.123456Z");
  EXPECT_EQ(format_timestamp(parse_timestamp("2026-10-14T09:30:15Z").value()),
            "2026-10-14T09:30:15.000000Z");
  EXPECT_EQ(format_timestamp(parse_timestamp("2026-10-14T09:30:15.5Z").value()),
            "2026-10-14T09:30:15.500000Z");
}

TEST(Timestamp, RejectsMalformed) {
  for (const auto* text :
       {"", "2026-10-14", "2026-10-14 09:30:15Z", "2026-13-01T00:00:00Z", "2026-02-30T00:00:00Z",
        "2026-10-14T25:00:00Z", "2026-10-14T09:30:15.1234567Z", "2026-10-14T09:30:15"}) {
    EXPECT_FALSE(parse_timestamp(text).has_value()) << text;
  }
}

TEST(Timestamp, RoundTripsThroughSystemClock) {
  const auto now = SystemClock{}.now();
  EXPECT_EQ(parse_timestamp(format_timestamp(now)).value(), now);
}

TEST(BusinessDate, ParsesAndDerives) {
  EXPECT_EQ(BusinessDate::parse("2026-10-14").value().str(), "2026-10-14");
  EXPECT_EQ(BusinessDate::from_timestamp(parse_timestamp("2026-10-14T23:59:59Z").value()).str(),
            "2026-10-14");
  EXPECT_LT(BusinessDate::parse("2026-10-13").value(), BusinessDate::parse("2026-10-14").value());
  for (const auto* text : {"", "2026-1-4", "2026/10/14", "2026-02-30", "20261014"}) {
    EXPECT_FALSE(BusinessDate::parse(text).has_value()) << text;
  }
}

}  // namespace
}  // namespace corebank
