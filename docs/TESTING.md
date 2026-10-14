# Testing

This document describes the test suite, how to run it, the sanitizer and static-analysis
configuration with the rationale for every disabled check, and the continuous-integration
matrix. The suite runs against synthetic data in temporary SQLite files; no external
services are involved.

Related documents: [ARCHITECTURE.md](ARCHITECTURE.md), [CONCURRENCY.md](CONCURRENCY.md),
[IDEMPOTENCY.md](IDEMPOTENCY.md), [EOD_BATCH.md](EOD_BATCH.md),
[SHELL_AUTOMATION.md](SHELL_AUTOMATION.md),
[ADR-0007 error handling](ADR/0007-error-handling.md).

## Test categories

`tests/CMakeLists.txt` builds 31 test executables with GoogleTest v1.17.0 (fetched by
`FetchContent`, GMock disabled) and labels each one through `corebank_add_test`. The counts
below are `TEST`/`TEST_F` macros per directory, 160 in total.

| Label | Directory | Executables | Cases | Scope |
| --- | --- | --- | --- | --- |
| `unit` | `tests/unit` | 14 | 69 | Money, currency, identifiers, `Result`, time, state machine, account, config, logger, JSON writer, protocol, bounded queue, lock manager, argument parsing |
| `integration` | `tests/integration` | 7 | 44 | Service flows on a real SQLite file: transaction service, idempotency, reversal, audit, reconciliation, persistence, rollback |
| `concurrency` | `tests/concurrency` | 4 | 12 | Concurrent transfers, concurrent idempotency, processor, deadlock |
| `batch` | `tests/batch` | 1 | 7 | End-of-day stages, idempotent re-run, failure injection, resume |
| `messaging` | `tests/messaging` | 3 | 19 | Message handler, TCP server, signal-driven shutdown |
| `property` | `tests/property` | 1 | 3 | Deterministic randomised invariants |
| `cli` | `tests/cli` | 1 | 6 | Exit codes and JSON output of the real binary |

Every executable has a 300 second ctest timeout. Fixtures in `tests/support/test_support.hpp`
provide `TempDir` (a unique directory under the system temporary path, removed on
destruction), `EngineFixture` (a migrated `SqliteRepository` with a pool of 8 connections,
a `SystemClock`, a seeded `IdGenerator` and a `TransactionService`) and `FakeClock`. Each
fixture instance uses its own database file, so tests are isolated and can run in parallel.

## What the important tests prove

