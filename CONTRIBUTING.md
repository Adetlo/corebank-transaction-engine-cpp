# Contributing

Thanks for your interest. This is an educational project; contributions that sharpen
the financial controls, the concurrency model, the operational tooling, the tests or
the documentation are very welcome.

## Setup

```bash
git clone https://github.com/Adetlo/corebank-transaction-engine-cpp.git
cd corebank-transaction-engine-cpp
scripts/build.sh            # Release build into ./build (CMake + Ninja if available)
scripts/test.sh             # whole suite through ctest
```

Requirements: a C++20 compiler (GCC 13+ or Clang 16+), CMake 3.22+, SQLite 3
development headers, bash. GoogleTest is fetched by CMake at configure time. On
Debian/Ubuntu: `apt-get install build-essential cmake ninja-build libsqlite3-dev`.
On macOS the Command Line Tools are enough (`-fexperimental-library` is added
automatically for `std::jthread`).

## Quality gates

Every pull request must pass the same gates CI runs:

```bash
clang-format --dry-run --Werror $(find src include tests -name '*.cpp' -o -name '*.hpp')
clang-tidy -p build src/**/*.cpp                     # .clang-tidy: warnings are errors
cppcheck --enable=warning,style,performance,portability --std=c++20 --language=c++ \
  --inline-suppr --suppressions-list=.cppcheck-suppressions --error-exitcode=1 -I include src include
shellcheck -x -S style scripts/*.sh scripts/lib/common.sh
scripts/build.sh --debug --asan --ubsan && scripts/test.sh   # no sanitizer findings
scripts/build.sh --debug --tsan  && scripts/test.sh concurrency
scripts/smoke_test.sh
```

Pinned tool versions: clang-format and clang-tidy 18.1.x (CI installs them with pipx).

## Test layout

| Directory | What it proves |
| --- | --- |
| `tests/unit` | Money, currency, strong types, state machine, config, protocol, queue, locks, args |
| `tests/integration` | Service flows on a real SQLite file: idempotency, reversal, audit, reconciliation, rollback |
| `tests/concurrency` | Contention, opposite-direction transfers, duplicate keys in flight, processor, deadlock deadline |
| `tests/batch` | End-of-day stages, idempotent re-run, failure injection and resume |
| `tests/messaging` | Message handler, TCP server limits and errors, signal-driven shutdown |
| `tests/property` | Deterministic randomised invariants (conservation, non-negative balances, reversal) |
| `tests/cli` | Exit codes and JSON output of the real binary |

Add a regression test with every bug fix; add tests in the matching category for
every new control.

## Conventions

* Domain outcomes are `Result<T>` values, never exceptions (see ADR-0007).
* Money is `int64_t` minor units; floating point never touches an amount (ADR-0002).
* Lock order is fixed: idempotency gate, account stripes, database write transaction
  (ADR-0003). Do not add a lock without updating `docs/CONCURRENCY.md`.
* Financial history is append-only. Corrections are new, linked transactions.
* Synthetic data only: no real identifiers, hostnames, credentials or institution-specific structures.
* Commit messages follow the Conventional Commits style used in the history.

## Documentation

Behaviour changes update the matching document in `docs/`; design decisions get an ADR
in `docs/ADR/` (copy the format of the existing ones).
