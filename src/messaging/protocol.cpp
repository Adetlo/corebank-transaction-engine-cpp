#include "corebank/messaging/protocol.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace corebank::protocol {
namespace {

bool valid_key(std::string_view key) {
  if (key.empty() || key.size() > 32) {
    return false;
  }
  return std::all_of(key.begin(), key.end(), [](char ch) {
    return std::islower(static_cast<unsigned char>(ch)) != 0 ||
           std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '_';
  });
}

bool valid_type(std::string_view type) {
  if (type.empty() || type.size() > 32) {
    return false;
  }
  return std::all_of(type.begin(), type.end(), [](char ch) {
    return std::isupper(static_cast<unsigned char>(ch)) != 0 ||
           std::isdigit(static_cast<unsigned char>(ch)) != 0 || ch == '_';
  });
}

bool printable(std::string_view text) {
  return std::all_of(text.begin(), text.end(),
                     [](char ch) { return std::isprint(static_cast<unsigned char>(ch)) != 0; });
}

}  // namespace

std::optional<std::string_view> Message::get(std::string_view key) const noexcept {
  for (const auto& [k, v] : fields) {
    if (k == key) {
      return std::string_view{v};
    }
  }
  return std::nullopt;
}

Result<std::string_view> Message::require(std::string_view key) const {
  if (const auto value = get(key)) {
    return *value;
  }
  return make_error(ErrorCode::MissingField, "missing required field '" + std::string{key} + "'");
}

Result<Message> parse(std::string_view line, std::size_t max_bytes) {
  if (line.size() > max_bytes) {
    return make_error(ErrorCode::PayloadTooLarge,
                      "message exceeds " + std::to_string(max_bytes) + " bytes");
  }
  if (!line.empty() && line.back() == '\r') {
    line.remove_suffix(1);
  }
  if (line.empty()) {
    return make_error(ErrorCode::MalformedMessage, "empty message");
  }
  if (!printable(line)) {
    return make_error(ErrorCode::MalformedMessage, "message contains control characters");
  }
  Message message;
  std::size_t start = 0;
  bool first = true;
  while (start <= line.size()) {
    const auto end = line.find('|', start);
    const std::string_view token =
        line.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
    if (first) {
      if (!valid_type(token)) {
        return make_error(ErrorCode::MalformedMessage,
                          "message type must be upper-case letters, digits or '_'");
      }
      message.type = std::string{token};
      first = false;
    } else {
      const auto eq = token.find('=');
      if (eq == std::string_view::npos) {
        return make_error(ErrorCode::MalformedMessage,
                          "field '" + std::string{token} + "' is not key=value");
      }
      const std::string_view key = token.substr(0, eq);
      const std::string_view value = token.substr(eq + 1);
      if (!valid_key(key)) {
        return make_error(ErrorCode::MalformedMessage,
                          "field key '" + std::string{key} + "' is invalid");
      }
      if (value.size() > kMaxValueBytes) {
        return make_error(ErrorCode::PayloadTooLarge, "field '" + std::string{key} + "' exceeds " +
                                                          std::to_string(kMaxValueBytes) +
                                                          " bytes");
      }
      if (message.has(key)) {
        return make_error(ErrorCode::MalformedMessage,
                          "duplicate field '" + std::string{key} + "'");
      }
      if (message.fields.size() >= kMaxFields) {
        return make_error(ErrorCode::MalformedMessage, "too many fields");
      }
      message.fields.emplace_back(std::string{key}, std::string{value});
    }
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return message;
}

std::string sanitize_value(std::string_view value) {
  std::string out;
  out.reserve(value.size());
  for (const char ch : value) {
    if (ch == '|' || ch == '\n' || ch == '\r') {
      out += ' ';
    } else if (std::isprint(static_cast<unsigned char>(ch)) == 0) {
      out += '?';
    } else {
      out += ch;
    }
  }
  return out;
}

std::string serialize(std::string_view type,
                      const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string out{type};
  for (const auto& [key, value] : fields) {
    out += '|';
    out += key;
    out += '=';
    out += sanitize_value(value);
  }
  return out;
}

std::string ok_response(const std::vector<std::pair<std::string, std::string>>& fields) {
  return serialize("OK", fields);
}

std::string error_response(const Error& error) {
  return serialize("ERR",
                   {{"code", std::string{to_string(error.code)}}, {"message", error.message}});
}

}  // namespace corebank::protocol
