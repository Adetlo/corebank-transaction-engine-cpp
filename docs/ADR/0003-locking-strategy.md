# ADR-0003: Fixed-order locking: gate, striped mutexes, database transaction

**Status:** Accepted (2026-10-14)

## Context

Several worker threads post transactions against the same accounts at once. A
transfer reads two balances, checks funds, writes two postings and two balance
updates. Without coordination two threads could both read a balance of 100, both
decide a debit of 80 is allowed, and leave the account at negative 60. Two transfers
in opposite directions (A to B and B to A) could also deadlock if each thread locked
"its" source account first.

SQLite serialises writers, but only once a write transaction has started. A thread
that reads balances in a deferred transaction and then upgrades to a write still
races with another thread doing the same.

## Decision

Acquire resources in one fixed order, never varied, documented at the call site in
`TransactionService::execute` (`src/application/transaction_service.cpp`):

```cpp
// Lock order (never varied): idempotency gate -> account stripes -> database write txn.
```

1. **Idempotency gate.** `IdempotencyGate::acquire(key)` blocks while another thread
   holds the same key, so in-flight duplicates serialise before touching any account.
2. **Account stripes.** `AccountLockManager` owns 64 `std::shared_mutex` stripes. The
   stripe of an account is `std::hash<std::string>(id) % 64`. `lock_exclusive` maps
   every involved account to its stripe index, sorts the indices, removes duplicates,
   and locks them in ascending order:

   ```cpp
   std::sort(indices.begin(), indices.end());
   indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
   for (const auto index : indices) {
     locks.emplace_back(*stripes_[index]);
   }
   ```

   Read-only history queries take `lock_shared` on one stripe.
3. **Database transaction.** Only then does the service open a read-write unit of
   work. `SqliteUnitOfWork::begin` first takes the repository's in-process writer
   mutex (`SqliteRepository::writer_mutex()`), so concurrent writers block on a
   mutex rather than retrying in SQLite's busy handler, and then runs
   `BEGIN IMMEDIATE` (`src/infrastructure/sqlite/sqlite_repository.cpp`), which takes
   SQLite's write lock up front and waits up to `busy_timeout_ms` if another
   process holds it. Read-only units of work use `BEGIN DEFERRED` and never take the
   writer mutex.

As a second guard that does not depend on in-process state, `accounts.version` is an
optimistic concurrency column. `update_account_balance` runs
`UPDATE ... WHERE account_id = ? AND version = ?` and returns `PERSISTENCE_ERROR` when
no row changed, which aborts and rolls back the unit of work.

### Deadlock-freedom argument

Every thread acquires locks in the same global order: gate (one key, never nested),
then stripes in ascending index, then the writer mutex, then the single database
write lock. A wait-for
cycle would require some thread to hold a higher-ordered resource while waiting for a
lower-ordered one, which the code never does. De-duplication prevents a thread from
locking the same stripe twice when two accounts hash to the same stripe.
`tests/concurrency/deadlock_test.cpp` drives opposite-direction pairs under a deadline;
`tests/concurrency/concurrent_transfers_test.cpp` checks conservation of money and
non-negative balances; `tests/unit/lock_manager_test.cpp` checks the ordering and
de-duplication directly.

## Consequences

Positive:

- Transfers on disjoint stripes run in parallel up to the database's write lock;
  transfers on the same accounts serialise without spinning.
- No deadlock is possible by construction, and the argument fits in a paragraph.
- The version column protects correctness even if a second process bypasses the
  in-memory stripes.

Negative:

- Striping causes false contention: unrelated accounts that share a stripe wait on
  each other. With 64 stripes this is acceptable for an educational engine.
- The gate and stripes are per process; a multi-process deployment relies only on
  the database (see [LIMITATIONS.md](../LIMITATIONS.md)).
- Holding stripes across the database transaction means a slow commit delays other
  work on those stripes. The alternative (release before commit) would reopen the
  read-check-write race.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| One global mutex | Trivially correct but removes all parallelism; the worker pool would be pointless. |
| One mutex per account in a map | Unbounded growth and a lifecycle problem: when can an entry be removed safely while another thread may be about to look it up? |
| Rely on SQLite alone | Works for SQLite's single writer, but the design would not transfer to a row-locking database, and without `BEGIN IMMEDIATE` the check-then-write would still race inside one process. |
| Lock-free balance updates | Not justified: the critical section includes durable writes, and correctness would depend on retry loops that are harder to reason about than ordered locks. |

## References

- `include/corebank/application/account_lock_manager.hpp`, `src/application/account_lock_manager.cpp`
- `include/corebank/application/idempotency_gate.hpp`, `src/application/idempotency_gate.cpp`
- `src/application/transaction_service.cpp` (`execute`, `apply_legs`)
- `src/infrastructure/sqlite/sqlite_repository.cpp` (`SqliteUnitOfWork::begin`, `update_account_balance`)
- `tests/concurrency/deadlock_test.cpp`, `tests/concurrency/concurrent_transfers_test.cpp`, `tests/unit/lock_manager_test.cpp`
- [CONCURRENCY.md](../CONCURRENCY.md), [PERSISTENCE.md](../PERSISTENCE.md)
- [ADR-0004](0004-sqlite.md), [ADR-0005](0005-idempotency-design.md)
