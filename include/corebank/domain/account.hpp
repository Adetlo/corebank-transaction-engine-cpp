#pragma once

#include "corebank/domain/identifiers.hpp"
#include "corebank/domain/money.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace corebank {

enum class AccountType { Customer, Settlement, FeeIncome, Suspense };
enum class AccountStatus { Active, Frozen, Closed };

[[nodiscard]] std::string_view to_string(AccountType type) noexcept;
[[nodiscard]] std::string_view to_string(AccountStatus status) noexcept;
[[nodiscard]] Result<AccountType> parse_account_type(std::string_view text);
[[nodiscard]] Result<AccountStatus> parse_account_status(std::string_view text);

struct Account {
  AccountId id;
  std::string name;
  AccountType type{AccountType::Customer};
  Money balance;
  bool allow_overdraft{false};
  AccountStatus status{AccountStatus::Active};
  std::int64_t version{0};
  Timestamp created_at;
  Timestamp updated_at;

  [[nodiscard]] CurrencyCode currency() const noexcept { return balance.currency(); }
  [[nodiscard]] bool is_active() const noexcept { return status == AccountStatus::Active; }

  // Checks whether `amount` can be debited now: active, same currency, funds/overdraft.
  [[nodiscard]] Result<Money> balance_after_debit(const Money& amount) const;
  [[nodiscard]] Result<Money> balance_after_credit(const Money& amount) const;
};

[[nodiscard]] Result<void> validate_account_name(std::string_view name);

}  // namespace corebank
