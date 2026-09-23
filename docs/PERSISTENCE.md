# Persistence

The engine stores everything in one SQLite database file. This document covers the repository abstraction the application layer programs against, unit-of-work semantics, the schema and its constraints and triggers, the PRAGMAs, the connection pool, optimistic versioning, migrations, the on-disk file layout and backup guidance, test-only fault injection, and what would have to change to move to a server database such as PostgreSQL.

Related documents: [ARCHITECTURE.md](ARCHITECTURE.md), [CONCURRENCY.md](CONCURRENCY.md), [IDEMPOTENCY.md](IDEMPOTENCY.md), [OPERATIONS.md](OPERATIONS.md), [ADR-0004](ADR/0004-sqlite.md).

## Repository abstraction

`include/corebank/application/repository.hpp` declares two abstract classes. The application and batch layers depend on these only; SQLite is referenced nowhere outside `src/infrastructure/sqlite/` and the composition roots.

```cpp
class Repository {
 public:
  [[nodiscard]] virtual Result<std::unique_ptr<UnitOfWork>> begin(TransactionMode mode) = 0;
  [[nodiscard]] virtual Result<void> migrate() = 0;
  [[nodiscard]] virtual Result<void> health_check() = 0;
};
```

`UnitOfWork` exposes `commit()` and `rollback()` plus one method per query or mutation: account find, list, insert and balance update; transaction insert, update, find, list and per-account list; posting insert and reads; the reconciliation aggregates `posting_totals_by_account`, `posting_totals_by_transaction`, `currency_totals` and `count_orphan_idempotency_records`; idempotency find and insert; audit append and list; batch run, stage and daily summary operations; and `integrity_check`. Every method returns `Result`. Row values are mapped into domain types in the repository, and malformed stored data (an unknown status text, an unparsable timestamp) surfaces as `PersistenceError` rather than a crash.

## Unit of work semantics

`SqliteUnitOfWork` (an anonymous-namespace class in `src/infrastructure/sqlite/sqlite_repository.cpp`) owns one pooled connection for its lifetime.

| Aspect | Behaviour |
| --- | --- |
| Begin | `TransactionMode::ReadWrite` executes `BEGIN IMMEDIATE`; `ReadOnly` executes `BEGIN DEFERRED`. `IMMEDIATE` acquires SQLite's write lock up front, so a writer never discovers at commit time that another writer got there first. |
| Commit | `COMMIT`, then marks the unit inactive. |
| Rollback | `ROLLBACK` if active; a no-op otherwise. |
| Destruction | If still active, executes `ROLLBACK`, then returns the connection to the pool. |

The destructor rule is what makes error handling simple in the service: any early `return error` from inside `TransactionService::post` destroys the `unique_ptr<UnitOfWork>` and discards every statement executed so far, including the `RECEIVED` row and the audit event. `tests/integration/persistence_test.cpp` `UncommittedUnitOfWorkRollsBackOnDestruction` checks this directly, and `tests/integration/rollback_test.cpp` checks it through the service with injected faults.

## Schema

`src/infrastructure/sqlite/schema.cpp` holds the complete DDL as one string (`schema_sql()`), applied by `apply_schema`. `kSchemaVersion` is 1.

### Tables

| Table | Key | Columns |
| --- | --- | --- |
| `schema_version` | `version` | `applied_at` |
| `accounts` | `account_id` | `name`, `account_type` (CHECK in CUSTOMER, SETTLEMENT, FEE_INCOME, SUSPENSE), `currency` (CHECK length 3), `balance_minor`, `allow_overdraft` (CHECK 0 or 1), `status` (CHECK in ACTIVE, FROZEN, CLOSED), `version`, `created_at`, `updated_at`; table CHECK `allow_overdraft = 1 OR balance_minor >= 0` |
| `transactions` | `transaction_id` | `transaction_type` (CHECK in CREDIT, DEBIT, TRANSFER, REVERSAL), `status` (CHECK in the six statuses), `currency`, `amount_minor` (CHECK > 0), `source_account_id`, `destination_account_id`, `reference`, `idempotency_key`, `reversal_of` (FK to `transactions`), `reversed_by`, `rejection_code`, `rejection_reason`, `business_date`, `created_at`, `updated_at` |
| `postings` | `posting_id` AUTOINCREMENT | `transaction_id` (FK), `account_id` (FK), `direction` (CHECK DEBIT or CREDIT), `currency`, `amount_minor` (CHECK > 0), `balance_after_minor`, `created_at` |
| `idempotency_records` | `idempotency_key` | `request_signature`, `transaction_id` (FK), `created_at` |
| `audit_events` | `event_id` AUTOINCREMENT | `occurred_at`, `event_type`, `entity_type`, `entity_id`, `detail`, `batch_id` |
| `batch_runs` | `batch_id` | `business_date`, `status` (CHECK RUNNING, FAILED, COMPLETED), `attempt`, `current_stage`, `cutoff_at`, `started_at`, `updated_at`, `completed_at`, `failure_reason` |
| `batch_stages` | (`batch_id`, `stage`) | `status` (CHECK PENDING, RUNNING, COMPLETED, FAILED), `attempts`, `started_at`, `completed_at`, `detail` |
| `daily_summaries` | (`business_date`, `currency`) | `posted_count`, `rejected_count`, `reversed_count`, `credit_total_minor`, `debit_total_minor`, `transfer_total_minor`, `generated_at`, `batch_id` |

