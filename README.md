# CoreBank Transaction Engine C++

Educational C++20 core-banking transaction engine, built from first principles with
CMake and SQLite. Early development: the domain types are being laid down first; the
transaction service, persistence, concurrency, messaging and batch layers will follow.

> **Disclaimer.** This is an independent educational implementation using synthetic data
> and generic banking concepts. It is not derived from or representative of any employer
> production system.

## Goals

* Exact money with integer minor units and checked arithmetic, no floating point.
* An explicit error model (`Result<T>` with stable error codes) instead of exceptions.
* Atomic, idempotent, audited double-entry postings.
* A tested transaction state machine and a reconciled end-of-day close.
* Linux-first operations: shell automation, structured logs, graceful shutdown.

## Layout

```
include/corebank/   public headers by layer (domain first)
src/                implementation, same layout
cmake/              shared compiler warning set
```

## Build

Requirements: a C++20 compiler (GCC 13+ or Clang 16+), CMake 3.22+, SQLite 3 headers.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Warnings are errors (`-Wall -Wextra -Wpedantic -Wconversion ...`); see
`cmake/CompilerWarnings.cmake`. Formatting follows `.clang-format`.

## License

MIT, see [LICENSE](LICENSE).
