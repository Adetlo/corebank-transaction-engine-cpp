#include "corebank/domain/posting.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace corebank {

std::string_view to_string(PostingDirection direction) noexcept {
  return direction == PostingDirection::Debit ? "DEBIT" : "CREDIT";
}

Result<PostingDirection> parse_posting_direction(std::string_view text) {
  if (text == "DEBIT") {
    return PostingDirection::Debit;
  }
  if (text == "CREDIT") {
    return PostingDirection::Credit;
  }
  return make_error(ErrorCode::ValidationError,
                    "unknown posting direction '" + std::string{text} + "'");
}

std::int64_t signed_minor(const Posting& posting) noexcept {
  return posting.direction == PostingDirection::Credit ? posting.amount.minor()
                                                       : -posting.amount.minor();
}

}  // namespace corebank