All amounts are `INTEGER` minor units; all timestamps are ISO-8601 `TEXT` in UTC. Enumerations are stored as their upper-case text form so the file is readable with the `sqlite3` shell.

### Indexes

`ix_transactions_business_date (business_date, created_at)`, `ix_transactions_status`, `ix_transactions_source`, `ix_transactions_destination`, `ix_postings_account (account_id, posting_id)`, `ix_postings_transaction`, `ix_audit_entity (entity_id, event_id)`, `ix_audit_batch (batch_id, event_id)`, `ix_batch_runs_date (business_date, started_at)`.

### Immutability triggers

```sql
CREATE TRIGGER IF NOT EXISTS trg_postings_no_update BEFORE UPDATE ON postings
BEGIN SELECT RAISE(ABORT, 'postings are immutable'); END;
...
CREATE TRIGGER IF NOT EXISTS trg_transactions_terminal BEFORE UPDATE OF status ON transactions
WHEN OLD.status IN ('REJECTED','REVERSED')
BEGIN SELECT RAISE(ABORT, 'terminal transactions cannot change status'); END;
```

| Trigger | Rule |
| --- | --- |
| `trg_postings_no_update`, `trg_postings_no_delete` | Postings are never changed or removed. |
| `trg_audit_no_update`, `trg_audit_no_delete` | Audit events are append-only. |
| `trg_idempotency_no_update`, `trg_idempotency_no_delete` | Idempotency records are permanent. |
| `trg_transactions_no_delete` | Transactions are never deleted. |
| `trg_transactions_terminal` | A `REJECTED` or `REVERSED` transaction cannot change status. |
| `trg_accounts_no_delete` | Accounts are never deleted. |

These rules hold for any connection, including an operator's `sqlite3` shell. `tests/integration/audit_test.cpp` opens a raw connection and confirms each one.

## PRAGMAs and connection flags

`SqliteRepository::open_connection` applies the same settings to every pooled connection:

```cpp
for (const std::string& pragma :
     {std::string{"PRAGMA journal_mode = WAL"}, std::string{"PRAGMA synchronous = NORMAL"},
      std::string{"PRAGMA foreign_keys = ON"},
      "PRAGMA busy_timeout = " + std::to_string(options_.busy_timeout_ms)}) {
```

| Setting | Effect |
| --- | --- |
| `journal_mode = WAL` | Readers do not block the writer and the writer does not block readers; commits append to `corebank.db-wal`. |
| `synchronous = NORMAL` | With WAL, the WAL is synced at checkpoints rather than every commit. A power loss can lose the most recent committed transactions but cannot corrupt the database. This is a durability trade-off chosen for an educational engine; `FULL` would sync on every commit. |
| `foreign_keys = ON` | The `REFERENCES` clauses are enforced (SQLite defaults to off). |
| `busy_timeout` | A connection waiting for the write lock retries for up to this many milliseconds (default 5000, from `SqliteOptions`) before failing with `PersistenceError`. Inside one process this rarely triggers: read-write units of work first serialise on `SqliteRepository::writer_mutex()`, so the busy handler only matters when another process holds the lock. |

Connections are opened with `SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX`. `NOMUTEX` disables SQLite's per-connection mutex; it is safe because the pool hands each connection to exactly one unit of work at a time. The engine does not change `wal_autocheckpoint`, so SQLite's default automatic checkpointing applies.

## Connection pool

`SqliteRepository::open` creates the parent directory if needed and opens `pool_size` connections eagerly (default 4; `cmd_serve` uses `worker_count + 2`). `acquire` blocks on a condition variable until a connection is free; `release` returns it and notifies one waiter. A unit of work holds exactly one connection, and no code path holds two at once, so pool waits cannot form a cycle (`tests/integration/persistence_test.cpp` `ConnectionPoolBlocksUntilAConnectionIsReturned`).

## Prepared statements

All SQL goes through `sqlite::Statement` (`src/infrastructure/sqlite/statement.cpp`), which wraps `sqlite3_prepare_v2` and typed `sqlite3_bind_text` / `sqlite3_bind_int64` / `sqlite3_bind_null`. Values are never concatenated into SQL. The only dynamic SQL is the `WHERE` clause assembly in `list_transactions` and `list_audit`, which appends fixed fragments and binds the values. `tests/integration/persistence_test.cpp` `StatementBindingIsTypedAndParameterised` stores a hostile string verbatim to demonstrate this.

`Statement::step` maps `SQLITE_CONSTRAINT_PRIMARYKEY` and `SQLITE_CONSTRAINT_UNIQUE` to `ErrorCode::DuplicateRequest`; `insert_account` translates that to `AccountExists`, and the service translates it on `idempotency_records` to a replay (see [IDEMPOTENCY.md](IDEMPOTENCY.md)).

