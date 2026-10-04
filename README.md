# CoreBank Transaction Engine C++

Production-style educational C++ transaction-processing engine demonstrating
financial-system correctness, concurrency, batch processing and Linux operational
automation.

Built with C++20, CMake and SQLite, it posts credits, debits, transfers and reversals
through an idempotent, audited, double-entry journal; runs them through a bounded worker
pool behind a TCP line protocol; reconciles the ledger against its own postings; and
closes the day with a restartable end-of-day batch driven by bash scripts.

> **Disclaimer.** This is an independent educational implementation using synthetic data
> and generic banking concepts. It is not derived from or representative of any employer
> production system.

## What it demonstrates

| Area | Where to look |
| --- | --- |
| Strong domain types (`Money`, `CurrencyCode`, `AccountId`, `TransactionId`, `IdempotencyKey`, `JournalReference`) | `include/corebank/domain/` |
| Explicit error model (`Result<T>`, stable `ErrorCode` names) instead of exceptions | `include/corebank/domain/result.hpp` |
| Transaction service: validation, idempotency, locking, posting, reversal | `src/application/transaction_service.cpp` |
| Worker pool with bounded queue and graceful shutdown | `src/application/transaction_processor.cpp` |
| Repository abstraction with a SQLite implementation (WAL, triggers, optimistic versioning) | `include/corebank/application/repository.hpp`, `src/infrastructure/sqlite/` |
| Reconciliation and end-of-day batch | `src/application/reconciliation_service.cpp`, `src/batch/eod_service.cpp` |
| TCP line protocol, parser, handler, server and client (POSIX sockets, no Boost) | `src/messaging/` |
| Signal handling, structured logging, configuration | `src/infrastructure/` |
| CLI with exit-code contract | `src/cli/` |
| Operational scripts, systemd unit, Docker | `scripts/`, `systemd/`, `Dockerfile`, `docker-compose.yml` |
| Unit, integration, concurrency, batch, messaging, property and CLI suites | `tests/` |

## Architecture

Layered and dependency-ruled: `domain` knows nothing about SQLite or sockets,
`application` depends on the domain and the repository interfaces, `infrastructure`
implements them, `messaging`, `batch` and `cli` sit on top. A single static library
(`corebank_core`) is shared by the CLI, the benchmark and every test.

```
include/corebank/   public headers by layer (domain, application, infrastructure, messaging, batch, cli)
src/                implementation, same layout, plus bench/
tests/              unit, integration, concurrency, batch, messaging, property, cli, support
scripts/            build, test, start, stop, healthcheck, run_eod, reconcile, smoke_test, benchmark, clean, demo
config/             default.yaml (flat key: value, overridden by COREBANK_* environment variables)
systemd/            corebank-engine.service (example)
docs/               architecture, domain model, concurrency, idempotency, persistence, testing, security, limitations
```

Design notes: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/DOMAIN_MODEL.md](docs/DOMAIN_MODEL.md), [docs/CONCURRENCY.md](docs/CONCURRENCY.md),
[docs/IDEMPOTENCY.md](docs/IDEMPOTENCY.md), [docs/PERSISTENCE.md](docs/PERSISTENCE.md),
[docs/TESTING.md](docs/TESTING.md), [docs/SECURITY.md](docs/SECURITY.md),
[docs/LIMITATIONS.md](docs/LIMITATIONS.md).

## Build

Requirements: a C++20 compiler (GCC 13+ or Clang 16+; Apple clang works with the flag
CMake adds automatically), CMake 3.22+, SQLite 3 headers, bash. GoogleTest 1.17 is
fetched at configure time.

```bash
# Debian / Ubuntu
sudo apt-get install build-essential cmake ninja-build libsqlite3-dev

scripts/build.sh                 # Release build into ./build
scripts/test.sh                  # whole suite, or scripts/test.sh <label>
scripts/build.sh --debug --asan --ubsan && scripts/test.sh
scripts/build.sh --debug --tsan && scripts/test.sh concurrency
```