| Property | Test |
| --- | --- |
| Money arithmetic is exact, overflow-checked and currency-safe | `tests/unit/money_test.cpp`: `AddsAndSubtractsExactly`, `DetectsArithmeticOverflow`, `RefusesCrossCurrencyArithmetic` |
| Invalid amounts are rejected before persistence | `money_test.cpp` `RejectsInvalidText`; `transaction_service_test.cpp` `ValidationErrorsAreNotPersisted`; `cli_test.cpp` (`--amount abc` exits 1) |
| Duplicate transaction with the same key replays | `idempotency_test.cpp` `SameKeySameRequestReplaysWithoutReposting`, `RejectedOutcomesAreReplayedToo` |
| Same key with a different request is a conflict | `idempotency_test.cpp` `SameKeyDifferentRequestIsAConflict` |
| Concurrent duplicates commit exactly once | `concurrent_idempotency_test.cpp`: 12 threads behind a `std::barrier`, one transaction id, 11 replays, gate drained |
| Concurrent transfers never corrupt balances | `concurrent_transfers_test.cpp` `SameAccountContentionNeverCorruptsBalances`, `RandomisedMeshKeepsTotalAndNonNegativeBalances` |
| Opposite-direction transfers conserve money | `concurrent_transfers_test.cpp` `OppositeDirectionTransfersConserveMoney` |
| Deadlock safety | `deadlock_test.cpp` `OppositeDirectionPairsCompleteWithinDeadline`: 8 threads on a ring of 4 accounts, 800 transfers, 120 second deadline, total unchanged; `ReadersDoNotStarveWriters` |
| Rollback on database failure | `rollback_test.cpp`: injected `FailCommit` and `FailInsertPosting` leave balances, postings, audit and idempotency key untouched; a retry succeeds and is not a replay |
| Reversal restores balances and links both sides | `reversal_test.cpp` `SingleReversalRestoresBalancesAndLinksBothSides`, `DuplicateReversalIsRejected`, `ReversalOfDebitRecredits` |
| Reconciliation detects tampering | `reconciliation_test.cpp` `DetectsTamperedBalance`, `DetectsOverdraftBreach`, `DetectsOrphanPostingsAndMissingLegs` |
| EOD restartability and failure recovery | `eod_test.cpp` `FailureInEveryStageIsRecoverable`, `InjectedFailureIsRecordedAndRestartSkipsCompletedStages`, `SecondRunForSameDateIsIdempotent`, `StalePendingTransactionsAreRejectedAtCutoff` |
| Malformed TCP input gets a structured error | `tcp_server_test.cpp` `MalformedAndOversizedInputGetStructuredErrors` (`MALFORMED_MESSAGE`, `MISSING_FIELD`, `INVALID_CURRENCY`, `PAYLOAD_TOO_LARGE` then socket closed) |
| Graceful shutdown finishes queued work | `signal_test.cpp` `SignalDrivenShutdownFinishesQueuedWork`; `processor_test.cpp` `GracefulShutdownDrainsQueuedWork` |
| Append-only storage | `audit_test.cpp` `AuditEventsCannotBeUpdatedOrDeleted`, `PostingsAndIdempotencyRecordsAreImmutableToo`, `TerminalTransactionsCannotChangeStatusAtStorageLevel` |

### CLI tests drive the real binary

`tests/cli/cli_test.cpp` receives the path of the built `corebank` executable through the
`COREBANK_CLI_PATH` compile definition and runs it with `popen`. It asserts exit codes 0, 1,
2, 3 and 4 on real invocations: usage errors, a full account and transfer flow with replay
and conflict, reversal, `eod run` with `COREBANK_FAIL_STAGE=ARCHIVE` followed by a resume,
`reconcile` after tampering with the database through the `sqlite3` tool (skipped when the
tool is absent), and `health` against an unreachable port.

## Running the tests

```bash
scripts/build.sh                     # Release build with tests
scripts/test.sh                      # whole suite
scripts/test.sh concurrency          # one label
scripts/test.sh unit -R Money        # label plus extra ctest arguments
ctest --test-dir build -L '^(integration|batch)$' --output-on-failure
```

`scripts/test.sh` wraps `ctest --test-dir "$BUILD_DIR" --output-on-failure -j
"${CTEST_PARALLEL:-4}"` and anchors the label as `-L '^label$'`. A single executable can be
run directly, for example `build/tests/eod_test --gtest_filter='*Recoverable*'` (the exact
path depends on the generator).

## Property-style tests

`tests/property/invariants_test.cpp` uses seeded `std::mt19937` loops instead of a
property-testing library such as RapidCheck. The reasons:

* Dependency burden: the project has one external dependency (SQLite) plus GoogleTest for
  tests. A property framework would add a second fetched library, more compile time and
  another API for contributors to learn.
* Reproducibility: a fixed seed makes every run identical, failure messages name the seed
  and the step, and a failing case can be replayed without a shrinking phase.
* The invariants are cheap to state as loops over the real service and database, so the
  main benefit of generators (shrinking arbitrary structures) is not needed.

