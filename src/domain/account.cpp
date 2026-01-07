#include "corebank/domain/account.hpp"

#include <cctype>
#include <string>
#include <string_view>

namespace corebank {

std::string_view to_string(AccountType type) noexcept {
  switch (type) {
    case AccountType::Customer:
      return "CUSTOMER";
    case AccountType::Settlement:
      return "SETTLEMENT";
    case AccountType::FeeIncome:
      return "FEE_INCOME";
    case AccountType::Suspense:
      return "SUSPENSE";
  }
  return "UNKNOWN";
}

std::string_view to_string(AccountStatus status) noexcept {
  switch (status) {
    case AccountStatus::Active:
      return "ACTIVE";
    case AccountStatus::Frozen:
      return "FROZEN";
    case AccountStatus::Closed:
      return "CLOSED";
  }
  return "UNKNOWN";
}

Result<AccountType> parse_account_type(std::string_view text) {
  for (const auto type : {AccountType::Customer, AccountType::Settlement, AccountType::FeeIncome,
                          AccountType::Suspense}) {
    if (to_string(type) == text) {
      return type;
    }
  }
  return make_error(ErrorCode::ValidationError,
                    "unknown account type '" + std::string{text} +
                        "' (expected CUSTOMER, SETTLEMENT, FEE_INCOME or SUSPENSE)");
}

Result<AccountStatus> parse_account_status(std::string_view text) {
  for (const auto status : {AccountStatus::Active, AccountStatus::Frozen, AccountStatus::Closed}) {
    if (to_string(status) == text) {
      return status;
    }
  }
  return make_error(ErrorCode::ValidationError,
                    "unknown account status '" + std::string{text} + "'");
}

Result<void> validate_account_name(std::string_view name) {
  if (name.empty() || name.size() > 100) {
    return make_error(ErrorCode::ValidationError, "account name must be 1 to 100 characters");
  }
  for (const char ch : name) {
    if (std::isprint(static_cast<unsigned char>(ch)) == 0 || ch == '|') {
      return make_error(ErrorCode::ValidationError,
                        "account name may only contain printable characters, excluding '|'");
    }
  }
  return ok();
}

Result<Money> Account::balance_after_debit(const Money& amount) const {
  if (!is_active()) {
    return make_error(ErrorCode::AccountInactive,
                      "account " + id.str() + " is " + std::string{to_string(status)});
  }
  if (!amount.same_currency(balance)) {
    return make_error(ErrorCode::CurrencyMismatch, "account " + id.str() + " is denominated in " +
                                                       balance.currency().str() + ", not " +
                                                       amount.currency().str());
  }
  auto next = balance.subtract(amount);
  if (!next) {
    return next.error();
  }
  if (next.value().is_negative() && !allow_overdraft) {
    return make_error(ErrorCode::InsufficientFunds, "account " + id.str() + " has " +
                                                        balance.to_string() + ", cannot debit " +
                                                        amount.to_string());
  }
  return next;
}

Result<Money> Account::balance_after_credit(const Money& amount) const {
  if (!is_active()) {
    return make_error(ErrorCode::AccountInactive,
                      "account " + id.str() + " is " + std::string{to_string(status)});
  }
  if (!amount.same_currency(balance)) {
    return make_error(ErrorCode::CurrencyMismatch, "account " + id.str() + " is denominated in " +
                                                       balance.currency().str() + ", not " +
                                                       amount.currency().str());
  }
  return balance.add(amount);
}

}  // namespace corebank
