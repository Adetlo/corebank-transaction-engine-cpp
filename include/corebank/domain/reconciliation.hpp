// Reconciliation outcome: a list of named checks plus per-currency totals.
#pragma once

#include "corebank/domain/time.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace corebank {

struct ReconciliationCheck {
  std::string name;
  bool passed{false};
  std::string detail;
};

struct CurrencyTotal {
  std::string currency;
  std::int64_t posted_count{0};
  std::int64_t rejected_count{0};
  std::int64_t reversed_count{0};
  std::int64_t credit_total_minor{0};
  std::int64_t debit_total_minor{0};
  std::int64_t transfer_total_minor{0};
};

struct ReconciliationResult {
  bool passed{false};
  Timestamp run_at;
  std::string business_date;
  std::vector<ReconciliationCheck> checks;
  std::vector<CurrencyTotal> totals;

  [[nodiscard]] std::size_t failed_count() const noexcept;
  [[nodiscard]] std::string summary() const;
};

}  // namespace corebank
