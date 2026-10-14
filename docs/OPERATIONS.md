# Operations

This document covers running the engine as an operator: runtime layout, configuration,
start/stop/health procedures, logging, signals, exit codes, monitoring, backup and
troubleshooting. The engine is an educational implementation that processes synthetic data;
nothing here is a production deployment guide for a real bank.

Related documents: [ARCHITECTURE.md](ARCHITECTURE.md), [EOD_BATCH.md](EOD_BATCH.md),
[SHELL_AUTOMATION.md](SHELL_AUTOMATION.md), [PERSISTENCE.md](PERSISTENCE.md),
[SECURITY.md](SECURITY.md), [LIMITATIONS.md](LIMITATIONS.md),
[ADR-0004 SQLite](ADR/0004-sqlite.md).

## Runtime layout

Defaults come from `scripts/lib/common.sh` and `include/corebank/infrastructure/config.hpp`.
Relative paths in the binary resolve against the working directory.

| Path | Default | Purpose |
| --- | --- | --- |
| `COREBANK_HOME` | Repository root | Installation root for the scripts |
| `COREBANK_VAR` | `$COREBANK_HOME/var` | Runtime state root; `ensure_dirs` creates it with mode 0750 |
| `COREBANK_DB` | `$COREBANK_VAR/corebank.db` | SQLite database, WAL mode (`corebank.db-wal`, `corebank.db-shm` appear alongside) |
| `COREBANK_BATCH_DIR` | `$COREBANK_VAR/batch` | EOD output: `<date>/daily_summary.json`, `<date>/transactions.csv` (mode 0640) |
| `COREBANK_LOG_DIR` | `$COREBANK_VAR/log` | `engine.log` (from `start.sh`), `eod-<date>.log`, `reconcile.log` |
| `COREBANK_PID_FILE` | `$COREBANK_VAR/run/corebank.pid` | Written by `start.sh`, mode 0640, removed by `stop.sh` |

In the Docker image and the systemd example the state root is `/srv/corebank/var`, owned
by the unprivileged `corebank` user, and the configuration file is
`/etc/corebank/default.yaml`. `.gitignore` excludes `var/`, `*.db*`, `*.log` and `*.pid`.

## Configuration reference

Configuration is loaded in this order (`Config::load`, `src/infrastructure/config.cpp`):
built-in defaults, then the flat `key: value` YAML file named by `--config` or
`COREBANK_CONFIG`, then `COREBANK_*` environment variables, then CLI options
(`--db`, `--business-date`, `--log-level`, and `serve --address/--port`). Nested YAML keys
and unknown keys are rejected with `ConfigurationError`. `corebank config show [--json]`
prints the effective values.

| Key | Environment variable | Default (code) | Valid range | Meaning |
| --- | --- | --- | --- | --- |
| `database_path` | `COREBANK_DB` | `var/corebank.db` | non-empty | SQLite file; parent directories are created |
| `worker_count` | `COREBANK_WORKERS` | 4 | 1 to 256 | Processor worker threads; pool size is `worker_count + 2` |
| `queue_size` | `COREBANK_QUEUE_SIZE` | 1024 | 1 to 1000000 | Bounded request queue capacity |
| `log_level` | `COREBANK_LOG_LEVEL` | `INFO` | `DEBUG`, `INFO`, `WARN`, `ERROR` | Minimum level emitted |
| `log_file` | `COREBANK_LOG_FILE` | empty (stderr) | path | Append JSON lines to a file instead of stderr |
| `batch_dir` | `COREBANK_BATCH_DIR` | `var/batch` | non-empty | EOD output directory |
| `max_transaction_minor` | `COREBANK_MAX_TRANSACTION_MINOR` | 100000000000 | 1 to int64 max | Largest single amount in minor units (1,000,000,000.00 at two decimal places; same value in `config/default.yaml`) |
| `listen_address` | `COREBANK_LISTEN_ADDRESS` | `127.0.0.1` | non-empty | TCP bind address |
| `listen_port` | `COREBANK_PORT` | 7400 | 1 to 65535 | TCP port |
| `max_connections` | `COREBANK_MAX_CONNECTIONS` | 64 | 1 to 10000 | Concurrent client limit; excess connections are refused |
| `idle_timeout_seconds` | `COREBANK_IDLE_TIMEOUT_SECONDS` | 30 | 1 to 86400 | Idle client disconnect |
| `max_message_bytes` | `COREBANK_MAX_MESSAGE_BYTES` | 4096 | 64 to 1048576 | Maximum line length on the wire |
| `request_timeout_ms` | `COREBANK_REQUEST_TIMEOUT_MS` | 5000 | 10 to 600000 | Time a request may wait for the processor |
| `fail_stage` | `COREBANK_FAIL_STAGE` | none | stage name | EOD failure injection, testing only |
| `business_date` | `COREBANK_BUSINESS_DATE` | none (clock) | `YYYY-MM-DD` | Business date override |