| Test | Seeds and steps | Invariant |
| --- | --- | --- |
| `MoneyIsConservedAcrossRandomInternalTransfers` | Seeds 1, 7, 42; 5 accounts; 300 transfers each | Sum of balances is unchanged after every transfer; no account without overdraft goes negative; reconciliation passes |
| `ReversalRestoresOriginalNetState` | Seed 2026; 4 accounts; 100 transfer and reverse pairs | Every balance returns to its snapshot after the reversal |
| `PostingChainAlwaysMatchesBalance` | Seed 99; 3 accounts; 200 random credits, debits and transfers | Replaying each account's postings reproduces `balance_after` at every step and equals the stored balance; balances stay non-negative |

## Sanitizers

`cmake/Sanitizers.cmake` adds `-fsanitize=address`, `-fsanitize=undefined
-fno-sanitize-recover=all` or `-fsanitize=thread` to every target, and refuses to combine
ThreadSanitizer with the other two. `scripts/build.sh --asan --ubsan` and `--tsan` set the
same options.

| CI job | Compiler | Options | Scope | Expectation |
| --- | --- | --- | --- | --- |
| `sanitizers` | clang | `ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1`, `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` | Whole suite, `-j 2` | No findings; any report fails the job |
| `thread-sanitizer` | clang | `TSAN_OPTIONS=halt_on_error=1:second_deadlock_stack=1` | Labels `unit`, `concurrency`, `messaging`, `property`, `integration` | No data races or lock-order reports |

The ThreadSanitizer job is pinned to clang because GCC's TSan runtime failed to map its
shadow memory under the address-space randomisation settings of recent CI kernels, which
aborted the run before any test executed. Clang's runtime handles those layouts, so the
same tests run there. The excluded labels (`batch`, `cli`) are single-threaded or spawn the
binary as a separate process and therefore gain nothing from TSan.

## Static analysis

### clang-tidy (`.clang-tidy`)

CI installs clang-tidy 18.1.8 with pipx, configures with `COREBANK_BUILD_TESTS=OFF` to
obtain `compile_commands.json`, and runs on every file under `src/` with
`WarningsAsErrors: '*'`. The enabled groups are `bugprone-*`, `clang-analyzer-*`,
`concurrency-*`, selected `cppcoreguidelines-*` checks, `misc-*`, `modernize-*`,
`performance-*`, `portability-*` and `readability-*`.

| Disabled check | Rationale |
| --- | --- |
| `bugprone-unchecked-optional-access` | False positives: the check does not see that `Result::value()` and `std::optional` accesses are guarded by the preceding `has_value()` or `if (!r)` test through the `Result` wrapper |
| `bugprone-easily-swappable-parameters` | Domain functions legitimately take several adjacent identifiers or amounts; strong types (`AccountId`, `TransactionId`, `Money`) already prevent the mix-ups the check targets |
| `bugprone-exception-escape` | Domain code uses `Result<T>`, not exceptions (ADR-0007); the check reports standard-library allocation paths only |
| `performance-enum-size` | Scoped enums are intentionally `int`-sized; the storage saving is irrelevant and the narrower type would spread casts |
| `performance-avoid-endl` | Readability preference in CLI output |
| `misc-include-cleaner` | Transitive includes through the project's own headers are intentional; enforcing direct includes everywhere adds noise without a safety gain |
| `misc-non-private-member-variables-in-classes` | Aggregates such as `Transaction`, `BatchRun` and `Config` are plain structs by design |
| `misc-no-recursion` | Not applicable to the codebase's style |
| `modernize-use-trailing-return-type`, `modernize-use-nodiscard` | Style choices: leading return types; `[[nodiscard]]` is applied by hand where it matters |
| `readability-magic-numbers` | Limits such as port ranges, scale factors and test amounts are clearer inline than as named constants |
| `readability-identifier-length` | Short names (`id`, `tx`, `ec`) are idiomatic in the domain code |
| `readability-function-cognitive-complexity` | Stage pipelines and command dispatchers are long but linear |
| `readability-named-parameter`, `readability-convert-member-functions-to-static`, `readability-else-after-return`, `readability-avoid-nested-conditional-operator` | Style preferences that conflict with the Google-style formatting in `.clang-format` |

