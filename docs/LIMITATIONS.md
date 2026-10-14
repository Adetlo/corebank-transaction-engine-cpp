# Limitations

This page lists what the CoreBank Transaction Engine C++ does not do, or does only in
a simplified form. The engine is an independent educational implementation that works
on synthetic data. Each limitation is stated as it exists in the code, with the file
that defines the behaviour, so that readers can judge the gap for themselves. Nothing
here is a defect report; these are the boundaries the design chose.

## Deployment model

### Single process, single writer

SQLite allows one writer at a time. Every read-write unit of work opens with
`BEGIN IMMEDIATE` (`src/infrastructure/sqlite/sqlite_repository.cpp`), so concurrent
writers queue on the database's write lock, bounded by `busy_timeout_ms`. Worker
threads add parallelism for validation, locking and reads, but commits are serial.
Throughput does not scale across processes or hosts. See
[ADR-0004](ADR/0004-sqlite.md).

### In-memory coordination is per process

The `IdempotencyGate` (`src/application/idempotency_gate.cpp`) and the 64 lock
stripes of `AccountLockManager` (`src/application/account_lock_manager.cpp`) live in
process memory. A second process that opens the same database file is not covered by
them. It is protected only by:

- the `idempotency_records` primary key, which makes a duplicate insert fail with
  `DUPLICATE_REQUEST` and triggers a replay,
- the optimistic `version` column on `accounts`, which aborts a balance update whose
  expected version no longer matches,
- `BEGIN IMMEDIATE`, which takes the write lock before any read inside the unit of
  work.

Correctness holds, but the second process pays for rolled-back transactions instead
of waiting at the gate.

### One thread per connection

`TcpServer` (`src/messaging/tcp_server.cpp`) starts a `std::jthread` for every
accepted socket. The count is bounded by `max_connections` (default 64) and idle
connections are closed after `idle_timeout_seconds` (default 30), but there is no
event loop and no connection multiplexing.

### Configuration, platforms and toolchains

- No hot reload. Configuration is read once at start-up from the file and from
  `COREBANK_*` variables (`src/infrastructure/config.cpp`). Changing it requires a
  restart.
- No Windows support. Networking and signal handling use POSIX sockets, `poll`,
  `pipe` and `sigaction` (`src/messaging/tcp_server.cpp`,
  `src/infrastructure/signal_handler.cpp`).
- Apple's libc++ ships `std::jthread` behind `-fexperimental-library`;
  `cmake/CompilerWarnings.cmake` adds it on AppleClang. GCC 13 and Clang 18 on Linux
  need nothing.

## Security

The full threat model is in [SECURITY.md](SECURITY.md). In short:

| Limitation | Detail | Where |
|---|---|---|
| No authentication or authorisation | Any client that can reach the port can create accounts and move money. | `src/messaging/message_handler.cpp` |
| No transport encryption | The protocol is plain text over TCP. | `src/messaging/protocol.cpp` |
| Loopback is a default, not an enforcement | `listen_address` defaults to `127.0.0.1` but accepts any IPv4 address. The Docker image sets `0.0.0.0` inside the container. | `config/default.yaml`, `Dockerfile` |
| Archive files are unsigned | End-of-day output is plain CSV and JSON written with mode `0640`. No checksum or signature. | `src/batch/eod_service.cpp` |
| Audit detail is free-form JSON text | `audit_events.detail` is produced by `JsonWriter` and stored as text; it is not validated against a schema on read. | `src/infrastructure/sqlite/schema.cpp` |

## Protocol

### Versioning is a constant

`protocol::kVersion` in `include/corebank/messaging/protocol.hpp` is `"1"` and is
reported by `PING` together with the build version. There is no negotiation and no
compatibility layer for a future version.

### Small message set

`MessageHandler::dispatch` (`src/messaging/message_handler.cpp`) understands `PING`,
`ACCOUNT_CREATE`, `BALANCE`, `TXN_GET`, `CREDIT`, `DEBIT`, `TRANSFER` and `REVERSE`.
There is no message for listing accounts, reading history or audit, running
reconciliation or running the batch; those are CLI-only.

### Timed-out requests still complete

When a request exceeds `request_timeout_ms` (default 5000) the client receives
`ERR|code=TIMEOUT`, but the queued job still runs to completion. The client must
retry with the same idempotency key to learn the outcome (`MessageHandler::await`).

## Domain scope

### No foreign exchange

Each account has exactly one currency and every posting must match it. A credit,
debit or transfer in a different currency is rejected with `CURRENCY_MISMATCH`
(`src/domain/account.cpp`). The currency table in `src/domain/currency.cpp` is
fixed at eight codes (`GBP, USD, EUR, JPY, IDR, SGD, CHF, AUD`); adding one is a
source change.