`COREBANK_CONFIG` names the file and is read by the CLI, not by `Config` itself. The
scripts add their own variables (`COREBANK_HOME`, `BUILD_DIR`, `COREBANK_BIN`, `COREBANK_VAR`,
`COREBANK_LOG_DIR`, `COREBANK_PID_FILE`, `COREBANK_HOST`); see
[SHELL_AUTOMATION.md](SHELL_AUTOMATION.md).

## Start, stop and health

### Scripts

```bash
scripts/build.sh --release          # build/corebank
scripts/start.sh                    # nohup, writes pid file, waits for TCP health (START_TIMEOUT=15)
scripts/healthcheck.sh              # TCP PING; exit 0 healthy, 4 unhealthy
scripts/healthcheck.sh --db         # PRAGMA quick_check on the database file
scripts/stop.sh                     # SIGTERM, SIGKILL after STOP_TIMEOUT=20 seconds
```

`start.sh --foreground` replaces the shell with `corebank serve` for supervisors that
manage the process themselves.

### systemd (example unit)

`systemd/corebank-engine.service` is labelled EXAMPLE and must be adapted before use. It
runs `/usr/local/bin/corebank serve` as user and group `corebank`, with
`Environment=` entries for `COREBANK_CONFIG`, `COREBANK_DB`, `COREBANK_BATCH_DIR` and
`COREBANK_LOG_LEVEL`, an optional `EnvironmentFile=-/etc/corebank/engine.env`, an
`ExecStartPost` loop that waits for `corebank health --host 127.0.0.1 --port 7400`,
`KillSignal=SIGTERM`, `KillMode=mixed`, `TimeoutStopSec=30`, `Restart=on-failure`,
`RestartSec=5s`, `StartLimitIntervalSec=300`, `StartLimitBurst=5`, `UMask=0027` and the
hardening directives `NoNewPrivileges`, `ProtectSystem=strict`, `ProtectHome`,
`PrivateTmp`, `PrivateDevices`, `ProtectKernelTunables`, `ProtectControlGroups`,
`RestrictAddressFamilies=AF_INET AF_UNIX`, `ReadWritePaths=/srv/corebank/var` and
`LimitNOFILE=4096`.

```bash
sudo cp systemd/corebank-engine.service /etc/systemd/system/
sudo systemctl daemon-reload && sudo systemctl enable --now corebank-engine
journalctl -u corebank-engine -f        # JSON lines from stderr
```

### Docker

`Dockerfile` is a two-stage build on `debian:bookworm-slim`: the builder compiles with
tests off and the benchmark on, then strips the binaries; the runtime stage installs only
`libsqlite3-0` and `bash`, creates user and group `corebank` with uid and gid 10001, copies
`corebank`, `corebank-bench`, `config/default.yaml` and `scripts/`, sets `COREBANK_*`
defaults (listen on `0.0.0.0`, state under `/srv/corebank/var`), declares
`VOLUME /srv/corebank/var`, `EXPOSE 7400`, `STOPSIGNAL SIGTERM` and a `HEALTHCHECK` that
runs `corebank health --host 127.0.0.1 --port 7400` every 10 seconds.

```bash
docker compose up -d                 # service "engine", host port bound to 127.0.0.1:7400
docker compose run --rm demo         # profile "demo": scripts/demo.sh --skip-build --remote
docker compose stop                  # stop_grace_period: 30s
```

The `demo` service waits for `engine` to report healthy and shares the `corebank-var`
volume.

## Logging

`src/infrastructure/logger.cpp` writes one JSON object per line to stderr, or to
`log_file` when set, protected by a mutex and filtered by level. Every line has `ts`
(UTC, microsecond precision), `level` and `event`; other fields depend on the event.

