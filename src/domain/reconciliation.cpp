#include "corebank/domain/reconciliation.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace corebank {

std::size_t ReconciliationResult::failed_count() const noexcept {
  return static_cast<std::size_t>(
      std::count_if(checks.begin(), checks.end(),
                    [](const ReconciliationCheck& check) { return !check.passed; }));
}

std::string ReconciliationResult::summary() const {
  std::string out = passed ? "PASSED" : "FAILED";
  out += " (" + std::to_string(checks.size() - failed_count()) + "/" +
         std::to_string(checks.size()) + " checks passed)";
  return out;
}

}  // namespace corebank
