# ADR-0007: `Result<T>` and error codes instead of exceptions

**Status:** Accepted (2026-10-14)

## Context

A transaction engine has many expected failure outcomes: invalid input, unknown
account, insufficient funds, duplicate key, database busy. These are not exceptional;
they are the normal vocabulary of the domain and every caller must handle them. The
same outcome must be reported consistently in the log, in the CLI exit code and in
the wire protocol, so that a shell script and a TCP client can react to it without
parsing prose.

## Decision

Model every outcome as a value. `include/corebank/domain/result.hpp` defines
`Result<T>` as a `std::variant<T, Error>` with a `Result<void>` specialisation, both
marked `[[nodiscard]]`:

```cpp
template <typename T>
class [[nodiscard]] Result {
  std::variant<T, Error> storage_;
};
struct Error {
  ErrorCode code{ErrorCode::Internal};
  std::string message;
};
```

`ErrorCode` (`include/corebank/domain/error.hpp`) is an enum whose `to_string` in
`src/domain/error.cpp` returns stable upper-case names such as `INSUFFICIENT_FUNDS`,
`IDEMPOTENCY_CONFLICT`, `PERSISTENCE_ERROR`. Those names are the contract: they appear
in the `error` field of log lines, in `rejection_code` in the database, in
`ERR|code=...|message=...` on the wire (`src/messaging/protocol.cpp`), and they map to
CLI exit codes in `src/cli/output.cpp`:

| Exit code | Meaning | Example codes |
|---|---|---|
| 0 | ok | |
| 1 | usage or validation | `INVALID_AMOUNT`, `MALFORMED_MESSAGE` |
| 2 | business rejection | `INSUFFICIENT_FUNDS`, `IDEMPOTENCY_CONFLICT` |
| 3 | control failure | `BATCH_STAGE_FAILED`, `RECONCILIATION_FAILED` |
| 4 | system error | `PERSISTENCE_ERROR`, `QUEUE_FULL`, `TIMEOUT` |

Two kinds of failure are distinguished in the service layer
(`src/application/transaction_service.cpp`):

- **Business rejections** (`AccountNotFound` during posting, `AccountInactive`,
  `CurrencyMismatch`, `InsufficientFunds`, `Overflow`, a reversal whose original is
  no longer `POSTED`) are persisted as a `REJECTED` transaction with an audit event
  and an idempotency record, and returned as a successful `Result` holding a
  `TransactionReceipt` whose `transaction.status` is `REJECTED`.
- **Request validation errors** (non-positive amount, same-account transfer, invalid
  identifier) and **infrastructure errors** (`PersistenceError`, `QueueFull`,
  `ShuttingDown`) are returned as `Result` errors and nothing is persisted; the unit
  of work rolls back on destruction.

Exceptions are not used for any domain or infrastructure outcome. The code base has
no `throw`, `try` or `catch`. Truly exceptional conditions such as `std::bad_alloc`
are left to propagate and terminate the process, which is the intended behaviour for
a state the engine cannot reason about.

## Consequences

Positive:

- Every failure path is visible at the call site; `[[nodiscard]]` turns an ignored
  `Result` into a compiler warning, and `-Werror` into a build failure.
- One enum drives logs, exit codes and the protocol, so scripts in `scripts/` branch
  on exit codes 0 to 4 and clients branch on `code=`.
- Business rejections are durable and replayable, which is what
  [ADR-0005](0005-idempotency-design.md) requires.
- No hidden control flow across threads: a worker that fails hands the `Result` to a
  `std::promise`, never an exception.

Negative:

- The `if (auto x = ...; !x) return x.error();` pattern is repeated many times; the
  code is longer than exception-based equivalents.
- `Result<T>` offers only `and_then`, `map` and `value_or`; richer monadic chains
  would need more combinators.
- Adding an `ErrorCode` requires updating `to_string` and `exit_code_for`, which the
  compiler enforces through exhaustive switches without a `default`.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| Exceptions for domain outcomes | Expected outcomes would travel through invisible paths, cross thread boundaries awkwardly, and make the "rejected but persisted" case hard to express. |
| C-style integer codes with out parameters | Loses the value-or-error invariant and the `[[nodiscard]]` guarantee; easy to forget to check. |
| `std::expected` | Correct fit, but C++23 only; the project baseline is C++20 ([ADR-0001](0001-cpp20.md)). |
| `std::optional<T>` plus a global last-error | Not thread-safe and separates the error from the call that produced it. |

## References

- `include/corebank/domain/result.hpp`
- `include/corebank/domain/error.hpp`, `src/domain/error.cpp`
- `src/application/transaction_service.cpp` (`post`, `record_rejection`)
- `src/cli/output.cpp` (`exit_code_for`), `include/corebank/cli/output.hpp`
- `src/messaging/protocol.cpp` (`error_response`)
- `tests/unit/result_test.cpp`
- [ARCHITECTURE.md](../ARCHITECTURE.md), [TESTING.md](../TESTING.md)
- [ADR-0001](0001-cpp20.md), [ADR-0005](0005-idempotency-design.md)