## Optimistic versioning

Balance updates carry the version the caller read:

```cpp
"UPDATE accounts SET balance_minor = ?, version = version + 1, updated_at = ? "
"WHERE account_id = ? AND version = ?"
```

followed by a `changes() == 1` check that returns `PersistenceError` (`"optimistic lock failure updating account ..."`) when the row's version moved. `TransactionService::apply_legs` reads the account inside the write transaction and passes `account.version`, so under SQLite's single-writer lock the check cannot fail in practice; it exists so that the same code remains correct on a database where two writers can proceed concurrently. `tests/integration/persistence_test.cpp` `OptimisticVersionCheckRejectsStaleWrites` confirms the mechanism.

## Migrations

There is one schema version and one migration function. `apply_schema` runs the DDL, which uses `CREATE ... IF NOT EXISTS` throughout, then:

```cpp
auto stmt =
    connection.prepare("INSERT OR IGNORE INTO schema_version(version, applied_at) VALUES (?, ?)");
```

`Repository::migrate` is therefore idempotent and is called on every `corebank` start (`Context::open_database` in `src/cli/app.cpp`) and by the tests. `tests/integration/persistence_test.cpp` `MigrateIsIdempotentAndRecordsSchemaVersion` runs it twice and checks a single row with `version = kSchemaVersion`.

What is not implemented: versioned upgrade steps, `ALTER TABLE` migrations, or a check that a database newer than the binary is refused. A schema change today means a new `kSchemaVersion`, new DDL guarded by `IF NOT EXISTS` or an explicit upgrade statement, and a corresponding `INSERT OR IGNORE`.

## File layout

With the default configuration the database is `var/corebank.db` relative to the working directory (`database_path`, overridable by `--db` or `COREBANK_DB`). In WAL mode SQLite keeps three files next to each other:

| File | Purpose |
| --- | --- |
| `corebank.db` | The main database. |
| `corebank.db-wal` | The write-ahead log: committed pages not yet checkpointed into the main file. |
| `corebank.db-shm` | Shared-memory index for the WAL, used by concurrent connections. |

`.gitignore` excludes `var/`, `*.db`, `*.db-wal` and `*.db-shm`.

## Backup guidance

Copying `corebank.db` on its own while the engine is running is not a consistent backup, because recent commits may live only in the `-wal` file. Two safe options:

1. Online backup with the SQLite shell, which uses the backup API and is consistent under concurrent writers:

   ```sh
   sqlite3 var/corebank.db ".backup 'var/corebank-backup.db'"
   ```

2. Stop the engine (`scripts/stop.sh`), which lets SQLite checkpoint and remove the WAL on the last close, then copy the main file.

No backup script ships in `scripts/`; the command above is operator guidance. Restoring is a file replacement while the engine is stopped. See [OPERATIONS.md](OPERATIONS.md).

## Test-only fault injection

```cpp
enum class Fault { None, FailCommit, FailInsertPosting };
```

`SqliteRepository::inject_fault` stores the value in an atomic. `SqliteUnitOfWork::commit` with `FailCommit` executes `ROLLBACK` and returns `PersistenceError("injected commit failure")`; `insert_posting` with `FailInsertPosting` returns `PersistenceError` before touching the database. `tests/integration/rollback_test.cpp` uses them to show that a commit failure leaves no transaction, no posting, no audit event and no idempotency record, that a mid-transaction failure rolls back the first leg, and that the engine stays usable afterwards. The CLI never sets a fault.

## What would change for PostgreSQL

The `Repository` and `UnitOfWork` interfaces would stay; a second implementation would replace `src/infrastructure/sqlite/`. The substantive differences:

| Area | SQLite today | Server database |
| --- | --- | --- |
| Write isolation | `BEGIN IMMEDIATE` gives one writer at a time; read-check-write is trivially isolated. | Multiple writers proceed. Use `SELECT ... FOR UPDATE` on the involved account rows, or `SERIALIZABLE` isolation with retry on serialization failure. Neither retry loop exists today. |
| Optimistic version | Defence in depth. | Load-bearing; a `changes() != 1` result becomes a real conflict that must be retried or reported. |
| In-process stripes | Order waits and protect history reads. | Still useful to reduce row-lock contention inside one process; not sufficient across processes. |
| Duplicate keys | `SQLITE_CONSTRAINT_*` extended codes mapped to `DuplicateRequest`. | Map SQLSTATE `23505` instead. |
| Triggers | SQLite trigger syntax with `RAISE(ABORT, ...)`. | Rewrite in the server's procedural language. |
| `AUTOINCREMENT` | Rowid-based. | Identity or sequence columns. |
| `PRAGMA quick_check` | Storage integrity for `health_check` and reconciliation. | No direct equivalent; a lightweight query would stand in. |
| Pool | In-process handles to one file. | Network connections; pool sizing and timeouts become operational settings. |
| `busy_timeout` | Bounded wait for the file lock. | `lock_timeout` / `statement_timeout`. |

None of this is implemented; the table records the design intent behind keeping the repository abstraction and the stripes.
