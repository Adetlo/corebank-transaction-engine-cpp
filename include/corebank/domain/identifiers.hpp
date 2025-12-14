// Strong identifier types. Each tag carries its own validation policy so that an
// AccountId can never be passed where a TransactionId is expected.
#pragma once

#include "corebank/domain/result.hpp"

#include <compare>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace corebank {

struct IdentifierPolicy {
  std::string_view name;
  std::size_t max_length{64};
  // Allowed characters beyond [A-Za-z0-9].
  std::string_view extra_chars;
};

template <typename Tag>
class Identifier {
 public:
  [[nodiscard]] static Result<Identifier> parse(std::string_view text);
  // For values produced by the engine itself (generated ids); still validated in debug.
  [[nodiscard]] static Identifier unchecked(std::string text) {
    return Identifier(std::move(text));
  }

  [[nodiscard]] const std::string& str() const noexcept { return value_; }
  [[nodiscard]] std::string_view view() const noexcept { return value_; }

  [[nodiscard]] bool operator==(const Identifier& other) const noexcept = default;
  [[nodiscard]] std::strong_ordering operator<=>(const Identifier& other) const noexcept = default;

 private:
  explicit Identifier(std::string value) : value_(std::move(value)) {}
  std::string value_;
};

// Shared validator used by every identifier tag.
[[nodiscard]] Result<void> validate_identifier(std::string_view text,
                                               const IdentifierPolicy& policy);

struct AccountIdTag {
  static constexpr IdentifierPolicy policy{"account id", 64, "-_."};
};
struct TransactionIdTag {
  static constexpr IdentifierPolicy policy{"transaction id", 64, "-_."};
};
struct BatchIdTag {
  static constexpr IdentifierPolicy policy{"batch id", 64, "-_."};
};
struct IdempotencyKeyTag {
  static constexpr IdentifierPolicy policy{"idempotency key", 128, "-_.:"};
};

using AccountId = Identifier<AccountIdTag>;
using TransactionId = Identifier<TransactionIdTag>;
using BatchId = Identifier<BatchIdTag>;
using IdempotencyKey = Identifier<IdempotencyKeyTag>;

template <typename Tag>
Result<Identifier<Tag>> Identifier<Tag>::parse(std::string_view text) {
  if (auto check = validate_identifier(text, Tag::policy); !check) {
    return check.error();
  }
  return Identifier(std::string{text});
}

// Free-text narrative attached to a transaction (bounded, printable, protocol-safe).
class JournalReference {
 public:
  static constexpr std::size_t kMaxLength = 140;

  [[nodiscard]] static Result<JournalReference> parse(std::string_view text);
  [[nodiscard]] static JournalReference unchecked(std::string text) {
    return JournalReference(std::move(text));
  }

  [[nodiscard]] const std::string& str() const noexcept { return value_; }
  [[nodiscard]] bool operator==(const JournalReference& other) const noexcept = default;

 private:
  explicit JournalReference(std::string value) : value_(std::move(value)) {}
  std::string value_;
};

}  // namespace corebank

template <typename Tag>
struct std::hash<corebank::Identifier<Tag>> {
  std::size_t operator()(const corebank::Identifier<Tag>& id) const noexcept {
    return std::hash<std::string>{}(id.str());
  }
};
