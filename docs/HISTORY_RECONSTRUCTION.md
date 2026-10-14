# History reconstruction

This repository was originally developed over an extended period beginning in 2024 and was later
consolidated and published to GitHub. The Git history was reconstructed from the final project
structure and known development phases. Historical commit dates represent approximate development
phases rather than forensic original commit timestamps.

## Reconstructed phases

| Period (approx.) | Phase | What it added |
| --- | --- | --- |
| Dec 2025 | P1 Project structure, P2 Money and identifiers | CMake 3.22 / C++20 skeleton, shared warning set, clang-format, MIT licence; `Error`, `Result<T>`, `CurrencyCode`, business-date/time utilities, `Money`, strongly typed identifiers |
| Jan 2026 | P2 State machine, P3 Repository and SQLite, P4 Transaction processing | `Transaction` state machine, `Posting`, `Account`, audit events; repository interface, SQLite connection/statement wrappers, schema with WAL and immutability triggers, SQLite repository; request types, ID generator, transaction service |
| Feb-Mar 2026 | P5 Idempotency, P6 Concurrency and infrastructure | JSON-lines logger, JSON writer, flat YAML configuration with `config/default.yaml`; idempotency gate; striped account lock manager; bounded queue; worker-pool transaction processor |
| Apr-May 2026 | P7 Reconciliation, P8 TCP messaging, P9 CLI | Reconciliation model and service; wire protocol, message handler, POSIX TCP server and client, errno text; CLI argument parser, output formatting, `corebank` binary with account, transaction, ops and serve commands |
| Jun-Jul 2026 | P10 EOD batch, P11 Linux operations, P12 Docker | Batch run/stage model, restartable end-of-day service; signal handler; `scripts/lib/common.sh` and the start/stop/healthcheck/build/clean/test/run_eod/reconcile/demo scripts; systemd unit; Dockerfile, compose file, dockerignore |
| Jul-Aug 2026 | P13 Testing, P14 Sanitizers | GoogleTest harness and support library; unit, integration, concurrency, batch, messaging, property and CLI suites; shell smoke test; ASan/UBSan/TSan/coverage build options |
| Aug-Sep 2026 | CI, P15 Static analysis, P16 Benchmarking | GitHub Actions build/test/format workflow, sanitizer and Docker jobs, clang-tidy/cppcheck/ShellCheck gates, CodeQL workflow; `corebank-bench` and `scripts/benchmark.sh` |
| Sep-Oct 2026 | P17 Documentation | Architecture, domain model, persistence, concurrency, idempotency, testing, security and limitations guides; expanded README |
| Oct 2026 | Publication and release | Operations, shell automation, EOD batch and benchmark guides; community health files and Dependabot; ADRs 0001-0007; refreshed GitHub Actions versions; `release: prepare v1.0.0` and the `v1.0.0` tag |

## What is and is not authentic

- **File contents are authentic.** Every tracked file is committed exactly once at its
  published content. The only exceptions are configuration and documentation files that
  received hand-written intermediate versions so that early commits do not reference
  modules that did not exist yet: `CMakeLists.txt` (project version `0.1.0`, source list
  grown as modules arrived, test/sanitizer/benchmark sections added with those features),
  `tests/CMakeLists.txt` (test list grown per suite), `.github/workflows/ci.yml` (jobs
  added incrementally, older action versions until the refresh commit),
  `.github/workflows/codeql.yml` (older checkout action) and `README.md` (three versions).
  Their final versions are the published files.
- **Commit dates, messages and grouping are reconstructed.** They approximate the order in
  which the modules were developed; they are not the original timestamps.
- **The repository was published on GitHub in October 2026.** The pre-reconstruction
  history consisted of 10 commits, all dated 2026-10-14, plus the annotated tag `v1.0.0`
  (2026-10-14 16:20 +0100), which this history keeps on the `release: prepare v1.0.0` commit.
- **Known anachronisms in the reconstructed timeline:**
  - `tests/unit/identifiers_test.cpp`, `tests/unit/state_machine_test.cpp`,
    `tests/unit/time_test.cpp` and `tests/cli/cli_test.cpp` use `2026-10-14` / `2026-10-15`
    as example fixture dates although they are committed in the Jul-Aug 2026 testing phase.
  - `LICENSE` is committed in December 2025 with the copyright year 2025 and updated to 2026
    (the publication year) in the October 2026 community-files commit.
  - The ADRs are dated `Accepted (2026-10-14)`, the date they were written up for publication,
    not the dates the decisions were taken during development. `docs/BENCHMARKS.md` and the
    README benchmark table report a run made on 2026-10-14; `docs/OPERATIONS.md`,
    `docs/SHELL_AUTOMATION.md` and `docs/EOD_BATCH.md` use October 2026 example dates. These
    files are committed on 2026-10-13/14.
  - Source files were added at their final content, so some intermediate trees do not build:
    for example `transaction_service.cpp` (Jan 2026) already includes the idempotency gate,
    lock manager and logger that land in the following phase, and the CLI (May 2026) already
    includes the end-of-day service and signal handler that land in June 2026.
  - `cmake/CompilerWarnings.cmake`, committed first, already contains the Clang-only
    `-Wnull-dereference` rule and the macOS `-fexperimental-library` flag that the published
    history introduced on 2026-10-14.
  - GoogleTest `v1.17.0` (released 2025) is referenced from the first test commit; the Ubuntu
    `clang-format`/`clang-tidy` 18.1.8 pins and `github/codeql-action@v4` in the intermediate
    workflows predate the commits that use them.

## Archive of the published history

The pre-reconstruction history (10 commits dated 2026-10-13/14, ending at `1aa286e`, and the
original `v1.0.0` tag) is preserved in an archive branch/tag alongside this reconstructed
history so the published state remains verifiable.
