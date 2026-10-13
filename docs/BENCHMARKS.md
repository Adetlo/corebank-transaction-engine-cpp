# Benchmarks

**Local synthetic benchmark.** Every number on this page was produced by
`corebank-bench` on one developer laptop against a temporary SQLite file with synthetic
accounts. The numbers describe this code on that machine and nothing else. They are not a
statement about any production banking system, and they should not be compared with one.

## What is measured

`src/bench/main.cpp` builds a fresh database, creates `--accounts` funded accounts and runs
three phases:

| Run | What it does | Count unit |
| --- | --- | --- |
| `single_thread_transfers` | Calls `TransactionService::transfer` directly from one thread, one transfer after another. Latency is the service call. | transfers |
| `multi_thread_transfers` | Submits transfers to a `TransactionProcessor` (`--threads` workers, bounded queue) in a closed loop with at most `--window` requests in flight, like that many concurrent clients. Latency is submit to future resolution, so it includes queueing. | transfers |
| `eod_batch` | Runs the full end-of-day pipeline (`EodService::run`) over everything posted so far. | rows processed (transactions plus postings) |

Each transfer is a full engine round trip: idempotency check, account stripe locks, a
`BEGIN IMMEDIATE` transaction with four audit and journal writes, commit. Latency
percentiles are p50 and p95 over every request in the run.

```bash
scripts/build.sh --release
./build/corebank-bench                                   # 32 accounts, 5000 transfers, 4 threads
./build/corebank-bench --transactions 20000 --threads 8 --accounts 64 --window 32
./build/corebank-bench --json > bench-results.json      # machine-readable
scripts/benchmark.sh                                     # both of the above, logged
```

## Results (2026-10-14)

Machine: Apple M3 (8 cores), 16 GiB, macOS, Apple clang 17, Release build (`-O3`), SQLite
3.51 in WAL mode with `synchronous = NORMAL`, database on the local SSD under `/tmp`.

Run A: 32 accounts, 5000 transfers, 4 workers, window 32

| run | count | seconds | per second | p50 ms | p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| single_thread_transfers | 5000 | 1.508 | 3315 | 0.225 | 0.289 |
| multi_thread_transfers | 5000 | 1.697 | 2947 | 12.462 | 13.909 |
| eod_batch | 10032 | 0.271 | 37067 | n/a | n/a |

Run B: 64 accounts, 20000 transfers, 8 workers, window 32

| run | count | seconds | per second | p50 ms | p95 ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| single_thread_transfers | 20000 | 7.164 | 2792 | 0.210 | 0.275 |
| multi_thread_transfers | 20000 | 8.846 | 2261 | 17.095 | 19.561 |
| eod_batch | 40064 | 1.174 | 34132 | n/a | n/a |

## Reading the numbers honestly

- **Worker threads do not raise write throughput on SQLite.** SQLite admits one writer
  at a time, so the multi-threaded run cannot exceed the single-threaded rate; it lands a
  little below it because of the extra hand-offs (queue, future, writer mutex). The pool
  exists for back-pressure, isolation of slow clients and overlapping network I/O with
  database work, not for parallel commits. [ADR-0004](ADR/0004-sqlite.md) accepts this
  trade-off; a row-locking database would let the stripes do real work.
- **Multi-threaded latency is queueing latency.** With 32 requests in flight and roughly
  3000 commits per second, each request waits for about 30 others: 32 / 3000 s is about
  11 ms, which is what p50 shows. The service-level latency underneath is the
  single-thread figure (about 0.2 ms).
- **Durability setting matters.** `synchronous = NORMAL` syncs the WAL at checkpoints,
  not on every commit. `synchronous = FULL` would be slower and is the right choice if
  the last few committed transactions must survive power loss (see
  [PERSISTENCE.md](PERSISTENCE.md)).
- **Before the writer mutex was added** the multi-threaded run reached only about half of
  the single-threaded rate (1550 per second on run A) because worker threads retried in
  SQLite's busy handler. Serialising writers in-process
  (`SqliteRepository::writer_mutex()`) removed that loss; see
  [CONCURRENCY.md](CONCURRENCY.md).
- **EOD throughput** is dominated by two full-table scans (balance check, reconciliation)
  and the CSV export, all inside a handful of transactions.

## Reproducing and comparing

Run the benchmark twice and keep the second result; the first run pays for page-cache
warm-up and SQLite file growth. Compare only runs with identical parameters, build type
and machine. CI runs a short benchmark (`--transactions 500 --threads 2`) purely as a
smoke test of the binary; it does not publish or gate on numbers.
