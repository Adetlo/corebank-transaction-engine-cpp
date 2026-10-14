// Minimal line-delimited wire protocol:  TYPE|key=value|key=value\n
// Responses:  OK|key=value...\n   or   ERR|code=CODE|message=text\n
// Generic by design: it is not modelled on any proprietary banking protocol.
#pragma once

#include "corebank/domain/error.hpp"
#include "corebank/domain/result.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corebank::protocol {

inline constexpr std::size_t kDefaultMaxLineBytes = 4096;
inline constexpr std::size_t kMaxFields = 32;
inline constexpr std::size_t kMaxValueBytes = 512;
inline constexpr std::string_view kVersion = "1";

struct Message {
  std::string type;
  std::vector<std::pair<std::string, std::string>> fields;

  [[nodiscard]] std::optional<std::string_view> get(std::string_view key) const noexcept;
  [[nodiscard]] Result<std::string_view> require(std::string_view key) const;
  [[nodiscard]] bool has(std::string_view key) const noexcept { return get(key).has_value(); }
};

// Parses one line (without the trailing newline). Rejects oversized input, empty type,
// malformed pairs, duplicate keys, control characters and too many fields.
[[nodiscard]] Result<Message> parse(std::string_view line,
                                    std::size_t max_bytes = kDefaultMaxLineBytes);

// Builds "TYPE|k=v|k=v" (no newline).
[[nodiscard]] std::string serialize(std::string_view type,
                                    const std::vector<std::pair<std::string, std::string>>& fields);
[[nodiscard]] std::string ok_response(
    const std::vector<std::pair<std::string, std::string>>& fields);
[[nodiscard]] std::string error_response(const Error& error);

// Values must not contain '|', '\n', '\r' or '='-prefixed keys; this sanitises output.
[[nodiscard]] std::string sanitize_value(std::string_view value);

}  // namespace corebank::protocol