```json
{"ts":"2026-10-14T18:02:11.482913Z","level":"INFO","event":"engine.started","address":"127.0.0.1","port":7400,"workers":4,"database":"var/corebank.db"}
{"ts":"2026-10-14T18:02:11.482950Z","level":"INFO","event":"tcp.listening","address":"127.0.0.1","port":7400,"max_connections":64}
{"ts":"2026-10-14T18:02:15.104377Z","level":"INFO","event":"transaction.posted","transaction_id":"TXN-...","type":"TRANSFER","status":"POSTED","account_id":"CUST-A","amount":"25.00 GBP","duration_ms":1.42}
{"ts":"2026-10-14T18:02:16.221004Z","level":"INFO","event":"transaction.replayed","transaction_id":"TXN-...","idempotency_key":"SMOKE-T1"}
{"ts":"2026-10-14T18:03:40.005120Z","level":"INFO","event":"eod.stage_completed","batch_id":"BATCH-...","stage":"RECONCILIATION","detail":"reconciliation ...","duration_ms":3.8}
{"ts":"2026-10-14T18:05:00.000481Z","level":"INFO","event":"engine.stopped","completed":212}
```

Identifier and amount values above are placeholders; the field names are the real ones.

| Event | Level | Notable fields |
| --- | --- | --- |
| `engine.started`, `engine.stopping`, `engine.stopped` | INFO | `address`, `port`, `workers`, `database`; `signal`; `completed` |
| `processor.started`, `processor.stopped` | INFO | `workers`, `queue_capacity`; `mode`, `completed` |
| `tcp.listening`, `tcp.stopped`, `tcp.connection_refused` | INFO, INFO, WARN | `address`, `port`, `max_connections`; `total_connections`; `reason` |
| `tcp.message` | DEBUG | `bytes`, `duration_ms` |
| `message.rejected` | WARN | `error` (protocol error code) |
| `account.created` | INFO | `account_id`, `currency`, `duration_ms` |
| `transaction.posted`, `transaction.rejected` | INFO | `transaction_id`, `type`, `status`, `account_id`, `amount`, `duration_ms` |
| `transaction.replayed` | INFO | `transaction_id`, `idempotency_key` |
| `transaction.failed` | ERROR | `transaction_id`, `error`, `duration_ms` |
| `reconciliation.completed` | INFO or WARN | `passed`, `failed_checks`, `duration_ms` |
| `eod.started`, `eod.resumed`, `eod.already_completed`, `eod.stage_skipped`, `eod.stage_completed`, `eod.stage_failed` | INFO (ERROR for `stage_failed`) | `batch_id`, `business_date`, `attempt`, `stage`, `detail`, `reason`, `duration_ms` |

The scripts log separately to stderr in `ts= level= script= msg=` form (see
[SHELL_AUTOMATION.md](SHELL_AUTOMATION.md)).

## Signals and graceful shutdown

`src/infrastructure/signal_handler.cpp` installs handlers for `SIGTERM` and `SIGINT` using
a self-pipe: the handler only sets atomics and writes one byte to a non-blocking pipe, and
`SignalHandler::wait` polls that pipe. `SIGPIPE` is set to `SIG_IGN` so a client that
disconnects mid-write cannot terminate the process.

`cmd_serve` (`src/cli/commands_serve.cpp`) performs the shutdown in this order:

1. Log `engine.stopping` with the signal number.
2. `TcpServer::stop()`: stop accepting and close client connections.
3. `TransactionProcessor::shutdown_gracefully()`: finish queued work.
4. Append an `ENGINE_STOPPED` audit event with the signal and the completed count.
5. Log `engine.stopped`, uninstall the handlers, return exit code 0.

`stop.sh`, the systemd unit and Docker all deliver `SIGTERM` first. The Docker CI job
checks that `docker stop -t 30` produces an `engine.stopped` log line.

## Exit codes

Defined in `include/corebank/cli/output.hpp` and mapped from error codes in
`src/cli/output.cpp` (`exit_code_for`). The scripts reuse the same values.

| Code | Name | Examples |
| --- | --- | --- |
| 0 | ok | Posted, replayed, reconciliation passed, batch completed or already completed |
| 1 | usage | Missing option, invalid amount, unknown command, malformed message |
| 2 | business rejection | Insufficient funds, unknown account, idempotency conflict, duplicate account, `send` receiving an `ERR` response |
| 3 | control failure | Reconciliation failed, EOD stage failed, batch already running, `eod status` of a `FAILED` batch |
| 4 | system error | Persistence or configuration error, queue full, timeout, network error, unreachable engine |

## Monitoring hints

