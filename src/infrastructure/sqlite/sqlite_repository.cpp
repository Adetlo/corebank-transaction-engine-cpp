#include "corebank/infrastructure/sqlite/sqlite_repository.hpp"

#include "corebank/infrastructure/sqlite/schema.hpp"
#include "corebank/infrastructure/sqlite/statement.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace corebank::sqlite {
namespace {

// ---------------------------------------------------------------------------
// Row mappers
// ---------------------------------------------------------------------------

Result<Money> money_from(const Statement& row, int amount_col, int currency_col) {
  auto currency = CurrencyCode::parse(row.column_text(currency_col));
  if (!currency) {
    return make_error(ErrorCode::PersistenceError,
                      "stored currency is invalid: " + currency.error().message);
  }
  return Money{row.column_int64(amount_col), currency.value()};
}

Result<Timestamp> ts_from(const Statement& row, int col) {
  auto parsed = parse_timestamp(row.column_text(col));
  if (!parsed) {
    return make_error(ErrorCode::PersistenceError,
                      "stored timestamp is invalid: " + parsed.error().message);
  }
  return parsed.value();
}

Result<std::optional<Timestamp>> optional_ts_from(const Statement& row, int col) {
  if (row.column_is_null(col)) {
    return std::optional<Timestamp>{};
  }
  auto parsed = ts_from(row, col);
  if (!parsed) {
    return parsed.error();
  }
  return std::optional<Timestamp>{parsed.value()};
}

template <typename Id>
Result<std::optional<Id>> optional_id_from(const Statement& row, int col) {
  if (row.column_is_null(col)) {
    return std::optional<Id>{};
  }
  auto parsed = Id::parse(row.column_text(col));
  if (!parsed) {
    return make_error(ErrorCode::PersistenceError,
                      "stored identifier is invalid: " + parsed.error().message);
  }
  return std::optional<Id>{parsed.value()};
}

template <typename Id>
Result<Id> id_from(const Statement& row, int col) {
  auto parsed = Id::parse(row.column_text(col));
  if (!parsed) {
    return make_error(ErrorCode::PersistenceError,
                      "stored identifier is invalid: " + parsed.error().message);
  }
  return parsed.value();
}

constexpr std::string_view kAccountColumns =
    "account_id, name, account_type, currency, balance_minor, allow_overdraft, status, version, "
    "created_at, updated_at";

Result<Account> read_account(const Statement& row) {
  auto id = id_from<AccountId>(row, 0);
  if (!id) {
    return id.error();
  }
  auto type = parse_account_type(row.column_text(2));
  if (!type) {
    return make_error(ErrorCode::PersistenceError, type.error().message);
  }
  auto balance = money_from(row, 4, 3);
  if (!balance) {
    return balance.error();
  }
  auto status = parse_account_status(row.column_text(6));
  if (!status) {
    return make_error(ErrorCode::PersistenceError, status.error().message);
  }
  auto created = ts_from(row, 8);
  if (!created) {
    return created.error();
  }
  auto updated = ts_from(row, 9);
  if (!updated) {
    return updated.error();
  }
  return Account{
      .id = id.value(),
      .name = row.column_text(1),
      .type = type.value(),
      .balance = balance.value(),
      .allow_overdraft = row.column_int64(5) != 0,
      .status = status.value(),
      .version = row.column_int64(7),
      .created_at = created.value(),
      .updated_at = updated.value(),
  };
}

constexpr std::string_view kTransactionColumns =
    "transaction_id, transaction_type, status, currency, amount_minor, source_account_id, "
    "destination_account_id, reference, idempotency_key, reversal_of, reversed_by, "
    "rejection_code, rejection_reason, business_date, created_at, updated_at";

Result<ErrorCode> error_code_from_text(const std::string& text) {
  for (int i = 0; i <= static_cast<int>(ErrorCode::Internal); ++i) {
    const auto code = static_cast<ErrorCode>(i);
    if (to_string(code) == text) {
      return code;
    }
  }
  return make_error(ErrorCode::PersistenceError, "stored rejection code is invalid: " + text);
}

Result<Transaction> read_transaction(const Statement& row) {
  auto id = id_from<TransactionId>(row, 0);
  if (!id) {
    return id.error();
  }
  auto type = parse_transaction_type(row.column_text(1));
  if (!type) {
    return make_error(ErrorCode::PersistenceError, type.error().message);
  }
  auto status = parse_transaction_status(row.column_text(2));
  if (!status) {
    return make_error(ErrorCode::PersistenceError, status.error().message);
  }
  auto amount = money_from(row, 4, 3);
  if (!amount) {
    return amount.error();
  }
  auto source = optional_id_from<AccountId>(row, 5);
  if (!source) {
    return source.error();
  }
  auto destination = optional_id_from<AccountId>(row, 6);
  if (!destination) {
    return destination.error();
  }
  auto key = optional_id_from<IdempotencyKey>(row, 8);
  if (!key) {
    return key.error();
  }
  auto reversal_of = optional_id_from<TransactionId>(row, 9);
  if (!reversal_of) {
    return reversal_of.error();
  }
  auto reversed_by = optional_id_from<TransactionId>(row, 10);
  if (!reversed_by) {
    return reversed_by.error();
  }
  std::optional<Error> rejection;
  if (!row.column_is_null(11)) {
    auto code = error_code_from_text(row.column_text(11));
    if (!code) {
      return code.error();
    }
    rejection = Error{code.value(), row.column_text(12)};
  }
  auto date = BusinessDate::parse(row.column_text(13));
  if (!date) {
    return make_error(ErrorCode::PersistenceError, date.error().message);
  }
  auto created = ts_from(row, 14);
  if (!created) {
    return created.error();
  }
  auto updated = ts_from(row, 15);
  if (!updated) {
    return updated.error();
  }
  return Transaction{
      .id = id.value(),
      .type = type.value(),
      .status = status.value(),
      .amount = amount.value(),
      .source = source.value(),
      .destination = destination.value(),
      .reference = JournalReference::unchecked(row.column_text(7)),
      .idempotency_key = key.value(),
      .reversal_of = reversal_of.value(),
      .reversed_by = reversed_by.value(),
      .rejection = rejection,
      .business_date = date.value(),
      .created_at = created.value(),
      .updated_at = updated.value(),
  };
}

constexpr std::string_view kPostingColumns =
    "posting_id, transaction_id, account_id, direction, currency, amount_minor, "
    "balance_after_minor, created_at";

Result<Posting> read_posting(const Statement& row) {
  auto txn = id_from<TransactionId>(row, 1);
  if (!txn) {
    return txn.error();
  }
  auto account = id_from<AccountId>(row, 2);
  if (!account) {
    return account.error();
  }
  auto direction = parse_posting_direction(row.column_text(3));
  if (!direction) {
    return make_error(ErrorCode::PersistenceError, direction.error().message);
  }
  auto amount = money_from(row, 5, 4);
  if (!amount) {
    return amount.error();
  }
  auto created = ts_from(row, 7);
  if (!created) {
    return created.error();
  }
  return Posting{
      .id = row.column_int64(0),
      .transaction_id = txn.value(),
      .account_id = account.value(),
      .direction = direction.value(),
      .amount = amount.value(),
      .balance_after_minor = row.column_int64(6),
      .created_at = created.value(),
  };
}

Result<AuditEvent> read_audit(const Statement& row) {
  auto at = ts_from(row, 1);
  if (!at) {
    return at.error();
  }
  auto type = parse_audit_event_type(row.column_text(2));
  if (!type) {
    return make_error(ErrorCode::PersistenceError, type.error().message);
  }
  auto batch = optional_id_from<BatchId>(row, 6);
  if (!batch) {
    return batch.error();
  }
  return AuditEvent{
      .id = row.column_int64(0),
      .occurred_at = at.value(),
      .type = type.value(),
      .entity_type = row.column_text(3),
      .entity_id = row.column_text(4),
      .detail = row.column_text(5),
      .batch_id = batch.value(),
  };
}

constexpr std::string_view kBatchColumns =
    "batch_id, business_date, status, attempt, current_stage, cutoff_at, started_at, updated_at, "
    "completed_at, failure_reason";

Result<BatchRun> read_batch(const Statement& row) {
  auto id = id_from<BatchId>(row, 0);
  if (!id) {
    return id.error();
  }
  auto date = BusinessDate::parse(row.column_text(1));
  if (!date) {
    return make_error(ErrorCode::PersistenceError, date.error().message);
  }
  auto status = parse_batch_status(row.column_text(2));
  if (!status) {
    return make_error(ErrorCode::PersistenceError, status.error().message);
  }
  std::optional<BatchStage> current;
  if (!row.column_is_null(4)) {
    auto stage = parse_batch_stage(row.column_text(4));
    if (!stage) {
      return make_error(ErrorCode::PersistenceError, stage.error().message);
    }
    current = stage.value();
  }
  auto cutoff = optional_ts_from(row, 5);
  if (!cutoff) {
    return cutoff.error();
  }
  auto started = ts_from(row, 6);
  if (!started) {
    return started.error();
  }
  auto updated = ts_from(row, 7);
  if (!updated) {
    return updated.error();
  }
  auto completed = optional_ts_from(row, 8);
  if (!completed) {
    return completed.error();
  }
  return BatchRun{
      .id = id.value(),
      .business_date = date.value(),
      .status = status.value(),
      .attempt = static_cast<int>(row.column_int64(3)),
      .current_stage = current,
      .cutoff_at = cutoff.value(),
      .started_at = started.value(),
      .updated_at = updated.value(),
      .completed_at = completed.value(),
      .failure_reason = row.column_text(9),
      .stages = {},
  };
}

Result<BatchStageRecord> read_stage(const Statement& row) {
  auto batch = id_from<BatchId>(row, 0);
  if (!batch) {
    return batch.error();
  }
  auto stage = parse_batch_stage(row.column_text(1));
  if (!stage) {
    return make_error(ErrorCode::PersistenceError, stage.error().message);
  }
  auto status = parse_stage_status(row.column_text(2));
  if (!status) {
    return make_error(ErrorCode::PersistenceError, status.error().message);
  }
  auto started = optional_ts_from(row, 4);
  if (!started) {
    return started.error();
  }
  auto completed = optional_ts_from(row, 5);
  if (!completed) {
    return completed.error();
  }
  return BatchStageRecord{
      .batch_id = batch.value(),
      .stage = stage.value(),
      .status = status.value(),
      .attempts = static_cast<int>(row.column_int64(3)),
      .started_at = started.value(),
      .completed_at = completed.value(),
      .detail = row.column_text(6),
  };
}

Result<DailySummary> read_summary(const Statement& row) {
  auto date = BusinessDate::parse(row.column_text(0));
  if (!date) {
    return make_error(ErrorCode::PersistenceError, date.error().message);
  }
  auto generated = ts_from(row, 8);
  if (!generated) {
    return generated.error();
  }
  auto batch = id_from<BatchId>(row, 9);
  if (!batch) {
    return batch.error();
  }
  return DailySummary{
      .business_date = date.value(),
      .currency = row.column_text(1),
      .posted_count = row.column_int64(2),
      .rejected_count = row.column_int64(3),
      .reversed_count = row.column_int64(4),
      .credit_total_minor = row.column_int64(5),
      .debit_total_minor = row.column_int64(6),
      .transfer_total_minor = row.column_int64(7),
      .generated_at = generated.value(),
      .batch_id = batch.value(),
  };
}

// Collects every row of a query into a vector using `reader`.
template <typename T, typename Reader>
Result<std::vector<T>> collect(Statement& stmt, Reader reader) {
  std::vector<T> out;
  while (true) {
    auto row = stmt.step();
    if (!row) {
      return row.error();
    }
    if (!row.value()) {
      break;
    }
    auto item = reader(stmt);
    if (!item) {
      return item.error();
    }
    out.push_back(std::move(item.value()));
  }
  return out;
}

// ---------------------------------------------------------------------------
// Unit of work
// ---------------------------------------------------------------------------

class SqliteUnitOfWork final : public UnitOfWork {
 public:
  SqliteUnitOfWork(SqliteRepository& repository, std::unique_ptr<Connection> connection,
                   TransactionMode mode)
      : repository_(repository), connection_(std::move(connection)), mode_(mode) {}
  SqliteUnitOfWork(const SqliteUnitOfWork&) = delete;
  SqliteUnitOfWork& operator=(const SqliteUnitOfWork&) = delete;
  SqliteUnitOfWork(SqliteUnitOfWork&&) = delete;
  SqliteUnitOfWork& operator=(SqliteUnitOfWork&&) = delete;