### Account status cannot be changed

`AccountStatus` has `ACTIVE`, `FROZEN` and `CLOSED`, the schema accepts all three,
and the service rejects operations on inactive accounts with `ACCOUNT_INACTIVE`.
However, neither the CLI (`src/cli/commands_account.cpp` offers `create`, `show`,
`history`, `list`) nor the protocol exposes a way to change the status, and
`UnitOfWork` (`include/corebank/application/repository.hpp`) has no method for it.
Every account created through the engine stays `ACTIVE`. There is consequently no
closure workflow: no balance sweep, no closure date, no reopening.

### Reversal rules are strict

`TransactionService::reverse` (`src/application/transaction_service.cpp`) only
accepts a `POSTED` original, refuses to reverse a `REVERSAL`, and always moves the
full original amount by swapping the legs. Partial reversals and reversals of
reversals do not exist.

### Business date is the UTC calendar day

`BusinessDate::from_timestamp` (`src/domain/time.cpp`) takes the first ten characters
of the UTC timestamp. There is no holiday calendar, no end-of-day cut-off time and no
time-zone handling. `--business-date` and `COREBANK_BUSINESS_DATE` override the date
for demos and tests only.

### Idempotency keys never expire

`idempotency_records` is append-only by trigger and nothing removes rows. Storage
grows with every financial request. See [ADR-0005](ADR/0005-idempotency-design.md).

### Single amount cap

`max_transaction_minor` is one value in minor units applied regardless of currency
(`ServiceLimits` in `include/corebank/application/transaction_service.hpp`).

## End-of-day batch

### No scheduler

The batch runs when an operator or cron invokes `scripts/run_eod.sh` or
`corebank eod run`. The engine does not trigger it and the server process does not
know whether a batch is running.

### `VALIDATE_PENDING` only matters after a crash

The service never commits a transaction in `RECEIVED`, `VALIDATED` or `PROCESSING`;
those states exist only inside one database transaction. The stage
(`EodService::stage_validate_pending` in `src/batch/eod_service.cpp`) can only find
rows left by a process that died mid-transaction, which SQLite rolls back anyway, or
rows written by an external tool. In normal operation it rejects zero transactions.

### Cut-off is informational

`TRANSACTION_CUTOFF` records `cutoff_at` and counts the day's transactions. It does
not stop the engine from posting more transactions for that business date after the
batch has run.

### Whole-day data is loaded in memory

`ARCHIVE` lists up to ten million transactions for the date into a vector before
writing `transactions.csv`. The limit is a constant in `stage_archive`.

### One batch per date, resumed in place

A `FAILED` or `RUNNING` run for the same date is resumed, never replaced; a
`COMPLETED` date is a no-op. See [ADR-0006](ADR/0006-eod-restart-model.md).

## Testing and performance

- Benchmark numbers are local and synthetic. `corebank-bench` (`src/bench/main.cpp`)
  measures this machine against one SQLite file with generated accounts. The results
  describe nothing beyond that run and are not published as targets.
- Property tests are seeded, not exhaustive. `tests/property/invariants_test.cpp`
  runs deterministic randomised scenarios; it is not a model checker.
- Fault injection is limited. Persistence faults are `FailCommit` and
  `FailInsertPosting` (`include/corebank/infrastructure/sqlite/sqlite_repository.hpp`);
  batch faults are one named stage via `COREBANK_FAIL_STAGE`. Disk-full, partial
  writes and network partitions are not simulated.

## Operational

- Shell scripts assume bash (`set -euo pipefail`, arrays, `${PIPESTATUS[0]}`).
  `scripts/lib/common.sh` provides `with_timeout` because coreutils `timeout` is
  absent on macOS.
- `scripts/start.sh` and `scripts/stop.sh` manage a background process through a pid
  file. For a real supervisor use the example unit in `systemd/`.
- Logs go to stderr or to one append-only file (`src/infrastructure/logger.cpp`).
  There is no rotation and no shipping.

## Related documents

- [ARCHITECTURE.md](ARCHITECTURE.md) for the layer boundaries these limitations sit on.
- [CONCURRENCY.md](CONCURRENCY.md) for why the lock stripes and gate are per process.
- [PERSISTENCE.md](PERSISTENCE.md) for the SQLite single-writer model.
- [EOD_BATCH.md](EOD_BATCH.md) for stage semantics.
- [SECURITY.md](SECURITY.md) for the threat model.
- [ADR index](ADR/README.md) for the decisions behind each trade-off.
