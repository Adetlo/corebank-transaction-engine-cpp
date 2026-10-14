// Text and JSON rendering for CLI results, plus the exit-code policy.
#pragma once

#include "corebank/application/requests.hpp"
#include "corebank/domain/account.hpp"
#include "corebank/domain/audit.hpp"
#include "corebank/domain/batch.hpp"
#include "corebank/domain/error.hpp"
#include "corebank/domain/posting.hpp"
#include "corebank/domain/reconciliation.hpp"
#include "corebank/domain/transaction.hpp"

#include <string>
#include <vector>

namespace corebank::cli {

// 0 ok, 1 usage, 2 business rejection, 3 control failure (reconciliation/EOD), 4 system error.
enum ExitCode : int {
  kOk = 0,
  kUsage = 1,
  kRejected = 2,
  kControlFailed = 3,
  kSystemError = 4,
};

[[nodiscard]] int exit_code_for(ErrorCode code) noexcept;

struct Printer {
  bool json{false};

  void error(const Error& error) const;
  void message(const std::string& text, const std::string& json_key = "message") const;
  void account(const Account& account) const;
  void accounts(const std::vector<Account>& list) const;
  void receipt(const TransactionReceipt& receipt) const;
  void transaction(const Transaction& t, const std::vector<Posting>& legs) const;
  void postings(const std::vector<Posting>& list) const;
  void audit(const std::vector<AuditEvent>& events) const;
  void reconciliation(const ReconciliationResult& result) const;
  void batch(const BatchRun& run) const;
  void batches(const std::vector<BatchRun>& runs) const;
};

[[nodiscard]] std::string account_json(const Account& account);
[[nodiscard]] std::string transaction_json(const Transaction& txn);
[[nodiscard]] std::string posting_json(const Posting& posting);
[[nodiscard]] std::string audit_json(const AuditEvent& event);
[[nodiscard]] std::string batch_json(const BatchRun& run);
[[nodiscard]] std::string reconciliation_json(const ReconciliationResult& result);

}  // namespace corebank::cli
