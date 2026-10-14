# Security

This document describes the security posture of the CoreBank Transaction Engine C++
as it is implemented in this repository. The engine is an independent educational
implementation that processes synthetic data only. It is not a product and it is not
derived from any real bank's systems. Everything below is verifiable in the source;
nothing here is a claim about a production deployment.

For how to report a vulnerability, see [Reporting](#reporting) at the end of this page.

## Threat model

### In scope

The engine is designed to run as a single process on a developer machine, in a local
container, or on a single host behind a trusted network boundary. Within that boundary
the implementation defends against:

| Threat | Control |
|---|---|
| Malformed or oversized wire messages | Protocol parser limits and rejection rules (`src/messaging/protocol.cpp`) |
| Invalid business input (ids, amounts, currencies, references, names) | Validation at every boundary (`src/domain/*.cpp`, `src/infrastructure/config.cpp`) |
| SQL injection | Parameterised statements only (`src/infrastructure/sqlite/statement.cpp`) |
| Tampering with financial history through the database layer | Immutability triggers and CHECK constraints (`src/infrastructure/sqlite/schema.cpp`) |
| Resource exhaustion from clients | Bounded queue, connection limit, idle timeout, line-length limit |
| Running with more privilege than needed | Non-root container user, systemd hardening, restrictive file modes |
| Memory safety bugs | RAII everywhere, strict warnings as errors, sanitizers and static analysis in CI |

### Out of scope

The following are deliberately not implemented. They are listed so that nobody
mistakes the engine for something that can face an untrusted network.

- Authentication and authorisation. There are no users, roles, tokens or passwords.
  Any client that can reach the TCP port can create accounts and post transactions.
- Transport encryption. The wire protocol is plain text over TCP.
- Multi-tenant isolation. All accounts live in one database with one trust domain.
- Denial-of-service resistance beyond the fixed limits described below.
- Tamper evidence for the audit log beyond database triggers. There is no hash chain
  or signature over `audit_events`.
- Secrets management. No secrets exist in the engine, so none are handled.

## Controls that are implemented

### Input validation at every boundary

Every externally supplied value is parsed into a typed value object before it reaches
the service layer. Parsing returns a `Result` and never throws.

| Input | Rule | Source |
|---|---|---|
| Account, transaction, batch ids | 1 to 64 characters, `[A-Za-z0-9-_.]`, must start alphanumeric | `src/domain/identifiers.cpp` |
| Idempotency key | 1 to 128 characters, `[A-Za-z0-9-_.:]` | `include/corebank/domain/identifiers.hpp` |
| Journal reference | up to 140 printable characters, no `|`, CR or LF | `src/domain/identifiers.cpp` |
| Account name | 1 to 100 printable characters, no `|` | `src/domain/account.cpp` |
| Currency | exactly three upper-case letters from an 8-entry table | `src/domain/currency.cpp` |
| Amount | decimal text, no exponent, no more decimals than the currency allows, overflow-checked | `src/domain/money.cpp` |
| Timestamps and business dates | ISO-8601 UTC, calendar-validated via `std::chrono::year_month_day` | `src/domain/time.cpp` |
| Configuration | flat `key: value` only, unknown keys rejected, numeric ranges enforced | `src/infrastructure/config.cpp` |

The wire protocol parser in `src/messaging/protocol.cpp` applies its own limits before
any field is interpreted:

```cpp
inline constexpr std::size_t kDefaultMaxLineBytes = 4096;
inline constexpr std::size_t kMaxFields = 32;
inline constexpr std::size_t kMaxValueBytes = 512;
```

A line is rejected with `PAYLOAD_TOO_LARGE` or `MALFORMED_MESSAGE` when it exceeds the
byte limit, contains any non-printable character, has an empty or malformed type,
contains a field that is not `key=value`, repeats a key, or carries more than 32 fields.
Keys are limited to lower-case letters, digits and underscore. Responses pass through
`sanitize_value`, which replaces `|`, CR and LF so that a stored value can never break
the framing of a reply.

### Bounded resources

- `TransactionProcessor` uses a `BoundedQueue` with a configurable capacity
  (`queue_size`, default 1024). When the queue stays full for the enqueue timeout the
  request fails with `QUEUE_FULL` instead of growing memory
  (`src/application/transaction_processor.cpp`).
- `TcpServer` refuses connections beyond `max_connections` (default 64) with
  `TOO_MANY_CONNECTIONS`, closes idle connections after `idle_timeout_seconds`
  (default 30) and drops a connection whose unterminated input exceeds
  `max_message_bytes` (`src/messaging/tcp_server.cpp`).
- Each request has a server-side completion timeout (`request_timeout_ms`, default
  5000); the client is told to retry with the same idempotency key.
- The service enforces a maximum single-transaction amount
  (`max_transaction_minor`, `src/application/transaction_service.cpp`).

### Database layer

- Every SQL statement uses `sqlite3_prepare_v2` with `?` placeholders and
  `sqlite3_bind_text` / `sqlite3_bind_int64`. No user-supplied text is ever
  concatenated into SQL. The only string concatenation in
  `src/infrastructure/sqlite/sqlite_repository.cpp` splices constant column lists.
- `PRAGMA foreign_keys = ON` is set on every connection.
- The schema in `src/infrastructure/sqlite/schema.cpp` carries CHECK constraints on
  enumerated columns, on `amount_minor > 0`, and on
  `allow_overdraft = 1 OR balance_minor >= 0`.
- Triggers make `postings`, `audit_events` and `idempotency_records` append-only,
  forbid deleting `transactions` and `accounts`, and block status changes on
  terminal (`REJECTED`, `REVERSED`) transactions.
- `PRAGMA quick_check` backs both the `health` command and the first EOD stage.

### Process and filesystem

- The engine listens on `127.0.0.1` by default (`config/default.yaml`,
  `TcpServerOptions`). The Docker image sets `COREBANK_LISTEN_ADDRESS=0.0.0.0` inside
  the container so that the port can be published; `docker-compose.yml` and the CI
  smoke test publish it on `127.0.0.1` only.
- `Dockerfile` builds in one stage and runs in a second stage as user `corebank`
  (uid and gid 10001) with a `nologin` shell; state directories are created with mode
  `0750`.
- `systemd/corebank-engine.service` is an example unit with `NoNewPrivileges`,
  `ProtectSystem=strict`, `ProtectHome`, `PrivateTmp`, `PrivateDevices`,
  `ProtectKernelTunables`, `ProtectControlGroups`,
  `RestrictAddressFamilies=AF_INET AF_UNIX`, `ReadWritePaths` limited to the state
  directory and `UMask=0027`.
- `scripts/lib/common.sh` creates runtime directories with mode `0750`;
  `scripts/start.sh` writes the pid file with mode `0640`.
- End-of-day output files are written with mode `0640`
  (`write_file_0640` in `src/batch/eod_service.cpp`).
- `SignalHandler::install` ignores `SIGPIPE` so that a peer closing mid-write cannot
  terminate the process, and handles `SIGTERM` and `SIGINT` through a self-pipe using
  only async-signal-safe calls (`src/infrastructure/signal_handler.cpp`).

### Logging and data exposure

Logs are JSON lines produced by `src/infrastructure/logger.cpp`. Every string value is
escaped by `json_escape` (`src/infrastructure/json_writer.cpp`), which also encodes
control characters as `\u00XX`. The structured fields used by the engine are
identifiers, amounts, currency codes, status names, error codes, durations and
counters. Account names and journal references are stored in the database and
returned to clients, but they are not written to the log. The `config show` output
and the `ENGINE_STARTED` audit event include the configuration, which contains no
secrets by construction (`Config::describe` comment in
`include/corebank/infrastructure/config.hpp`).

### Memory safety and build hygiene

- Resources are owned by RAII types: `sqlite::Connection`, `sqlite::Statement`,
  `SqliteUnitOfWork` (rolls back on destruction), socket descriptors closed in the
  server loop, `std::jthread` for every thread, `std::unique_lock` and
  `std::shared_lock` guards for every mutex.
- There is no owning raw pointer. The two `new` expressions in the code base are in
  factory functions whose private constructors immediately hand the pointer to a
  `std::unique_ptr`.
- Money arithmetic uses `__builtin_add_overflow`, `__builtin_sub_overflow` and
  `__builtin_mul_overflow` and returns `OVERFLOW` instead of wrapping.
- `cmake/CompilerWarnings.cmake` enables `-Wall -Wextra -Wpedantic -Wshadow
  -Wconversion -Wsign-conversion -Wold-style-cast -Wnull-dereference` and more, with
  `-Werror` on by default.
- `.github/workflows/ci.yml` runs the suite under AddressSanitizer plus
  UndefinedBehaviorSanitizer and separately under ThreadSanitizer, and runs
  clang-tidy, cppcheck, clang-format and ShellCheck as blocking jobs.
- `.github/workflows/codeql.yml` runs CodeQL for C/C++ on every push, pull request
  and weekly. `.github/dependabot.yml` tracks GitHub Actions and the Docker base
  image.

## What a production deployment would add

None of the following exists in this repository. They are listed to make the gap
explicit, not as a roadmap.

| Area | Typical addition |
|---|---|
| Transport | mTLS or an API gateway terminating TLS in front of the engine |
| Identity | Authentication of callers and authorisation per operation and account |
| Audit integrity | Hash chain over audit events, signed with an HSM-backed key |
| Persistence | A multi-writer database such as PostgreSQL with row-level locking |
| Secrets | A secrets manager for database and gateway credentials |
| Abuse control | Per-client rate limiting and quotas |
| Observability | Log shipping to a central store with retention and access control |

## Reporting

Please do not open a public issue for a security problem. Use the private
vulnerability reporting process described in the root [`SECURITY.md`](../SECURITY.md)
(GitHub "Security" tab, "Report a vulnerability"). Include the affected component, a
minimal reproduction and the impact you believe it has.

Related documents: [ARCHITECTURE.md](ARCHITECTURE.md), [PERSISTENCE.md](PERSISTENCE.md),
[LIMITATIONS.md](LIMITATIONS.md), [ADR index](ADR/README.md).
