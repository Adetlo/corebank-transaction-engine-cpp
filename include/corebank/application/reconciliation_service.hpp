// Proves that stored balances, postings and transactions agree with each other.
#pragma once

#include "corebank/application/repository.hpp"
#include "corebank/domain/reconciliation.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"

#include <optional>

namespace corebank {

class ReconciliationService {
 public:
  ReconciliationService(Repository& repository, const Clock& clock);

  // Runs every check and records a RECONCILIATION_RUN audit event.
  [[nodiscard]] Result<ReconciliationResult> run(const std::optional<BusinessDate>& business_date);
  // Same checks inside an existing unit of work (used by the EOD batch); no audit.
  [[nodiscard]] Result<ReconciliationResult> run_in(UnitOfWork& uow,
                                                    const std::optional<BusinessDate>& date);

 private:
  Repository& repository_;
  const Clock& clock_;
};

}  // namespace corebank
