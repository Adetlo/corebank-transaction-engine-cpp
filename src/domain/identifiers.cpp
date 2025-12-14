#include "corebank/domain/identifiers.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace corebank {

Result<void> validate_identifier(std::string_view text, const IdentifierPolicy& policy) {
  const std::string name{policy.name};
  if (text.empty()) {
    return make_error(ErrorCode::InvalidIdentifier, name + " must not be empty");
  }
  if (text.size() > policy.max_length) {
    return make_error(ErrorCode::InvalidIdentifier,
                      name + " exceeds " + std::to_string(policy.max_length) + " characters");
  }
  if (std::isalnum(static_cast<unsigned char>(text.front())) == 0) {
    return make_error(ErrorCode::InvalidIdentifier, name + " must start with a letter or digit");
  }
  for (const char ch : text) {
    const bool alnum = std::isalnum(static_cast<unsigned char>(ch)) != 0;
    const bool extra = policy.extra_chars.find(ch) != std::string_view::npos;
    if (!alnum && !extra) {
      return make_error(ErrorCode::InvalidIdentifier,
                        name + " contains an invalid character '" + std::string(1, ch) + "'");
    }
  }
  return ok();
}

Result<JournalReference> JournalReference::parse(std::string_view text) {
  if (text.size() > kMaxLength) {
    return make_error(ErrorCode::InvalidReference,
                      "reference exceeds " + std::to_string(kMaxLength) + " characters");
  }
  for (const char ch : text) {
    const auto uch = static_cast<unsigned char>(ch);
    if (std::isprint(uch) == 0 || ch == '|' || ch == '\n' || ch == '\r') {
      return make_error(ErrorCode::InvalidReference,
                        "reference may only contain printable characters, excluding '|'");
    }
  }
  return JournalReference(std::string{text});
}

}  // namespace corebank
