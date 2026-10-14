# ADR-0005: Mandatory idempotency key with verbatim request signature

**Status:** Accepted (2026-10-14)

## Context

Clients retry. A TCP connection can drop after the server has committed but before
the client has read the reply, and `MessageHandler::await` returns `TIMEOUT` while
the queued job still runs. Without protection a retry would post the same transfer
twice. The engine therefore needs a way to recognise a repeated request, return the
original outcome, and detect a different request that reuses the same key.

## Decision

- Every financial request (`CreditRequest`, `DebitRequest`, `TransferRequest`,
  `ReverseRequest` in `include/corebank/application/requests.hpp`) carries an
  `IdempotencyKey`. The protocol requires `key=`; the CLI accepts `--key` and
  generates one through `IdGenerator::next_idempotency_key` when it is omitted
  (`src/cli/app.cpp`), so every stored transaction has a key.
- A canonical request signature is built in `src/application/transaction_service.cpp`
  and stored verbatim:

  ```cpp
  // TYPE|source|destination|amount ccy|reference
  signature_of("TRANSFER", request.source.str(), request.destination.str(),
               request.amount, request.reference.str());
  ```

  Credits leave the source empty, debits leave the destination empty, and reversals
  use `REVERSAL|<original id>||<reference>`. The signature is compared with
  `operator==` on the stored string. No hash is involved, so there is no collision
  case to reason about.
- `IdempotencyGate` serialises in-flight requests with the same key inside one
  process (first lock in the order of [ADR-0003](0003-locking-strategy.md)). After
  acquiring the gate, `replay_if_stored` looks up `idempotency_records`:
  same signature returns the stored transaction with `replayed = true`; a different
  signature returns `IDEMPOTENCY_CONFLICT`.
- The `idempotency_records` primary key is the backstop across processes. The record
  is inserted inside the same database transaction as the postings. If the insert
  fails with `DUPLICATE_REQUEST`, the unit of work is rolled back and the stored
  outcome is replayed instead.
- Rejected outcomes are stored and replayed too: `record_rejection` writes the
  `REJECTED` transaction, the audit event and the idempotency record in one commit.
- A commit failure does not consume the key, because the record and the transaction
  are written atomically; the next attempt with the same key runs again
  (`tests/integration/rollback_test.cpp`).
- Keys never expire; `idempotency_records` is append-only by trigger.

## Consequences

Positive:

- A retry with the same key and the same request is safe at any time and from any
  process sharing the database.
- A retry with a changed amount or counterparty is caught as a conflict instead of
  being silently accepted as a new transaction.
- Replaying rejections means a client that retries an `INSUFFICIENT_FUNDS` outcome
  gets the same answer rather than a second attempt that might now succeed under a
  different balance.
- Storing the signature in clear text makes conflicts debuggable from the database.

Negative:

- Storage grows with every financial request and nothing prunes it
  ([LIMITATIONS.md](../LIMITATIONS.md)).
- The reference is part of the signature, so the same transfer re-sent with a
  different narrative is a conflict. That is intentional: the key identifies one
  request, not one business intent.
- The gate is per process. Two processes with the same key proceed in parallel to
  the database and one of them loses on the primary key, which costs a rolled-back
  transaction.
- Clients must manage keys. A client that forgets to reuse the key on retry gets a
  duplicate posting; the engine cannot detect that case.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| Optional key | Requests without a key could never be retried safely; mandatory keys with CLI generation cost nothing. |
| Hash of the request instead of the verbatim signature | Saves bytes but introduces a collision question and hides the original request from operators. |
| Derive the key from the request content | Two legitimately identical transfers (same amount, same accounts, same reference) would be merged into one. |
| Expiring keys after a window | Simplifies storage but reopens the duplicate problem for late retries; left for a production system to decide with its retention policy. |
| Database constraint only, no in-process gate | Correct but wasteful: concurrent duplicates would each run the full posting path and all but one would roll back. |

## References

- `include/corebank/application/requests.hpp`
- `src/application/transaction_service.cpp` (`request_signature`, `replay_if_stored`, `execute`, `record_rejection`)
- `include/corebank/application/idempotency_gate.hpp`, `src/application/idempotency_gate.cpp`
- `src/infrastructure/sqlite/schema.cpp` (`idempotency_records`, triggers)
- `src/cli/app.cpp` (`idempotency_key`)
- `tests/integration/idempotency_test.cpp`, `tests/concurrency/concurrent_idempotency_test.cpp`
- [IDEMPOTENCY.md](../IDEMPOTENCY.md), [CONCURRENCY.md](../CONCURRENCY.md)
- [ADR-0003](0003-locking-strategy.md)