  ~SqliteUnitOfWork() override {
    if (active_) {
      [[maybe_unused]] const auto ignored = connection_->exec("ROLLBACK");
    }
    repository_.release(std::move(connection_));
  }

  [[nodiscard]] Result<void> begin() {
    if (mode_ == TransactionMode::ReadWrite) {
      write_lock_ = std::unique_lock<std::mutex>{repository_.writer_mutex()};
    }
    const auto* const sql =
        mode_ == TransactionMode::ReadWrite ? "BEGIN IMMEDIATE" : "BEGIN DEFERRED";
    if (auto started = connection_->exec(sql); !started) {
      write_lock_ = {};
      return started.error();
    }
    active_ = true;
    return ok();
  }

  Result<void> commit() override {
    if (!active_) {
      return make_error(ErrorCode::PersistenceError, "no active transaction to commit");
    }
    if (repository_.fault() == Fault::FailCommit) {
      [[maybe_unused]] const auto ignored = connection_->exec("ROLLBACK");
      active_ = false;
      write_lock_ = {};
      return make_error(ErrorCode::PersistenceError, "injected commit failure");
    }
    if (auto committed = connection_->exec("COMMIT"); !committed) {
      return committed.error();
    }
    active_ = false;
    write_lock_ = {};
    return ok();
  }

