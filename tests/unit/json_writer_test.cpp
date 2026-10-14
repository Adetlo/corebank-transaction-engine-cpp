#include "corebank/infrastructure/json_writer.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace corebank {
namespace {

TEST(JsonWriter, BuildsObjects) {
  JsonWriter w;
  w.string("name", "Customer \"A\"")
      .number("count", 3)
      .number("ratio", 0.5)
      .boolean("ok", true)
      .null("none");
  EXPECT_EQ(w.str(), R"({"name":"Customer \"A\"","count":3,"ratio":0.500,"ok":true,"none":null})");
}

TEST(JsonWriter, NestsRawFragmentsAndArrays) {
  JsonArrayWriter arr;
  arr.string("a").raw("{\"k\":1}");
  JsonWriter w;
  w.raw("items", arr.str());
  EXPECT_EQ(w.str(), R"({"items":["a",{"k":1}]})");
  EXPECT_TRUE(JsonWriter{}.empty());
  EXPECT_EQ(JsonWriter{}.str(), "{}");
}

TEST(JsonWriter, EscapesControlCharacters) {
  EXPECT_EQ(json_escape("tab\tnl\ncr\rbs\\q\""), "tab\\tnl\\ncr\\rbs\\\\q\\\"");
  EXPECT_EQ(json_escape(std::string("\x01", 1)), "\\u0001");
}

TEST(JsonWriter, IntegralOverloadsAcceptCommonWidths) {
  JsonWriter w;
  w.number("a", 1).number("b", 2U).number("c", std::size_t{3}).number("d", std::int64_t{4});
  EXPECT_EQ(w.str(), R"({"a":1,"b":2,"c":3,"d":4})");
}

}  // namespace
}  // namespace corebank
