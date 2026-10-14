# ADR-0004: SQLite in WAL mode behind a repository interface

**Status:** Accepted (2026-10-14)

## Context

The engine must persist accounts, transactions, postings, idempotency records, audit
events and batch state durably, with real transactions and real constraints, and it
must do so on any contributor's machine and in CI without provisioning a database
server. Tests should exercise the actual storage engine, not an in-memory fake, so
that constraint and rollback behaviour is tested rather than assumed.

## Decision

Use SQLite through the system `libsqlite3`, configured per connection in
`SqliteRepository::open_connection` (`src/infrastructure/sqlite/sqlite_repository.cpp`):

```cpp
"PRAGMA journal_mode = WAL"
"PRAGMA synchronous = NORMAL"
"PRAGMA foreign_keys = ON"
"PRAGMA busy_timeout = " + std::to_string(options_.busy_timeout_ms)
```

- A small pool of connections (`pool_size`, set to `worker_count + 2` by the CLI) is
  handed out by `acquire` / `release`; each `UnitOfWork` owns one connection for its
  lifetime and returns it on destruction.
- Read-write units open with `BEGIN IMMEDIATE`; read-only units with
  `BEGIN DEFERRED`. Destroying an uncommitted unit of work issues `ROLLBACK`.
- All SQL goes through `sqlite::Statement`, which binds with `sqlite3_bind_text` and
  `sqlite3_bind_int64`. Constraint violations on a primary key or unique index are
  mapped to `DUPLICATE_REQUEST`.
- The schema (`src/infrastructure/sqlite/schema.cpp`) carries CHECK constraints on
  every enumerated column and on amounts, foreign keys between postings,
  transactions and accounts, and triggers that make `postings`, `audit_events` and
  `idempotency_records` append-only and forbid deleting transactions or accounts.
- `PRAGMA quick_check` implements `integrity_check`, used by `corebank health` and by
  the `PRECHECK` batch stage.

The domain and application layers never see SQLite. They depend on the abstract
`Repository` and `UnitOfWork` in `include/corebank/application/repository.hpp`, whose
methods return `Result` values and whose only SQLite implementation lives under
`src/infrastructure/sqlite/`.

## Consequences

Positive:

- Zero external services: `apt-get install libsqlite3-dev` (or the preinstalled
  library on macOS) is the whole setup. CI, Docker and local runs are identical.
- Every integration test runs against a real database file in a temporary directory
  (`tests/support/test_support.hpp`), so triggers, foreign keys and rollback are
  tested for real (`tests/integration/persistence_test.cpp`,
  `tests/integration/rollback_test.cpp`).
- WAL mode lets readers proceed while a writer commits, which keeps balance queries
  responsive during posting.
- The repository interface keeps the service code portable to another engine.

Negative:

- SQLite has one writer at a time. Every `BEGIN IMMEDIATE` queues behind the current
  writer, bounded by `busy_timeout`. Throughput does not scale across processes or
  hosts (see [LIMITATIONS.md](../LIMITATIONS.md)).
- `synchronous = NORMAL` in WAL mode can lose the most recent transactions on a
  power failure (not on a process crash). This is a deliberate trade for an
  educational engine and is not suitable for a real ledger.
- There is no row-level locking, so the application-level stripes in
  [ADR-0003](0003-locking-strategy.md) carry more of the concurrency burden than they
  would with a server database.
- Schema migration is a single `CREATE ... IF NOT EXISTS` script with a
  `schema_version` row; there is no incremental migration framework.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| PostgreSQL | Would add row locks, `SERIALIZABLE` isolation and multi-writer scalability, but requires a running server for every build and test, which conflicts with the reproducibility goal. The repository interface leaves the door open. |
| In-memory repository for tests, SQLite for runtime | Tests would not exercise constraints, triggers or rollback; a fake that passes is not evidence that the database behaviour is right. |
| Embedded key-value store (LMDB, RocksDB) | No SQL constraints or triggers; the immutability and CHECK rules would have to be reimplemented in application code. |
| `journal_mode = DELETE` with `synchronous = FULL` | Safer on power loss but blocks readers during writes; WAL plus NORMAL was chosen for responsiveness in an educational setting. |

## References

- `include/corebank/application/repository.hpp`
- `include/corebank/infrastructure/sqlite/sqlite_repository.hpp`, `src/infrastructure/sqlite/sqlite_repository.cpp`
- `include/corebank/infrastructure/sqlite/connection.hpp`, `src/infrastructure/sqlite/connection.cpp`
- `include/corebank/infrastructure/sqlite/statement.hpp`, `src/infrastructure/sqlite/statement.cpp`
- `src/infrastructure/sqlite/schema.cpp`
- `tests/integration/persistence_test.cpp`, `tests/integration/rollback_test.cpp`
- [PERSISTENCE.md](../PERSISTENCE.md), [TESTING.md](../TESTING.md), [SECURITY.md](../SECURITY.md)
- [ADR-0003](0003-locking-strategy.md)