`CheckOptions` allow pointer conditions in `readability-implicit-bool-conversion`, accept a
sole default destructor in `cppcoreguidelines-special-member-functions`, disable value
analysis in `misc-const-correctness`, and allow `std::shared_ptr` and `std::function` by
value in `performance-unnecessary-value-param`.

### cppcheck (`.cppcheck-suppressions`)

CI runs `cppcheck --enable=warning,style,performance,portability --std=c++20
--inline-suppr --suppressions-list=.cppcheck-suppressions --error-exitcode=1 -I include src
include`.

| Suppression | Rationale |
| --- | --- |
| `missingIncludeSystem` | cppcheck cannot see the system headers; the message is noise, not a finding |
| `noExplicitConstructor` | `Result<T>` converts implicitly from `T` and from `Error` on purpose, so `return value;` and `return error;` work (ADR-0007) |
| `shadowFunction` | Google-style accessors share a name with the parameter or member they expose (`level(LogLevel level)`, `run(BatchRun& run)`); idiomatic, not shadowing |
| `functionStatic` | Interface implementations keep non-static members for symmetry with the base class |
| `useStlAlgorithm` | Readability preference: short explicit loops over `std::transform` with lambdas |

### Other gates

* `clang-format` 18.1.8 with `--dry-run --Werror` over `src`, `include` and `tests`.
* ShellCheck `-x -S style` over all scripts (see [SHELL_AUTOMATION.md](SHELL_AUTOMATION.md)).
* CodeQL (`.github/workflows/codeql.yml`) for C++ on pushes, pull requests and weekly.
* Compiler warnings from `cmake/CompilerWarnings.cmake` (`-Wall -Wextra -Wpedantic
  -Wshadow -Wconversion -Wsign-conversion` and others) are errors unless
  `COREBANK_WARNINGS_AS_ERRORS=OFF`.

## Coverage

`-DCOREBANK_ENABLE_COVERAGE=ON` (or `scripts/build.sh --coverage`) adds `--coverage -O0
-g` to every target for gcov/lcov instrumentation. Coverage is not collected or gated in
CI; the option exists for local inspection:

```bash
BUILD_DIR=build-cov scripts/build.sh --debug --coverage
BUILD_DIR=build-cov scripts/test.sh
lcov --capture --directory build-cov --output-file coverage.info
```

## CI matrix

`.github/workflows/ci.yml` runs on pushes to `main`, tags `v*` and pull requests, with
`contents: read` permissions and cancellation of superseded runs.

| Job | Toolchain | Steps |
| --- | --- | --- |
| `build-test` (gcc, Release) | gcc, Ninja | Build; ctest `unit`; ctest `integration`, `batch`, `messaging`, `property`, `cli`; ctest `concurrency`; `scripts/smoke_test.sh`; `corebank-bench --transactions 500 --threads 2` (Release only, local synthetic numbers) |
| `build-test` (clang, Debug) | clang, Ninja | Same without the benchmark step |
| `format` | clang-format 18.1.8 (pipx) | `--dry-run --Werror` |
| `clang-tidy` | clang-tidy 18.1.8 (pipx), clang | Warnings as errors on `src/**/*.cpp` |
| `cppcheck` | apt cppcheck | Suppressions file, `--error-exitcode=1` |
| `shellcheck` | apt ShellCheck | `-x -S style` |
| `sanitizers` | clang | ASan + UBSan, whole suite, no findings allowed |
| `thread-sanitizer` | clang | TSan over `unit`, `concurrency`, `messaging`, `property`, `integration` |
| `docker` | buildx | Build the image, run it, `health`, create an account, `CREDIT` and `BALANCE` over TCP, verify the container user is `corebank:corebank`, `docker stop -t 30`, assert `engine.stopped` in the logs |

`.github/dependabot.yml` updates GitHub Actions and the Docker base image monthly.
`CONTRIBUTING.md` lists the same gates for local use before a pull request.
