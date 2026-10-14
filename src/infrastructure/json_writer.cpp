#include "corebank/infrastructure/json_writer.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace corebank {

std::string json_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  for (const char ch : text) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          std::array<char, 8> buffer{};
          std::snprintf(buffer.data(), buffer.size(), "\\u%04x", static_cast<unsigned>(ch));
          out += buffer.data();
        } else {
          out += ch;
        }
    }
  }
  return out;
}

std::string format_double(double value) {
  std::array<char, 64> buffer{};
  const int written = std::snprintf(buffer.data(), buffer.size(), "%.3f", value);
  return {buffer.data(), static_cast<std::size_t>(written > 0 ? written : 0)};
}

void JsonWriter::key(std::string_view name) {
  if (!body_.empty()) {
    body_ += ',';
  }
  body_ += '"';
  body_ += json_escape(name);
  body_ += "\":";
}

JsonWriter& JsonWriter::string(std::string_view name, std::string_view value) {
  key(name);
  body_ += '"';
  body_ += json_escape(value);
  body_ += '"';
  return *this;
}

JsonWriter& JsonWriter::number(std::string_view name, std::int64_t value) {
  key(name);
  body_ += std::to_string(value);
  return *this;
}

JsonWriter& JsonWriter::number(std::string_view name, double value) {
  key(name);
  body_ += format_double(value);
  return *this;
}

JsonWriter& JsonWriter::boolean(std::string_view name, bool value) {
  key(name);
  body_ += value ? "true" : "false";
  return *this;
}

JsonWriter& JsonWriter::null(std::string_view name) {
  key(name);
  body_ += "null";
  return *this;
}

JsonWriter& JsonWriter::raw(std::string_view name, std::string_view json) {
  key(name);
  body_ += json;
  return *this;
}

void JsonArrayWriter::separator() {
  if (!body_.empty()) {
    body_ += ',';
  }
}

JsonArrayWriter& JsonArrayWriter::string(std::string_view value) {
  separator();
  body_ += '"';
  body_ += json_escape(value);
  body_ += '"';
  return *this;
}

JsonArrayWriter& JsonArrayWriter::raw(std::string_view json) {
  separator();
  body_ += json;
  return *this;
}

}  // namespace corebank