Or directly: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build
build && ctest --test-dir build`.

## Quick start

```bash
export COREBANK_DB=/tmp/corebank/engine.db COREBANK_BATCH_DIR=/tmp/corebank/batch
B=build/corebank

$B account create --id SETTLEMENT-GBP --name "Settlement Account" --currency GBP --type SETTLEMENT
$B account create --id CUSTOMER-A --name "Customer A" --currency GBP
$B transaction credit --account SETTLEMENT-GBP --amount 1000000.00 --currency GBP --key OPEN-1
$B transfer --from SETTLEMENT-GBP --to CUSTOMER-A --amount 250.50 --currency GBP --key T-1 --reference payroll
$B transfer --from SETTLEMENT-GBP --to CUSTOMER-A --amount 250.50 --currency GBP --key T-1   # replayed
$B account history --id CUSTOMER-A
$B reconcile                      # exit 0 when every check passes, 3 otherwise
$B eod run && $B eod status
$B audit --limit 20

$B serve &                        # TCP listener on 127.0.0.1:7400
$B send --host 127.0.0.1 --port 7400 "TRANSFER|key=T-2|from=SETTLEMENT-GBP|to=CUSTOMER-A|amount=10.00|ccy=GBP"
$B health --host 127.0.0.1 --port 7400
kill -TERM %1                     # graceful stop
```

Docker: `docker compose up -d` then `docker compose run --rm demo`; or
`docker build -t corebank .` and `docker run -p 127.0.0.1:7400:7400 corebank`.

## Shell automation

Eleven scripts share `scripts/lib/common.sh`: `set -euo pipefail`, one environment
contract (`COREBANK_HOME`, `BUILD_DIR`, `COREBANK_DB`, `COREBANK_BATCH_DIR`,
`COREBANK_LOG_DIR`, `COREBANK_PID_FILE`, `COREBANK_HOST`, `COREBANK_PORT`), structured
`ts= level= script= msg=` logging to stderr and documented exit codes
(0 ok, 1 usage, 2 business rejection, 3 control failure, 4 system error).

| Script | Purpose |
| --- | --- |
| `build.sh` | CMake configure and build (`--debug`, `--asan`, `--ubsan`, `--tsan`, `--coverage`, `--no-tests`) |
| `test.sh [label]` | ctest, whole suite or one category |
| `start.sh` / `stop.sh` / `healthcheck.sh` | engine lifecycle and monitoring |
| `run_eod.sh [date]` / `reconcile.sh [date]` | daily controls with per-run logs under `COREBANK_LOG_DIR` |
| `smoke_test.sh` | end-to-end checks on a throw-away database (CLI, TCP, EOD) |
| `benchmark.sh` | local synthetic benchmark, JSON output |
| `clean.sh [--all]` | remove build output and, optionally, runtime state |
| `demo.sh` | guided demo: build, start, accounts, transfers, concurrent load, reconcile, EOD, stop |

## Testing and quality gates

Test categories are ctest labels: `unit`, `integration`, `concurrency`, `batch`,
`messaging`, `property`, `cli`. CI builds with GCC and Clang, checks `clang-format`,
runs clang-tidy, cppcheck and ShellCheck, runs the whole suite under ASan + UBSan and
the thread-sensitive suites under ThreadSanitizer, builds and smoke-tests the Docker
image, and runs CodeQL. Property-style tests use seeded `std::mt19937` instead of a
RapidCheck dependency. Details: [docs/TESTING.md](docs/TESTING.md).

## Limitations

Single-process SQLite with one writer at a time; no authentication or TLS; plain-text
protocol; no FX or multi-currency transfers; reversals are whole and single-level; EOD is
run by an operator or scheduler, not by the engine; idempotency keys never expire. The
full list with reasoning is in [docs/LIMITATIONS.md](docs/LIMITATIONS.md).

## License

MIT, see [LICENSE](LICENSE).
