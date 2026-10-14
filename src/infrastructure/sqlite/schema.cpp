#include "corebank/infrastructure/sqlite/schema.hpp"

#include "corebank/domain/time.hpp"
#include "corebank/infrastructure/sqlite/connection.hpp"
#include "corebank/infrastructure/sqlite/statement.hpp"

#include <string_view>

namespace corebank::sqlite {
namespace {

constexpr std::string_view kSchema = R"SQL(
CREATE TABLE IF NOT EXISTS schema_version (
  version    INTEGER PRIMARY KEY,
  applied_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS accounts (
  account_id      TEXT PRIMARY KEY,
  name            TEXT NOT NULL,
  account_type    TEXT NOT NULL CHECK (account_type IN ('CUSTOMER','SETTLEMENT','FEE_INCOME','SUSPENSE')),
  currency        TEXT NOT NULL CHECK (length(currency) = 3),
  balance_minor   INTEGER NOT NULL DEFAULT 0,
  allow_overdraft INTEGER NOT NULL DEFAULT 0 CHECK (allow_overdraft IN (0, 1)),
  status          TEXT NOT NULL CHECK (status IN ('ACTIVE','FROZEN','CLOSED')),
  version         INTEGER NOT NULL DEFAULT 0,
  created_at      TEXT NOT NULL,
  updated_at      TEXT NOT NULL,
  CHECK (allow_overdraft = 1 OR balance_minor >= 0)
);

CREATE TABLE IF NOT EXISTS transactions (
  transaction_id         TEXT PRIMARY KEY,
  transaction_type       TEXT NOT NULL CHECK (transaction_type IN ('CREDIT','DEBIT','TRANSFER','REVERSAL')),
  status                 TEXT NOT NULL CHECK (status IN ('RECEIVED','VALIDATED','PROCESSING','POSTED','REJECTED','REVERSED')),
  currency               TEXT NOT NULL CHECK (length(currency) = 3),
  amount_minor           INTEGER NOT NULL CHECK (amount_minor > 0),
  source_account_id      TEXT,
  destination_account_id TEXT,
  reference              TEXT NOT NULL,
  idempotency_key        TEXT,
  reversal_of            TEXT REFERENCES transactions(transaction_id),
  reversed_by            TEXT,
  rejection_code         TEXT,
  rejection_reason       TEXT,
  business_date          TEXT NOT NULL,
  created_at             TEXT NOT NULL,
  updated_at             TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_transactions_business_date ON transactions(business_date, created_at);
CREATE INDEX IF NOT EXISTS ix_transactions_status ON transactions(status);
CREATE INDEX IF NOT EXISTS ix_transactions_source ON transactions(source_account_id);
CREATE INDEX IF NOT EXISTS ix_transactions_destination ON transactions(destination_account_id);

CREATE TABLE IF NOT EXISTS postings (
  posting_id          INTEGER PRIMARY KEY AUTOINCREMENT,
  transaction_id      TEXT NOT NULL REFERENCES transactions(transaction_id),
  account_id          TEXT NOT NULL REFERENCES accounts(account_id),
  direction           TEXT NOT NULL CHECK (direction IN ('DEBIT','CREDIT')),
  currency            TEXT NOT NULL CHECK (length(currency) = 3),
  amount_minor        INTEGER NOT NULL CHECK (amount_minor > 0),
  balance_after_minor INTEGER NOT NULL,
  created_at          TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS ix_postings_account ON postings(account_id, posting_id);
CREATE INDEX IF NOT EXISTS ix_postings_transaction ON postings(transaction_id);

CREATE TABLE IF NOT EXISTS idempotency_records (
  idempotency_key   TEXT PRIMARY KEY,
  request_signature TEXT NOT NULL,
  transaction_id    TEXT NOT NULL REFERENCES transactions(transaction_id),
  created_at        TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS audit_events (
  event_id    INTEGER PRIMARY KEY AUTOINCREMENT,
  occurred_at TEXT NOT NULL,
  event_type  TEXT NOT NULL,
  entity_type TEXT NOT NULL,
  entity_id   TEXT NOT NULL,
  detail      TEXT NOT NULL,
  batch_id    TEXT
);
CREATE INDEX IF NOT EXISTS ix_audit_entity ON audit_events(entity_id, event_id);
CREATE INDEX IF NOT EXISTS ix_audit_batch ON audit_events(batch_id, event_id);

CREATE TABLE IF NOT EXISTS batch_runs (
  batch_id       TEXT PRIMARY KEY,
  business_date  TEXT NOT NULL,
  status         TEXT NOT NULL CHECK (status IN ('RUNNING','FAILED','COMPLETED')),
  attempt        INTEGER NOT NULL DEFAULT 1,
  current_stage  TEXT,
  cutoff_at      TEXT,
  started_at     TEXT NOT NULL,
  updated_at     TEXT NOT NULL,
  completed_at   TEXT,
  failure_reason TEXT NOT NULL DEFAULT ''
);
CREATE INDEX IF NOT EXISTS ix_batch_runs_date ON batch_runs(business_date, started_at);

CREATE TABLE IF NOT EXISTS batch_stages (
  batch_id     TEXT NOT NULL REFERENCES batch_runs(batch_id),
  stage        TEXT NOT NULL,
  status       TEXT NOT NULL CHECK (status IN ('PENDING','RUNNING','COMPLETED','FAILED')),
  attempts     INTEGER NOT NULL DEFAULT 0,
  started_at   TEXT,
  completed_at TEXT,
  detail       TEXT NOT NULL DEFAULT '',
  PRIMARY KEY (batch_id, stage)
);

CREATE TABLE IF NOT EXISTS daily_summaries (
  business_date        TEXT NOT NULL,
  currency             TEXT NOT NULL,
  posted_count         INTEGER NOT NULL,
  rejected_count       INTEGER NOT NULL,
  reversed_count       INTEGER NOT NULL,
  credit_total_minor   INTEGER NOT NULL,
  debit_total_minor    INTEGER NOT NULL,
  transfer_total_minor INTEGER NOT NULL,
  generated_at         TEXT NOT NULL,
  batch_id             TEXT NOT NULL,
  PRIMARY KEY (business_date, currency)
);

-- Immutability: financial history and audit are append-only at the storage level too.
CREATE TRIGGER IF NOT EXISTS trg_postings_no_update BEFORE UPDATE ON postings
BEGIN SELECT RAISE(ABORT, 'postings are immutable'); END;
CREATE TRIGGER IF NOT EXISTS trg_postings_no_delete BEFORE DELETE ON postings
BEGIN SELECT RAISE(ABORT, 'postings are immutable'); END;
CREATE TRIGGER IF NOT EXISTS trg_audit_no_update BEFORE UPDATE ON audit_events
BEGIN SELECT RAISE(ABORT, 'audit events are immutable'); END;
CREATE TRIGGER IF NOT EXISTS trg_audit_no_delete BEFORE DELETE ON audit_events
BEGIN SELECT RAISE(ABORT, 'audit events are immutable'); END;
CREATE TRIGGER IF NOT EXISTS trg_idempotency_no_update BEFORE UPDATE ON idempotency_records
BEGIN SELECT RAISE(ABORT, 'idempotency records are immutable'); END;
CREATE TRIGGER IF NOT EXISTS trg_idempotency_no_delete BEFORE DELETE ON idempotency_records
BEGIN SELECT RAISE(ABORT, 'idempotency records are immutable'); END;
CREATE TRIGGER IF NOT EXISTS trg_transactions_no_delete BEFORE DELETE ON transactions
BEGIN SELECT RAISE(ABORT, 'transactions are never deleted'); END;
CREATE TRIGGER IF NOT EXISTS trg_transactions_terminal BEFORE UPDATE OF status ON transactions
WHEN OLD.status IN ('REJECTED','REVERSED')
BEGIN SELECT RAISE(ABORT, 'terminal transactions cannot change status'); END;
CREATE TRIGGER IF NOT EXISTS trg_accounts_no_delete BEFORE DELETE ON accounts
BEGIN SELECT RAISE(ABORT, 'accounts are never deleted'); END;
)SQL";

}  // namespace

std::string_view schema_sql() {
  return kSchema;
}

Result<void> apply_schema(Connection& connection) {
  if (auto applied = connection.exec(kSchema); !applied) {
    return applied.error();
  }
  auto stmt =
      connection.prepare("INSERT OR IGNORE INTO schema_version(version, applied_at) VALUES (?, ?)");
  if (!stmt) {
    return stmt.error();
  }
  if (auto bound = stmt.value().bind(1, kSchemaVersion); !bound) {
    return bound.error();
  }
  if (auto bound = stmt.value().bind(2, format_timestamp(SystemClock{}.now())); !bound) {
    return bound.error();
  }
  return stmt.value().execute();
}

}  // namespace corebank::sqlite
