# ADR-0006: Restartable end-of-day batch with per-stage checkpoints

**Status:** Accepted (2026-10-14)

## Context

The end-of-day batch (`src/batch/eod_service.cpp`) runs eight stages in order:
`PRECHECK`, `TRANSACTION_CUTOFF`, `VALIDATE_PENDING`, `BALANCE_CHECK`,
`RECONCILIATION`, `DAILY_SUMMARY`, `ARCHIVE`, `COMPLETE`. Any stage can fail: the
integrity check can report corruption, reconciliation can find a mismatch, the batch
directory can be unwritable, or the process can be killed. An operator who fixes the
cause and runs the batch again must not get duplicated summaries, a second archive
with different contents, or a run that silently skips the stage that failed.

## Decision

Persist batch progress in two tables (`src/infrastructure/sqlite/schema.cpp`):
`batch_runs` (one row per run with `status`, `attempt`, `current_stage`,
`failure_reason`) and `batch_stages` (one row per stage per run with `status`,
`attempts`, timestamps and `detail`).

`EodService::run_stage` uses three separate database transactions per stage:

1. Mark the stage `RUNNING` and append `BATCH_STAGE_STARTED`, then commit. A crash
   after this point leaves evidence that the stage was in progress.
2. Execute the stage and, in the same unit of work, mark it `COMPLETED`, append
   `BATCH_STAGE_COMPLETED`, and commit. The stage's writes and its checkpoint are
   atomic.
3. If the stage fails, roll back its writes, then in a fresh transaction mark the
   stage and the run `FAILED`, append `BATCH_STAGE_FAILED` and `BATCH_FAILED`, and
   commit.

`EodService::run` first returns `already_completed = true` without doing anything if
the latest run for the date is `COMPLETED`. Otherwise `open_or_resume` either creates
a new run with every stage `PENDING` (audit `BATCH_STARTED`) or resumes the existing
`FAILED` or `RUNNING` run: it increments `attempt`, resets every `RUNNING` or
`FAILED` stage to `PENDING`, keeps `COMPLETED` stages, and appends `BATCH_RESUMED`.
The main loop then skips stages already `COMPLETED`:

```cpp
if (record != nullptr && record->status == StageStatus::Completed) {
  Logger::instance().info("eod.stage_skipped", ...);
  continue;
}
```

Stages are idempotent so that re-running one after a rollback is safe:
`DAILY_SUMMARY` uses `upsert_daily_summary` keyed on `(business_date, currency)`,
`ARCHIVE` truncates and rewrites `transactions.csv`, `BALANCE_CHECK` and
`RECONCILIATION` only read, and `VALIDATE_PENDING` only touches rows that are still
pending.

For recovery testing, `COREBANK_FAIL_STAGE=<STAGE>` makes `execute_stage` fail that
stage deliberately (`EodOptions::fail_stage`), which `tests/batch/eod_test.cpp` uses
to prove that every stage is recoverable and that a restart skips completed ones.

## Consequences

Positive:

- Partial progress survives failures: an operator re-runs `scripts/run_eod.sh` and
  only the failed stage and those after it execute.
- The audit trail shows every attempt: `BATCH_STARTED`, `BATCH_RESUMED`,
  `BATCH_STAGE_STARTED`, `BATCH_STAGE_COMPLETED`, `BATCH_STAGE_FAILED`,
  `BATCH_FAILED`, `BATCH_COMPLETED`.
- A completed date cannot be run twice, which protects the archive and summaries from
  accidental regeneration.
- `attempt` and per-stage `attempts` counters make flapping visible in
  `corebank eod status`.

Negative:

- Three transactions per stage cost more than one, and the `RUNNING` checkpoint is
  visible to concurrent readers before the work is done.
- Because a `RUNNING` run is resumed rather than rejected, a batch that is genuinely
  still executing in another process would be resumed concurrently. `PRECHECK`
  guards the common case by failing when a different run is `RUNNING`, but the first
  stage of the resumed run can still overlap with the tail of the original.
- Stage idempotency is a property each stage must maintain by hand; a new stage that
  appends instead of upserting would break the model.

## Alternatives considered

| Alternative | Why rejected |
|---|---|
| One transaction for the whole batch | No partial progress, and a long write lock would block the engine for the duration of reconciliation and archiving. |
| Re-run everything on restart | Without idempotent stages this would duplicate summaries and archives; with them it would still redo expensive read stages and hide which stage failed. |
| External scheduler holding the state | Out of scope: the engine should be inspectable from its own database and CLI without a workflow system. |
| Delete the failed run and start fresh | Loses the attempt history and the audit evidence of what failed. |

## References

- `include/corebank/batch/eod_service.hpp`, `src/batch/eod_service.cpp`
- `include/corebank/domain/batch.hpp`
- `src/infrastructure/sqlite/schema.cpp` (`batch_runs`, `batch_stages`, `daily_summaries`)
- `src/infrastructure/sqlite/sqlite_repository.cpp` (`upsert_stage`, `upsert_daily_summary`)
- `scripts/run_eod.sh`
- `tests/batch/eod_test.cpp`
- [EOD_BATCH.md](../EOD_BATCH.md), [PERSISTENCE.md](../PERSISTENCE.md)
- [ADR-0004](0004-sqlite.md)
