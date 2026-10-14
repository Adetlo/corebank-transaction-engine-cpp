#include "corebank/application/reconciliation_service.hpp"

#include "corebank/domain/money.hpp"
#include "corebank/infrastructure/json_writer.hpp"
#include "corebank/infrastructure/logger.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace corebank {
namespace {

void add_check(ReconciliationResult& result, std::string name, bool passed, std::string detail) {
  result.checks.push_back(
      ReconciliationCheck{.name = std::move(name), .passed = passed, .detail = std::move(detail)});
}

}  // namespace

ReconciliationService::ReconciliationService(Repository& repository, const Clock& clock)
    : repository_(repository), clock_(clock) {}

Result<ReconciliationResult> ReconciliationService::run_in(
    UnitOfWork& uow, const std::optional<BusinessDate>& business_date) {
  ReconciliationResult result;
  result.run_at = clock_.now();
  result.business_date = business_date ? business_date->str() : "all";

  // 1. Stored balance == sum(credits) - sum(debits) and == last posting's balance_after.
  auto accounts = uow.list_accounts();
  if (!accounts) {
    return accounts.error();
  }
  auto totals = uow.posting_totals_by_account();
  if (!totals) {
    return totals.error();
  }
  std::unordered_map<AccountId, AccountPostingTotal> by_account;
  for (const auto& total : totals.value()) {
    by_account.emplace(total.account_id, total);
  }
  std::int64_t balance_mismatches = 0;
  std::int64_t chain_mismatches = 0;
  std::int64_t overdraft_breaches = 0;
  std::string first_mismatch;
  for (const auto& account : accounts.value()) {
    const auto it = by_account.find(account.id);
    const std::int64_t derived =
        it == by_account.end() ? 0 : it->second.credit_minor - it->second.debit_minor;
    if (derived != account.balance.minor()) {
      ++balance_mismatches;
      if (first_mismatch.empty()) {
        first_mismatch = account.id.str() + " stored=" + std::to_string(account.balance.minor()) +
                         " derived=" + std::to_string(derived);
      }
    }
    if (it != by_account.end() && it->second.posting_count > 0 &&
        it->second.last_balance_after_minor != account.balance.minor()) {
      ++chain_mismatches;
    }
    if (!account.allow_overdraft && account.balance.is_negative()) {
      ++overdraft_breaches;
    }
  }
  add_check(result, "account_balance_vs_postings", balance_mismatches == 0,
            balance_mismatches == 0
                ? std::to_string(accounts.value().size()) + " accounts agree with their postings"
                : std::to_string(balance_mismatches) + " mismatched (" + first_mismatch + ")");
  add_check(result, "posting_chain_balance_after", chain_mismatches == 0,
            chain_mismatches == 0 ? "latest posting balance_after matches every account"
                                  : std::to_string(chain_mismatches) + " accounts diverge");
  add_check(result, "overdraft_policy", overdraft_breaches == 0,
            overdraft_breaches == 0 ? "no negative balance without overdraft"
                                    : std::to_string(overdraft_breaches) + " accounts overdrawn");

  // 2. Journal consistency: every transaction has exactly the legs its status implies.
  auto txn_totals = uow.posting_totals_by_transaction();
  if (!txn_totals) {
    return txn_totals.error();
  }
  std::int64_t journal_errors = 0;
  std::int64_t unbalanced_transfers = 0;
  std::int64_t effective = 0;
  std::string first_journal_error;
  for (const auto& t : txn_totals.value()) {
    const bool is_effective =
        t.status == TransactionStatus::Posted || t.status == TransactionStatus::Reversed;
    if (!is_effective) {
      if (t.posting_count != 0) {
        ++journal_errors;
        if (first_journal_error.empty()) {
          first_journal_error =
              t.transaction_id.str() + " has postings while " + std::string{to_string(t.status)};
        }
      }
      continue;
    }
    ++effective;
    const bool two_legs = t.posting_count == 2;
    if (two_legs && t.debit_minor != t.credit_minor) {
      ++unbalanced_transfers;
    }
    const bool legs_ok =
        (t.type == TransactionType::Transfer && two_legs) ||
        ((t.type == TransactionType::Credit || t.type == TransactionType::Debit) &&
         t.posting_count == 1) ||
        (t.type == TransactionType::Reversal && (t.posting_count == 1 || t.posting_count == 2));
    if (!legs_ok) {
      ++journal_errors;
      if (first_journal_error.empty()) {
        first_journal_error = t.transaction_id.str() + " has " + std::to_string(t.posting_count) +
                              " postings for " + std::string{to_string(t.type)};
      }
    }
  }
  add_check(result, "journal_consistency", journal_errors == 0,
            journal_errors == 0
                ? std::to_string(effective) + " effective transactions have the expected legs"
                : std::to_string(journal_errors) + " inconsistent (" + first_journal_error + ")");
  add_check(result, "transfer_double_entry", unbalanced_transfers == 0,
            unbalanced_transfers == 0 ? "every two-leg transaction has debit == credit"
                                      : std::to_string(unbalanced_transfers) + " unbalanced");

  // 3. Idempotency records all point at real transactions.
  auto orphans = uow.count_orphan_idempotency_records();
  if (!orphans) {
    return orphans.error();
  }
  add_check(result, "idempotency_links", orphans.value() == 0,
            orphans.value() == 0 ? "no orphan idempotency records"
                                 : std::to_string(orphans.value()) + " orphan records");

  // 4. Storage-level integrity.
  auto integrity = uow.integrity_check();
  if (!integrity) {
    return integrity.error();
  }
  add_check(result, "storage_integrity", integrity.value() == "ok", integrity.value());

  // 5. Totals by currency (informational, feeds the daily summary).
  auto totals_by_currency = uow.currency_totals(business_date);
  if (!totals_by_currency) {
    return totals_by_currency.error();
  }
  result.totals = totals_by_currency.value();

  result.passed = result.failed_count() == 0;
  return result;
}

Result<ReconciliationResult> ReconciliationService::run(
    const std::optional<BusinessDate>& business_date) {
  const auto started = std::chrono::steady_clock::now();
  auto uow_result = repository_.begin(TransactionMode::ReadWrite);
  if (!uow_result) {
    return uow_result.error();
  }
  auto& uow = *uow_result.value();
  auto result = run_in(uow, business_date);
  if (!result) {
    return result.error();
  }
  JsonWriter detail;
  detail.boolean("passed", result.value().passed)
      .number("checks", static_cast<std::int64_t>(result.value().checks.size()))
      .number("failed", static_cast<std::int64_t>(result.value().failed_count()))
      .string("scope", result.value().business_date);
  AuditEvent event{.id = 0,
                   .occurred_at = clock_.now(),
                   .type = AuditEventType::ReconciliationRun,
                   .entity_type = "reconciliation",
                   .entity_id = result.value().business_date,
                   .detail = detail.str(),
                   .batch_id = std::nullopt};
  if (auto audited = uow.append_audit(event); !audited) {
    return audited.error();
  }
  if (auto committed = uow.commit(); !committed) {
    return committed.error();
  }
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
  Logger::instance().log(
      result.value().passed ? LogLevel::Info : LogLevel::Warn, "reconciliation.completed",
      {{"passed", result.value().passed},
       {"failed_checks", static_cast<std::int64_t>(result.value().failed_count())},
       {"duration_ms", ms}});
  return result;
}

}  // namespace corebank