| Check | Command | Healthy result |
| --- | --- | --- |
| Liveness over TCP | `corebank health --host 127.0.0.1 --port 7400` | `engine healthy: OK|...`, exit 0 |
| Database integrity | `corebank --db $COREBANK_DB health` | `database healthy: <path>`, exit 0 |
| Ledger consistency | `corebank reconcile [--date D] [--json]` | Every check `PASS`, exit 0; exit 3 otherwise |
| Batch state | `corebank eod status [--date D]` | `status: COMPLETED`; exit 3 if `FAILED` |
| Recent activity | `corebank audit --limit 50` | Append-only trail; filter with `--entity`, `--batch`, `--type` |
| Engine lifecycle | `corebank audit --type ENGINE_STARTED`, `--type ENGINE_STOPPED` | Each start and clean stop is recorded |

Every reconciliation run writes a `RECONCILIATION_RUN` audit event, so periodic
`scripts/reconcile.sh` invocations leave a verifiable history.

## Backup and restore (SQLite WAL)

Connections are opened with `PRAGMA journal_mode = WAL`, `synchronous = NORMAL`,
`foreign_keys = ON` and a busy timeout (`src/infrastructure/sqlite/sqlite_repository.cpp`).
In WAL mode recent commits may live in `corebank.db-wal` until a checkpoint, so copying
only `corebank.db` while the engine runs produces an inconsistent snapshot.

Recommended procedure with the `sqlite3` command-line tool, which produces a consistent
copy without stopping the engine:

```bash
sqlite3 "$COREBANK_DB" ".backup '/backup/corebank-$(date -u +%Y%m%dT%H%M%SZ).db'"
```

Alternative when the engine is stopped (`scripts/stop.sh` first): copy `corebank.db`,
`corebank.db-wal` and `corebank.db-shm` together, or run `PRAGMA wal_checkpoint(TRUNCATE)`
and then copy `corebank.db` alone.

Restore: stop the engine, replace `corebank.db` and remove any stale `-wal` and `-shm`
files, run `corebank --db "$COREBANK_DB" health` and `corebank reconcile`, then start the
engine. Back up `COREBANK_BATCH_DIR` with the database; a `COMPLETED` batch does not
regenerate its files (see [EOD_BATCH.md](EOD_BATCH.md)).

## Troubleshooting

| Symptom | Likely cause | Action |
| --- | --- | --- |
| `start.sh` exits 1 "engine already running" | Live pid in `COREBANK_PID_FILE` | `scripts/stop.sh` or use the running instance |
| `start.sh` exits 4 "engine exited during start-up" | Port in use, unwritable database path, bad configuration | Read `$COREBANK_LOG_DIR/engine.log`; try `corebank config show` |
| `start.sh` exits 4 "did not become healthy in time" | Slow start or `COREBANK_HOST`/`COREBANK_PORT` do not match the listener | Raise `START_TIMEOUT`, verify `listen_address` |
| `health --host` exits 4 | Engine not listening or wrong port | `scripts/healthcheck.sh`; check `tcp.listening` in the log |
| Client receives `ERR|code=TOO_MANY_CONNECTIONS` or `tcp.connection_refused` logged | `max_connections` reached | Raise `COREBANK_MAX_CONNECTIONS` or reduce clients |
| `ERR|code=PAYLOAD_TOO_LARGE`, connection closed | Line longer than `max_message_bytes` | Shorten the message or raise the limit |
| `ERR|code=TIMEOUT` or `QUEUE_FULL` | Processor saturated | Raise `worker_count`, `queue_size` or `request_timeout_ms`; check `duration_ms` in logs |
| `reconcile` exits 3 | Stored balance diverged from postings, or storage integrity failed | Read the failing check; restore from backup if integrity is broken |
| `eod run` exits 3 | A stage failed | `eod status --date D`, fix, re-run; see [EOD_BATCH.md](EOD_BATCH.md) |
| `eod run` fails in `PRECHECK` with "is still RUNNING" | Another date's batch was interrupted | Re-run that date to resume it |
| `stop.sh` exits 4 | Engine ignored `SIGTERM` within `STOP_TIMEOUT` | Inspect the log for a stuck stage; `SIGKILL` was sent and the pid file removed |
| `ConfigurationError: unknown configuration key` | Typo or nested YAML | Keys are flat `key: value` only |
| `cannot open log file` | `log_file` path not writable | Fix permissions or leave `log_file` empty for stderr |

## Security notes

The listener binds to loopback by default, there is no authentication on the wire protocol,
and the Docker image exposes `0.0.0.0` inside the container only. Run the engine as an
unprivileged user, keep `COREBANK_VAR` at mode 0750 and batch files at 0640, and place any
network exposure behind a trusted gateway. The threat model, protocol limits and reporting
process are in [SECURITY.md](SECURITY.md) and the repository-level `SECURITY.md`.
