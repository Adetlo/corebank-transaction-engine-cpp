// Persistence abstraction. The domain and application layers only ever see these
// interfaces; SQLite lives behind them in infrastructure/.
#pragma once

#include "corebank/domain/account.hpp"
#include "corebank/domain/audit.hpp"
#include "corebank/domain/batch.hpp"
#include "corebank/domain/identifiers.hpp"
#include "corebank/domain/posting.hpp"
#include "corebank/domain/reconciliation.hpp"
#include "corebank/domain/result.hpp"
#include "corebank/domain/time.hpp"
#include "corebank/domain/transaction.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace corebank {

struct IdempotencyRecord {
  IdempotencyKey key;
  std::string request_signature;  // canonical request text, compared verbatim
  TransactionId transaction_id;
  Timestamp created_at;
};

struct TransactionQuery {
  std::optional<BusinessDate> business_date;
  std::optional<TransactionStatus> status;
  bool pending_only{false};  // RECEIVED, VALIDATED or PROCESSING
  std::optional<Timestamp> created_before;
  std::size_t limit{1000};
};

struct AuditQuery {
  std::optional<std::string> entity_id;
  std::optional<BatchId> batch_id;
  std::optional<AuditEventType> type;
  std::size_t limit{200};
};

struct AccountPostingTotal {
  AccountId account_id;
  std::int64_t credit_minor{0};
  std::int64_t debit_minor{0};
  std::int64_t posting_count{0};
  std::int64_t last_balance_after_minor{0};
};

struct TransactionPostingTotal {
  TransactionId transaction_id;
  TransactionType type{TransactionType::Transfer};
  TransactionStatus status{TransactionStatus::Posted};
  std::int64_t credit_minor{0};
  std::int64_t debit_minor{0};
  std::int64_t posting_count{0};
};

enum class TransactionMode { ReadOnly, ReadWrite };

// One database transaction. Destroying an uncommitted unit of work rolls it back.
class UnitOfWork {
 public:
  UnitOfWork() = default;
  UnitOfWork(const UnitOfWork&) = delete;
  UnitOfWork& operator=(const UnitOfWork&) = delete;
  UnitOfWork(UnitOfWork&&) = delete;
  UnitOfWork& operator=(UnitOfWork&&) = delete;
  virtual ~UnitOfWork() = default;

  [[nodiscard]] virtual Result<void> commit() = 0;
  [[nodiscard]] virtual Result<void> rollback() = 0;

  // Accounts
  [[nodiscard]] virtual Result<std::optional<Account>> find_account(const AccountId& id) = 0;
  [[nodiscard]] virtual Result<std::vector<Account>> list_accounts() = 0;
  [[nodiscard]] virtual Result<void> insert_account(const Account& account) = 0;
  // Optimistic update: fails with PersistenceError if the stored version differs.
  [[nodiscard]] virtual Result<void> update_account_balance(const AccountId& id,
                                                            std::int64_t new_balance_minor,
                                                            std::int64_t expected_version,
                                                            Timestamp at) = 0;

  // Transactions
  [[nodiscard]] virtual Result<void> insert_transaction(const Transaction& txn) = 0;
  [[nodiscard]] virtual Result<void> update_transaction(const Transaction& txn) = 0;
  [[nodiscard]] virtual Result<std::optional<Transaction>> find_transaction(
      const TransactionId& id) = 0;
  [[nodiscard]] virtual Result<std::vector<Transaction>> list_transactions(
      const TransactionQuery& query) = 0;
  [[nodiscard]] virtual Result<std::vector<Transaction>> account_transactions(
      const AccountId& id, std::size_t limit) = 0;

  // Postings (immutable)
  [[nodiscard]] virtual Result<void> insert_posting(const Posting& posting) = 0;
  [[nodiscard]] virtual Result<std::vector<Posting>> postings_for_transaction(
      const TransactionId& id) = 0;
  [[nodiscard]] virtual Result<std::vector<Posting>> postings_for_account(const AccountId& id,
                                                                          std::size_t limit) = 0;
  [[nodiscard]] virtual Result<std::vector<AccountPostingTotal>> posting_totals_by_account() = 0;
  [[nodiscard]] virtual Result<std::vector<TransactionPostingTotal>>
  posting_totals_by_transaction() = 0;
  [[nodiscard]] virtual Result<std::vector<CurrencyTotal>> currency_totals(
      const std::optional<BusinessDate>& business_date) = 0;

  // Idempotency
  [[nodiscard]] virtual Result<std::optional<IdempotencyRecord>> find_idempotency(
      const IdempotencyKey& key) = 0;
  // Returns DuplicateRequest when the key already exists.
  [[nodiscard]] virtual Result<void> insert_idempotency(const IdempotencyRecord& record) = 0;
  [[nodiscard]] virtual Result<std::int64_t> count_orphan_idempotency_records() = 0;

  // Audit (append-only)
  [[nodiscard]] virtual Result<void> append_audit(const AuditEvent& event) = 0;
  [[nodiscard]] virtual Result<std::vector<AuditEvent>> list_audit(const AuditQuery& query) = 0;

  // Batches
  [[nodiscard]] virtual Result<std::optional<BatchRun>> find_batch(const BatchId& id) = 0;
  [[nodiscard]] virtual Result<std::optional<BatchRun>> latest_batch_for_date(
      const BusinessDate& date) = 0;
  [[nodiscard]] virtual Result<std::optional<BatchRun>> latest_batch() = 0;
  [[nodiscard]] virtual Result<std::vector<BatchRun>> list_batches(std::size_t limit) = 0;
  [[nodiscard]] virtual Result<void> insert_batch(const BatchRun& run) = 0;
  [[nodiscard]] virtual Result<void> update_batch(const BatchRun& run) = 0;
  [[nodiscard]] virtual Result<void> upsert_stage(const BatchStageRecord& record) = 0;
  [[nodiscard]] virtual Result<void> upsert_daily_summary(const DailySummary& summary) = 0;
  [[nodiscard]] virtual Result<std::vector<DailySummary>> daily_summaries(
      const BusinessDate& date) = 0;

  // Storage-level integrity check ("ok" when healthy).
  [[nodiscard]] virtual Result<std::string> integrity_check() = 0;
};

class Repository {
 public:
  Repository() = default;
  Repository(const Repository&) = delete;
  Repository& operator=(const Repository&) = delete;
  Repository(Repository&&) = delete;
  Repository& operator=(Repository&&) = delete;
  virtual ~Repository() = default;

  [[nodiscard]] virtual Result<std::unique_ptr<UnitOfWork>> begin(TransactionMode mode) = 0;
  [[nodiscard]] virtual Result<void> migrate() = 0;
  [[nodiscard]] virtual Result<void> health_check() = 0;
};

}  // namespace corebank
