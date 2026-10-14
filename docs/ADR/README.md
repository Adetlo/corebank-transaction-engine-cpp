# Architecture Decision Records

Each record captures one decision that shaped the CoreBank Transaction Engine C++,
the context at the time, the alternatives that were considered, and the consequences
the code base lives with. Records are numbered in the order the decisions were made
and are never edited after acceptance; a change of direction gets a new record that
supersedes the old one.

All records share the same layout: Status, Context, Decision, Consequences,
Alternatives considered, References.

| ADR | Title | Status | Summary |
|---|---|---|---|
| [0001](0001-cpp20.md) | C++20 as the language baseline | Accepted | `std::jthread`, `std::stop_token`, `<=>`, concepts, designated initialisers, `std::span`; no C++23 |
| [0002](0002-money-representation.md) | Money as `int64_t` minor units plus a currency code | Accepted | Exact arithmetic with overflow checks, no floating point, comparisons only within one currency |
| [0003](0003-locking-strategy.md) | Fixed-order locking: gate, striped mutexes, database transaction | Accepted | Deadlock-free by ordering; optimistic version column as a second guard |
| [0004](0004-sqlite.md) | SQLite in WAL mode behind a repository interface | Accepted | Zero-dependency reproducibility; single-writer limitation accepted |
| [0005](0005-idempotency-design.md) | Mandatory idempotency key with verbatim request signature | Accepted | In-process gate plus primary-key backstop; rejections replayed; no expiry |
| [0006](0006-eod-restart-model.md) | Restartable end-of-day batch with per-stage checkpoints | Accepted | Stage status committed before and after work; completed stages skipped on resume |
| [0007](0007-error-handling.md) | `Result<T>` and error codes instead of exceptions | Accepted | Stable upper-case codes across logs, CLI exit codes and the wire protocol |

## Conventions

- **Status** is `Accepted (date)`. A superseding record changes the old status to
  `Superseded by ADR-XXXX`.
- **Consequences** lists both positive and negative outcomes.
- **References** cites file paths in the repository and related documents under
  `docs/`.
- Records describe this educational implementation only. They do not reference any
  real bank, product or proprietary system, and they do not make production
  performance claims.

## Related documents

- [ARCHITECTURE.md](../ARCHITECTURE.md)
- [CONCURRENCY.md](../CONCURRENCY.md)
- [IDEMPOTENCY.md](../IDEMPOTENCY.md)
- [PERSISTENCE.md](../PERSISTENCE.md)
- [EOD_BATCH.md](../EOD_BATCH.md)
- [TESTING.md](../TESTING.md)
- [SECURITY.md](../SECURITY.md)
- [LIMITATIONS.md](../LIMITATIONS.md)