  Result<void> rollback() override {
    if (!active_) {
      return ok();
    }
    active_ = false;
    auto rolled = connection_->exec("ROLLBACK");
    write_lock_ = {};
    return rolled;
  }

  // ---- accounts -----------------------------------------------------------

  Result<std::optional<Account>> find_account(const AccountId& id) override {
    auto stmt = connection_->prepare("SELECT " + std::string{kAccountColumns} +
                                     " FROM accounts WHERE account_id = ?");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, id.view()); !bound) {
      return bound.error();
    }
    auto row = stmt.value().step();
    if (!row) {
      return row.error();
    }
    if (!row.value()) {
      return std::optional<Account>{};
    }
    auto account = read_account(stmt.value());
    if (!account) {
      return account.error();
    }
    return std::optional<Account>{account.value()};
  }

  Result<std::vector<Account>> list_accounts() override {
    auto stmt = connection_->prepare("SELECT " + std::string{kAccountColumns} +
                                     " FROM accounts ORDER BY account_id");
    if (!stmt) {
      return stmt.error();
    }
    return collect<Account>(stmt.value(), read_account);
  }

  Result<void> insert_account(const Account& account) override {
    auto stmt = connection_->prepare("INSERT INTO accounts(" + std::string{kAccountColumns} +
                                     ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    for (const auto& bound :
         {s.bind(1, account.id.view()), s.bind(2, account.name), s.bind(3, to_string(account.type)),
          s.bind(4, account.currency().code()), s.bind(5, account.balance.minor()),
          s.bind(6, account.allow_overdraft), s.bind(7, to_string(account.status)),
          s.bind(8, account.version), s.bind(9, format_timestamp(account.created_at)),
          s.bind(10, format_timestamp(account.updated_at))}) {
      if (!bound) {
        return bound.error();
      }
    }
    auto executed = s.execute();
    if (!executed && executed.error().code == ErrorCode::DuplicateRequest) {
      return make_error(ErrorCode::AccountExists,
                        "account " + account.id.str() + " already exists");
    }
    return executed;
  }

  Result<void> update_account_balance(const AccountId& id, std::int64_t new_balance_minor,
                                      std::int64_t expected_version, Timestamp at) override {
    auto stmt = connection_->prepare(
        "UPDATE accounts SET balance_minor = ?, version = version + 1, updated_at = ? "
        "WHERE account_id = ? AND version = ?");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    for (const auto& bound : {s.bind(1, new_balance_minor), s.bind(2, format_timestamp(at)),
                              s.bind(3, id.view()), s.bind(4, expected_version)}) {
      if (!bound) {
        return bound.error();
      }
    }
    if (auto executed = s.execute(); !executed) {
      return executed.error();
    }
    if (connection_->changes() != 1) {
      return make_error(ErrorCode::PersistenceError,
                        "optimistic lock failure updating account " + id.str());
    }
    return ok();
  }

  // ---- transactions -------------------------------------------------------

  Result<void> insert_transaction(const Transaction& txn) override {
    auto stmt =
        connection_->prepare("INSERT INTO transactions(" + std::string{kTransactionColumns} +
                             ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = bind_transaction(stmt.value(), txn, 1); !bound) {
      return bound.error();
    }
    return stmt.value().execute();
  }

  Result<void> update_transaction(const Transaction& txn) override {
    auto stmt = connection_->prepare(
        "UPDATE transactions SET status = ?, reversed_by = ?, rejection_code = ?, "
        "rejection_reason = ?, updated_at = ? WHERE transaction_id = ?");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    const std::optional<std::string> reversed_by =
        txn.reversed_by ? std::optional<std::string>{txn.reversed_by->str()} : std::nullopt;
    const std::optional<std::string> code =
        txn.rejection ? std::optional<std::string>{std::string{to_string(txn.rejection->code)}}
                      : std::nullopt;
    const std::optional<std::string> reason =
        txn.rejection ? std::optional<std::string>{txn.rejection->message} : std::nullopt;
    for (const auto& bound :
         {s.bind(1, to_string(txn.status)), s.bind_optional(2, reversed_by),
          s.bind_optional(3, code), s.bind_optional(4, reason),
          s.bind(5, format_timestamp(txn.updated_at)), s.bind(6, txn.id.view())}) {
      if (!bound) {
        return bound.error();
      }
    }
    if (auto executed = s.execute(); !executed) {
      return executed.error();
    }
    if (connection_->changes() != 1) {
      return make_error(ErrorCode::PersistenceError,
                        "transaction " + txn.id.str() + " not found for update");
    }
    return ok();
  }

  Result<std::optional<Transaction>> find_transaction(const TransactionId& id) override {
    auto stmt = connection_->prepare("SELECT " + std::string{kTransactionColumns} +
                                     " FROM transactions WHERE transaction_id = ?");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, id.view()); !bound) {
      return bound.error();
    }
    auto row = stmt.value().step();
    if (!row) {
      return row.error();
    }
    if (!row.value()) {
      return std::optional<Transaction>{};
    }
    auto txn = read_transaction(stmt.value());
    if (!txn) {
      return txn.error();
    }
    return std::optional<Transaction>{txn.value()};
  }

  Result<std::vector<Transaction>> list_transactions(const TransactionQuery& query) override {
    std::string sql = "SELECT " + std::string{kTransactionColumns} + " FROM transactions WHERE 1=1";
    if (query.business_date) {
      sql += " AND business_date = ?";
    }
    if (query.status) {
      sql += " AND status = ?";
    }
    if (query.pending_only) {
      sql += " AND status IN ('RECEIVED','VALIDATED','PROCESSING')";
    }
    if (query.created_before) {
      sql += " AND created_at < ?";
    }
    sql += " ORDER BY created_at, transaction_id LIMIT ?";
    auto stmt = connection_->prepare(sql);
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    int index = 1;
    if (query.business_date) {
      if (auto bound = s.bind(index++, query.business_date->str()); !bound) {
        return bound.error();
      }
    }
    if (query.status) {
      if (auto bound = s.bind(index++, to_string(*query.status)); !bound) {
        return bound.error();
      }
    }
    if (query.created_before) {
      if (auto bound = s.bind(index++, format_timestamp(*query.created_before)); !bound) {
        return bound.error();
      }
    }
    if (auto bound = s.bind(index, static_cast<std::int64_t>(query.limit)); !bound) {
      return bound.error();
    }
    return collect<Transaction>(s, read_transaction);
  }

  Result<std::vector<Transaction>> account_transactions(const AccountId& id,
                                                        std::size_t limit) override {
    auto stmt = connection_->prepare(
        "SELECT " + std::string{kTransactionColumns} +
        " FROM transactions WHERE source_account_id = ? OR destination_account_id = ?"
        " ORDER BY created_at DESC, transaction_id DESC LIMIT ?");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    for (const auto& bound : {s.bind(1, id.view()), s.bind(2, id.view()),
                              s.bind(3, static_cast<std::int64_t>(limit))}) {
      if (!bound) {
        return bound.error();
      }
    }
    return collect<Transaction>(s, read_transaction);
  }

  // ---- postings -----------------------------------------------------------

  Result<void> insert_posting(const Posting& posting) override {
    if (repository_.fault() == Fault::FailInsertPosting) {
      return make_error(ErrorCode::PersistenceError, "injected posting insert failure");
    }
    auto stmt = connection_->prepare(
        "INSERT INTO postings(transaction_id, account_id, direction, currency, amount_minor, "
        "balance_after_minor, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    for (const auto& bound :
         {s.bind(1, posting.transaction_id.view()), s.bind(2, posting.account_id.view()),
          s.bind(3, to_string(posting.direction)), s.bind(4, posting.amount.currency().code()),
          s.bind(5, posting.amount.minor()), s.bind(6, posting.balance_after_minor),
          s.bind(7, format_timestamp(posting.created_at))}) {
      if (!bound) {
        return bound.error();
      }
    }
    return s.execute();
  }

  Result<std::vector<Posting>> postings_for_transaction(const TransactionId& id) override {
    auto stmt = connection_->prepare("SELECT " + std::string{kPostingColumns} +
                                     " FROM postings WHERE transaction_id = ? ORDER BY posting_id");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, id.view()); !bound) {
      return bound.error();
    }
    return collect<Posting>(stmt.value(), read_posting);
  }

  Result<std::vector<Posting>> postings_for_account(const AccountId& id,
                                                    std::size_t limit) override {
    auto stmt = connection_->prepare(
        "SELECT " + std::string{kPostingColumns} +
        " FROM postings WHERE account_id = ? ORDER BY posting_id DESC LIMIT ?");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    for (const auto& bound : {s.bind(1, id.view()), s.bind(2, static_cast<std::int64_t>(limit))}) {
      if (!bound) {
        return bound.error();
      }
    }
    return collect<Posting>(s, read_posting);
  }

  Result<std::vector<AccountPostingTotal>> posting_totals_by_account() override {
    auto stmt = connection_->prepare(
        "SELECT a.account_id,"
        " COALESCE(SUM(CASE WHEN p.direction = 'CREDIT' THEN p.amount_minor ELSE 0 END), 0),"
        " COALESCE(SUM(CASE WHEN p.direction = 'DEBIT' THEN p.amount_minor ELSE 0 END), 0),"
        " COUNT(p.posting_id),"
        " COALESCE((SELECT balance_after_minor FROM postings q WHERE q.account_id = a.account_id"
        "           ORDER BY q.posting_id DESC LIMIT 1), 0)"
        " FROM accounts a LEFT JOIN postings p ON p.account_id = a.account_id"
        " GROUP BY a.account_id ORDER BY a.account_id");
    if (!stmt) {
      return stmt.error();
    }
    return collect<AccountPostingTotal>(
        stmt.value(), [](const Statement& row) -> Result<AccountPostingTotal> {
          auto id = id_from<AccountId>(row, 0);
          if (!id) {
            return id.error();
          }
          return AccountPostingTotal{.account_id = id.value(),
                                     .credit_minor = row.column_int64(1),
                                     .debit_minor = row.column_int64(2),
                                     .posting_count = row.column_int64(3),
                                     .last_balance_after_minor = row.column_int64(4)};
        });
  }

  Result<std::vector<TransactionPostingTotal>> posting_totals_by_transaction() override {
    auto stmt = connection_->prepare(
        "SELECT t.transaction_id, t.transaction_type, t.status,"
        " COALESCE(SUM(CASE WHEN p.direction = 'CREDIT' THEN p.amount_minor ELSE 0 END), 0),"
        " COALESCE(SUM(CASE WHEN p.direction = 'DEBIT' THEN p.amount_minor ELSE 0 END), 0),"
        " COUNT(p.posting_id)"
        " FROM transactions t LEFT JOIN postings p ON p.transaction_id = t.transaction_id"
        " GROUP BY t.transaction_id ORDER BY t.transaction_id");
    if (!stmt) {
      return stmt.error();
    }
    return collect<TransactionPostingTotal>(
        stmt.value(), [](const Statement& row) -> Result<TransactionPostingTotal> {
          auto id = id_from<TransactionId>(row, 0);
          if (!id) {
            return id.error();
          }
          auto type = parse_transaction_type(row.column_text(1));
          if (!type) {
            return make_error(ErrorCode::PersistenceError, type.error().message);
          }
          auto status = parse_transaction_status(row.column_text(2));
          if (!status) {
            return make_error(ErrorCode::PersistenceError, status.error().message);
          }
          return TransactionPostingTotal{.transaction_id = id.value(),
                                         .type = type.value(),
                                         .status = status.value(),
                                         .credit_minor = row.column_int64(3),
                                         .debit_minor = row.column_int64(4),
                                         .posting_count = row.column_int64(5)};
        });
  }

  Result<std::vector<CurrencyTotal>> currency_totals(
      const std::optional<BusinessDate>& business_date) override {
    std::string sql =
        "SELECT currency,"
        " SUM(CASE WHEN status IN ('POSTED','REVERSED') THEN 1 ELSE 0 END),"
        " SUM(CASE WHEN status = 'REJECTED' THEN 1 ELSE 0 END),"
        " SUM(CASE WHEN status = 'REVERSED' THEN 1 ELSE 0 END),"
        " COALESCE(SUM(CASE WHEN transaction_type = 'CREDIT' AND status IN ('POSTED','REVERSED')"
        "   THEN amount_minor ELSE 0 END), 0),"
        " COALESCE(SUM(CASE WHEN transaction_type = 'DEBIT' AND status IN ('POSTED','REVERSED')"
        "   THEN amount_minor ELSE 0 END), 0),"
        " COALESCE(SUM(CASE WHEN transaction_type = 'TRANSFER' AND status IN ('POSTED','REVERSED')"
        "   THEN amount_minor ELSE 0 END), 0)"
        " FROM transactions";
    if (business_date) {
      sql += " WHERE business_date = ?";
    }
    sql += " GROUP BY currency ORDER BY currency";
    auto stmt = connection_->prepare(sql);
    if (!stmt) {
      return stmt.error();
    }
    if (business_date) {
      if (auto bound = stmt.value().bind(1, business_date->str()); !bound) {
        return bound.error();
      }
    }
    return collect<CurrencyTotal>(stmt.value(), [](const Statement& row) -> Result<CurrencyTotal> {
      return CurrencyTotal{.currency = row.column_text(0),
                           .posted_count = row.column_int64(1),
                           .rejected_count = row.column_int64(2),
                           .reversed_count = row.column_int64(3),
                           .credit_total_minor = row.column_int64(4),
                           .debit_total_minor = row.column_int64(5),
                           .transfer_total_minor = row.column_int64(6)};
    });
  }

  // ---- idempotency --------------------------------------------------------

  Result<std::optional<IdempotencyRecord>> find_idempotency(const IdempotencyKey& key) override {
    auto stmt = connection_->prepare(
        "SELECT idempotency_key, request_signature, transaction_id, created_at"
        " FROM idempotency_records WHERE idempotency_key = ?");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, key.view()); !bound) {
      return bound.error();
    }
    auto row = stmt.value().step();
    if (!row) {
      return row.error();
    }
    if (!row.value()) {
      return std::optional<IdempotencyRecord>{};
    }
    const auto& s = stmt.value();
    auto txn = id_from<TransactionId>(s, 2);
    if (!txn) {
      return txn.error();
    }
    auto created = ts_from(s, 3);
    if (!created) {
      return created.error();
    }
    return std::optional<IdempotencyRecord>{IdempotencyRecord{.key = key,
                                                              .request_signature = s.column_text(1),
                                                              .transaction_id = txn.value(),
                                                              .created_at = created.value()}};
  }

  Result<void> insert_idempotency(const IdempotencyRecord& record) override {
    auto stmt = connection_->prepare(
        "INSERT INTO idempotency_records(idempotency_key, request_signature, transaction_id, "
        "created_at) VALUES (?, ?, ?, ?)");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    for (const auto& bound : {s.bind(1, record.key.view()), s.bind(2, record.request_signature),
                              s.bind(3, record.transaction_id.view()),
                              s.bind(4, format_timestamp(record.created_at))}) {
      if (!bound) {
        return bound.error();
      }
    }
    return s.execute();
  }

  Result<std::int64_t> count_orphan_idempotency_records() override {
    auto stmt = connection_->prepare(
        "SELECT COUNT(*) FROM idempotency_records i"
        " LEFT JOIN transactions t ON t.transaction_id = i.transaction_id"
        " WHERE t.transaction_id IS NULL");
    if (!stmt) {
      return stmt.error();
    }
    auto row = stmt.value().step();
    if (!row) {
      return row.error();
    }
    return stmt.value().column_int64(0);
  }

  // ---- audit --------------------------------------------------------------

  Result<void> append_audit(const AuditEvent& event) override {
    auto stmt = connection_->prepare(
        "INSERT INTO audit_events(occurred_at, event_type, entity_type, entity_id, detail, "
        "batch_id) VALUES (?, ?, ?, ?, ?, ?)");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    const std::optional<std::string> batch =
        event.batch_id ? std::optional<std::string>{event.batch_id->str()} : std::nullopt;
    for (const auto& bound :
         {s.bind(1, format_timestamp(event.occurred_at)), s.bind(2, to_string(event.type)),
          s.bind(3, event.entity_type), s.bind(4, event.entity_id), s.bind(5, event.detail),
          s.bind_optional(6, batch)}) {
      if (!bound) {
        return bound.error();
      }
    }
    return s.execute();
  }

  Result<std::vector<AuditEvent>> list_audit(const AuditQuery& query) override {
    std::string sql =
        "SELECT event_id, occurred_at, event_type, entity_type, entity_id, detail, batch_id"
        " FROM audit_events WHERE 1=1";
    if (query.entity_id) {
      sql += " AND entity_id = ?";
    }
    if (query.batch_id) {
      sql += " AND batch_id = ?";
    }
    if (query.type) {
      sql += " AND event_type = ?";
    }
    sql += " ORDER BY event_id LIMIT ?";
    auto stmt = connection_->prepare(sql);
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    int index = 1;
    if (query.entity_id) {
      if (auto bound = s.bind(index++, *query.entity_id); !bound) {
        return bound.error();
      }
    }
    if (query.batch_id) {
      if (auto bound = s.bind(index++, query.batch_id->view()); !bound) {
        return bound.error();
      }
    }
    if (query.type) {
      if (auto bound = s.bind(index++, to_string(*query.type)); !bound) {
        return bound.error();
      }
    }
    if (auto bound = s.bind(index, static_cast<std::int64_t>(query.limit)); !bound) {
      return bound.error();
    }
    return collect<AuditEvent>(s, read_audit);
  }

  // ---- batches ------------------------------------------------------------

  Result<std::optional<BatchRun>> find_batch(const BatchId& id) override {
    auto stmt = connection_->prepare("SELECT " + std::string{kBatchColumns} +
                                     " FROM batch_runs WHERE batch_id = ?");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, id.view()); !bound) {
      return bound.error();
    }
    return single_batch(stmt.value());
  }

  Result<std::optional<BatchRun>> latest_batch_for_date(const BusinessDate& date) override {
    auto stmt = connection_->prepare(
        "SELECT " + std::string{kBatchColumns} +
        " FROM batch_runs WHERE business_date = ? ORDER BY started_at DESC, batch_id DESC LIMIT 1");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, date.str()); !bound) {
      return bound.error();
    }
    return single_batch(stmt.value());
  }

  Result<std::optional<BatchRun>> latest_batch() override {
    auto stmt =
        connection_->prepare("SELECT " + std::string{kBatchColumns} +
                             " FROM batch_runs ORDER BY started_at DESC, batch_id DESC LIMIT 1");
    if (!stmt) {
      return stmt.error();
    }
    return single_batch(stmt.value());
  }

  Result<std::vector<BatchRun>> list_batches(std::size_t limit) override {
    auto stmt =
        connection_->prepare("SELECT " + std::string{kBatchColumns} +
                             " FROM batch_runs ORDER BY started_at DESC, batch_id DESC LIMIT ?");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, static_cast<std::int64_t>(limit)); !bound) {
      return bound.error();
    }
    auto runs = collect<BatchRun>(stmt.value(), read_batch);
    if (!runs) {
      return runs.error();
    }
    for (auto& run : runs.value()) {
      if (auto loaded = load_stages(run); !loaded) {
        return loaded.error();
      }
    }
    return runs;
  }

  Result<void> insert_batch(const BatchRun& run) override {
    auto stmt = connection_->prepare("INSERT INTO batch_runs(" + std::string{kBatchColumns} +
                                     ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = bind_batch(stmt.value(), run); !bound) {
      return bound.error();
    }
    if (auto executed = stmt.value().execute(); !executed) {
      return executed.error();
    }
    for (const auto& stage : run.stages) {
      if (auto upserted = upsert_stage(stage); !upserted) {
        return upserted.error();
      }
    }
    return ok();
  }

  Result<void> update_batch(const BatchRun& run) override {
    auto stmt = connection_->prepare(
        "UPDATE batch_runs SET status = ?, attempt = ?, current_stage = ?, cutoff_at = ?, "
        "updated_at = ?, completed_at = ?, failure_reason = ? WHERE batch_id = ?");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    const std::optional<std::string> stage =
        run.current_stage ? std::optional<std::string>{std::string{to_string(*run.current_stage)}}
                          : std::nullopt;
    const std::optional<std::string> cutoff =
        run.cutoff_at ? std::optional<std::string>{format_timestamp(*run.cutoff_at)} : std::nullopt;
    const std::optional<std::string> completed =
        run.completed_at ? std::optional<std::string>{format_timestamp(*run.completed_at)}
                         : std::nullopt;
    for (const auto& bound :
         {s.bind(1, to_string(run.status)), s.bind(2, run.attempt), s.bind_optional(3, stage),
          s.bind_optional(4, cutoff), s.bind(5, format_timestamp(run.updated_at)),
          s.bind_optional(6, completed), s.bind(7, run.failure_reason), s.bind(8, run.id.view())}) {
      if (!bound) {
        return bound.error();
      }
    }
    if (auto executed = s.execute(); !executed) {
      return executed.error();
    }
    if (connection_->changes() != 1) {
      return make_error(ErrorCode::BatchNotFound, "batch " + run.id.str() + " not found");
    }
    return ok();
  }

  Result<void> upsert_stage(const BatchStageRecord& record) override {
    auto stmt = connection_->prepare(
        "INSERT INTO batch_stages(batch_id, stage, status, attempts, started_at, completed_at, "
        "detail) VALUES (?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(batch_id, stage) DO UPDATE SET status = excluded.status,"
        " attempts = excluded.attempts, started_at = excluded.started_at,"
        " completed_at = excluded.completed_at, detail = excluded.detail");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    const std::optional<std::string> started =
        record.started_at ? std::optional<std::string>{format_timestamp(*record.started_at)}
                          : std::nullopt;
    const std::optional<std::string> completed =
        record.completed_at ? std::optional<std::string>{format_timestamp(*record.completed_at)}
                            : std::nullopt;
    for (const auto& bound :
         {s.bind(1, record.batch_id.view()), s.bind(2, to_string(record.stage)),
          s.bind(3, to_string(record.status)), s.bind(4, record.attempts),
          s.bind_optional(5, started), s.bind_optional(6, completed), s.bind(7, record.detail)}) {
      if (!bound) {
        return bound.error();
      }
    }
    return s.execute();
  }

  Result<void> upsert_daily_summary(const DailySummary& summary) override {
    auto stmt = connection_->prepare(
        "INSERT INTO daily_summaries(business_date, currency, posted_count, rejected_count, "
        "reversed_count, credit_total_minor, debit_total_minor, transfer_total_minor, "
        "generated_at, batch_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(business_date, currency) DO UPDATE SET"
        " posted_count = excluded.posted_count, rejected_count = excluded.rejected_count,"
        " reversed_count = excluded.reversed_count,"
        " credit_total_minor = excluded.credit_total_minor,"
        " debit_total_minor = excluded.debit_total_minor,"
        " transfer_total_minor = excluded.transfer_total_minor,"
        " generated_at = excluded.generated_at, batch_id = excluded.batch_id");
    if (!stmt) {
      return stmt.error();
    }
    auto& s = stmt.value();
    for (const auto& bound :
         {s.bind(1, summary.business_date.str()), s.bind(2, summary.currency),
          s.bind(3, summary.posted_count), s.bind(4, summary.rejected_count),
          s.bind(5, summary.reversed_count), s.bind(6, summary.credit_total_minor),
          s.bind(7, summary.debit_total_minor), s.bind(8, summary.transfer_total_minor),
          s.bind(9, format_timestamp(summary.generated_at)), s.bind(10, summary.batch_id.view())}) {
      if (!bound) {
        return bound.error();
      }
    }
    return s.execute();
  }

  Result<std::vector<DailySummary>> daily_summaries(const BusinessDate& date) override {
    auto stmt = connection_->prepare(
        "SELECT business_date, currency, posted_count, rejected_count, reversed_count, "
        "credit_total_minor, debit_total_minor, transfer_total_minor, generated_at, batch_id"
        " FROM daily_summaries WHERE business_date = ? ORDER BY currency");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, date.str()); !bound) {
      return bound.error();
    }
    return collect<DailySummary>(stmt.value(), read_summary);
  }

  Result<std::string> integrity_check() override {
    auto stmt = connection_->prepare("PRAGMA quick_check");
    if (!stmt) {
      return stmt.error();
    }
    auto row = stmt.value().step();
    if (!row) {
      return row.error();
    }
    return row.value() ? stmt.value().column_text(0) : std::string{"no result"};
  }

 private:
  [[nodiscard]] Result<void> bind_transaction(Statement& s, const Transaction& txn, int base) {
    const auto opt = [](const auto& id) -> std::optional<std::string> {
      if (id) {
        return id->str();
      }
      return std::nullopt;
    };
    const std::optional<std::string> code =
        txn.rejection ? std::optional<std::string>{std::string{to_string(txn.rejection->code)}}
                      : std::nullopt;
    const std::optional<std::string> reason =
        txn.rejection ? std::optional<std::string>{txn.rejection->message} : std::nullopt;
    for (const auto& bound :
         {s.bind(base, txn.id.view()), s.bind(base + 1, to_string(txn.type)),
          s.bind(base + 2, to_string(txn.status)), s.bind(base + 3, txn.amount.currency().code()),
          s.bind(base + 4, txn.amount.minor()), s.bind_optional(base + 5, opt(txn.source)),
          s.bind_optional(base + 6, opt(txn.destination)), s.bind(base + 7, txn.reference.str()),
          s.bind_optional(base + 8, opt(txn.idempotency_key)),
          s.bind_optional(base + 9, opt(txn.reversal_of)),
          s.bind_optional(base + 10, opt(txn.reversed_by)), s.bind_optional(base + 11, code),
          s.bind_optional(base + 12, reason), s.bind(base + 13, txn.business_date.str()),
          s.bind(base + 14, format_timestamp(txn.created_at)),
          s.bind(base + 15, format_timestamp(txn.updated_at))}) {
      if (!bound) {
        return bound.error();
      }
    }
    return ok();
  }

  [[nodiscard]] Result<void> bind_batch(Statement& s, const BatchRun& run) {
    const std::optional<std::string> stage =
        run.current_stage ? std::optional<std::string>{std::string{to_string(*run.current_stage)}}
                          : std::nullopt;
    const std::optional<std::string> cutoff =
        run.cutoff_at ? std::optional<std::string>{format_timestamp(*run.cutoff_at)} : std::nullopt;
    const std::optional<std::string> completed =
        run.completed_at ? std::optional<std::string>{format_timestamp(*run.completed_at)}
                         : std::nullopt;
    for (const auto& bound :
         {s.bind(1, run.id.view()), s.bind(2, run.business_date.str()),
          s.bind(3, to_string(run.status)), s.bind(4, run.attempt), s.bind_optional(5, stage),
          s.bind_optional(6, cutoff), s.bind(7, format_timestamp(run.started_at)),
          s.bind(8, format_timestamp(run.updated_at)), s.bind_optional(9, completed),
          s.bind(10, run.failure_reason)}) {
      if (!bound) {
        return bound.error();
      }
    }
    return ok();
  }

  [[nodiscard]] Result<void> load_stages(BatchRun& run) {
    auto stmt = connection_->prepare(
        "SELECT batch_id, stage, status, attempts, started_at, completed_at, detail"
        " FROM batch_stages WHERE batch_id = ?");
    if (!stmt) {
      return stmt.error();
    }
    if (auto bound = stmt.value().bind(1, run.id.view()); !bound) {
      return bound.error();
    }
    auto stages = collect<BatchStageRecord>(stmt.value(), read_stage);
    if (!stages) {
      return stages.error();
    }
    // Present stages in pipeline order regardless of storage order.
    run.stages.clear();
    for (const auto stage : kBatchStages) {
      for (const auto& record : stages.value()) {
        if (record.stage == stage) {
          run.stages.push_back(record);
        }
      }
    }
    return ok();
  }

  [[nodiscard]] Result<std::optional<BatchRun>> single_batch(Statement& stmt) {
    auto row = stmt.step();
    if (!row) {
      return row.error();
    }
    if (!row.value()) {
      return std::optional<BatchRun>{};
    }
    auto run = read_batch(stmt);
    if (!run) {
      return run.error();
    }
    if (auto loaded = load_stages(run.value()); !loaded) {
      return loaded.error();
    }
    return std::optional<BatchRun>{run.value()};
  }

  SqliteRepository& repository_;
  std::unique_ptr<Connection> connection_;
  TransactionMode mode_;
  bool active_{false};
  std::unique_lock<std::mutex> write_lock_;  // held for the lifetime of a write transaction
};

}  // namespace

