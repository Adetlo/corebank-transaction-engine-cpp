// A posting is one immutable leg of the journal: one account, one direction, one amount.
#pragma once

#include "corebank/domain/identifiers.hpp"
#include "corebank/domain/money.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"

#include <cstdint>
#include <string_view>

namespace corebank {

enum class PostingDirection { Debit, Credit };

[[nodiscard]] std::string_view to_string(PostingDirection direction) noexcept;
[[nodiscard]] Result<PostingDirection> parse_posting_direction(std::string_view text);

struct Posting {
  std::int64_t id{0};  // assigned by persistence
  TransactionId transaction_id;
  AccountId account_id;
  PostingDirection direction{PostingDirection::Credit};
  Money amount;
  std::int64_t balance_after_minor{0};
  Timestamp created_at;
};

// Signed effect of a posting on the account balance (+credit, -debit).
[[nodiscard]] std::int64_t signed_minor(const Posting& posting) noexcept;

}  // namespace corebank