// ---------------------------------------------------------------------------
// Repository / connection pool
// ---------------------------------------------------------------------------

SqliteRepository::SqliteRepository(std::string path, Options options)
    : path_(std::move(path)), options_(options) {}

SqliteRepository::~SqliteRepository() = default;

Result<std::unique_ptr<SqliteRepository>> SqliteRepository::open(const std::string& path,
                                                                 Options options) {
  if (options.pool_size == 0) {
    options.pool_size = 1;
  }
  const std::filesystem::path parent = std::filesystem::path{path}.parent_path();
  if (!parent.empty()) {
    std::error_code ec;
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      return make_error(ErrorCode::PersistenceError, "cannot create database directory '" +
                                                         parent.string() + "': " + ec.message());
    }
  }
  std::unique_ptr<SqliteRepository> repository(new SqliteRepository(path, options));
  for (std::size_t i = 0; i < options.pool_size; ++i) {
    auto connection = repository->open_connection();
    if (!connection) {
      return connection.error();
    }
    repository->pool_.push_back(std::move(connection.value()));
  }
  return std::unique_ptr<SqliteRepository>{std::move(repository)};
}

Result<std::unique_ptr<Connection>> SqliteRepository::open_connection() const {
  auto connection = Connection::open(path_);
  if (!connection) {
    return connection.error();
  }
  auto& conn = *connection.value();
  for (const std::string& pragma :
       {std::string{"PRAGMA journal_mode = WAL"}, std::string{"PRAGMA synchronous = NORMAL"},
        std::string{"PRAGMA foreign_keys = ON"},
        "PRAGMA busy_timeout = " + std::to_string(options_.busy_timeout_ms)}) {
    if (auto applied = conn.exec(pragma); !applied) {
      return applied.error();
    }
  }
  return std::move(connection.value());
}

std::unique_ptr<Connection> SqliteRepository::acquire() {
  std::unique_lock lock(pool_mutex_);
  pool_available_.wait(lock, [this] { return !pool_.empty(); });
  auto connection = std::move(pool_.back());
  pool_.pop_back();
  ++outstanding_;
  return connection;
}

void SqliteRepository::release(std::unique_ptr<Connection> connection) {
  {
    const std::lock_guard lock(pool_mutex_);
    pool_.push_back(std::move(connection));
    --outstanding_;
  }
  pool_available_.notify_one();
}

Result<std::unique_ptr<UnitOfWork>> SqliteRepository::begin(TransactionMode mode) {
  auto uow = std::make_unique<SqliteUnitOfWork>(*this, acquire(), mode);
  if (auto started = uow->begin(); !started) {
    return started.error();
  }
  return std::unique_ptr<UnitOfWork>{std::move(uow)};
}

Result<void> SqliteRepository::migrate() {
  auto connection = acquire();
  auto applied = apply_schema(*connection);
  release(std::move(connection));
  return applied;
}

Result<void> SqliteRepository::health_check() {
  auto uow = begin(TransactionMode::ReadOnly);
  if (!uow) {
    return uow.error();
  }
  auto check = uow.value()->integrity_check();
  if (!check) {
    return check.error();
  }
  if (check.value() != "ok") {
    return make_error(ErrorCode::PersistenceError, "integrity check failed: " + check.value());
  }
  return ok();
}

}  // namespace corebank::sqlite
