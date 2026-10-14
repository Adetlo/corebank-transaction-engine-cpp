# Shell Automation

The `scripts/` directory contains the operational tooling around the `corebank` binary:
build, test, start, stop, health, end-of-day, reconciliation, smoke test, benchmark, clean
and a guided demo. All scripts are bash, share one library and follow the same
conventions. They work on synthetic data and are written for a local or containerised
educational deployment.

Related documents: [OPERATIONS.md](OPERATIONS.md), [EOD_BATCH.md](EOD_BATCH.md),
[TESTING.md](TESTING.md), [BENCHMARKS.md](BENCHMARKS.md).

## Design principles

### Strict mode and a shared library

`scripts/lib/common.sh` is sourced, never executed. It sets `set -euo pipefail`, guards
against double loading with `_COREBANK_COMMON_LOADED`, resolves every path variable and
defines the helpers used by all scripts. Each script begins with:

```bash
SCRIPT_NAME=run_eod
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"
```

### Environment handling

All inputs are environment variables with defaults, so the same scripts run from a
checkout, a systemd unit or a container without edits. `common.sh` exports `COREBANK_DB`,
`COREBANK_BATCH_DIR` and `COREBANK_PORT` so the binary sees the same values the scripts use.

| Variable | Default | Used for |
| --- | --- | --- |
| `COREBANK_HOME` | Parent of `scripts/` | Repository or installation root |
| `BUILD_DIR` | `$COREBANK_HOME/build` | CMake build directory |
| `BUILD_TYPE` | `Release` | CMake build type |
| `COREBANK_BIN` | `$BUILD_DIR/corebank` | Engine binary |
| `COREBANK_VAR` | `$COREBANK_HOME/var` | Runtime state root |
| `COREBANK_DB` | `$COREBANK_VAR/corebank.db` | SQLite database |
| `COREBANK_BATCH_DIR` | `$COREBANK_VAR/batch` | EOD output |
| `COREBANK_LOG_DIR` | `$COREBANK_VAR/log` | Script and engine logs |
| `COREBANK_PID_FILE` | `$COREBANK_VAR/run/corebank.pid` | Engine pid |
| `COREBANK_HOST` | `127.0.0.1` | Host for TCP checks |
| `COREBANK_PORT` | `7400` | Engine TCP port |

### Exit codes

The scripts reuse the binary's exit-code policy (`src/cli/output.cpp`) through read-only
constants: `EXIT_OK=0`, `EXIT_USAGE=1`, `EXIT_REJECTED=2`, `EXIT_CONTROL=3`,
`EXIT_SYSTEM=4`. `describe_exit` turns a code into text for log lines, and `die CODE MSG`
logs an error and exits with that code. Wrapper scripts such as `run_eod.sh` propagate the
binary's exit status through `PIPESTATUS[0]` so that `tee` does not mask it.

### Structured logs

`log LEVEL MSG [key=value ...]` writes one line to stderr:

```text
ts=2026-10-14T18:02:11Z level=INFO script=run_eod msg="starting end-of-day" business_date=2026-10-14 db=/srv/corebank/var/corebank.db log=/srv/corebank/var/log/eod-2026-10-14.log
ts=2026-10-14T18:02:14Z level=ERROR script=run_eod msg="end-of-day failed; fix the cause and re-run to resume" business_date=2026-10-14 log=/srv/corebank/var/log/eod-2026-10-14.log
```

`log_info`, `log_warn` and `log_error` are the level shortcuts. Script logs go to stderr;
engine output captured by a script is appended to a file under `COREBANK_LOG_DIR` and also
echoed through `tee`.

### Helpers

| Helper | Behaviour |
| --- | --- |
| `require_command cmd...` | Exit 4 if a command is missing |
| `require_binary` | Exit 4 if `COREBANK_BIN` is not executable |
| `ensure_dirs` | Create `COREBANK_VAR`, batch, log and pid directories; chmod 750 |
| `engine_pid` | Print the pid from the pid file if that process is alive, else return 1 |
| `with_timeout SECONDS cmd...` | Portable deadline using a background watchdog (`coreutils timeout` is absent on macOS) |
| `describe_exit CODE` | Human-readable name of an exit code |

### Bash 3.2 compatibility

macOS ships bash 3.2, where expanding an empty array under `set -u` is an error. Every
optional array is therefore expanded as `${arr[@]+"${arr[@]}"}`, for example in
`build.sh`:

```bash
cmake -S "${COREBANK_HOME}" -B "${BUILD_DIR}" ${gen_args[@]+"${gen_args[@]}"} \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" ${cmake_args[@]+"${cmake_args[@]}"}
```

The same idiom appears in `start.sh` (`config_args`), `test.sh` (`label_args`) and
`reconcile.sh` (`args`).

## Script reference

| Script | Purpose | Usage | Environment | Exit codes | Logs |
| --- | --- | --- | --- | --- | --- |
| `build.sh` | Configure and build with CMake (Ninja if available) | `build.sh [--debug\|--release] [--asan] [--ubsan] [--tsan] [--coverage] [--no-tests]` | `BUILD_DIR`, `BUILD_TYPE`, `CMAKE_GENERATOR`, `CXX` | 0 built, 1 usage, 4 configure or build failure | stderr |
| `test.sh` | Run ctest, optionally one label | `test.sh [label] [extra ctest args]` with labels `unit integration concurrency batch messaging property cli` | `BUILD_DIR`, `CTEST_PARALLEL` (4) | 0 passed, 4 build missing or tests failed | stderr |
| `start.sh` | Start `corebank serve` under `nohup`, write pid file, wait for TCP health | `start.sh [--foreground]` | `COREBANK_DB`, `COREBANK_PORT`, `COREBANK_LOG_DIR`, `COREBANK_PID_FILE`, `COREBANK_CONFIG`, `COREBANK_WORKERS`, `COREBANK_LOG_LEVEL`, `START_TIMEOUT` (15) | 0 running, 1 already running, 4 failed to start | `$COREBANK_LOG_DIR/engine.log` |
| `stop.sh` | `SIGTERM`, then `SIGKILL` after the grace period | `stop.sh` | `COREBANK_PID_FILE`, `STOP_TIMEOUT` (20) | 0 stopped or not running, 4 had to be killed | stderr |
| `healthcheck.sh` | TCP `PING` or database integrity check | `healthcheck.sh [--db]` | `COREBANK_HOST`, `COREBANK_PORT`, `COREBANK_DB`, `COREBANK_BIN` | 0 healthy, 4 unhealthy | stderr |
| `run_eod.sh` | Run or resume the EOD batch for a date | `run_eod.sh [YYYY-MM-DD]` (default: today UTC) | `COREBANK_DB`, `COREBANK_BATCH_DIR`, `COREBANK_LOG_DIR`, `COREBANK_FAIL_STAGE` (testing) | 0 completed or already complete, 1 bad date, 3 batch failed, 4 system error | `$COREBANK_LOG_DIR/eod-<date>.log` |
| `reconcile.sh` | Run reconciliation, fail loudly | `reconcile.sh [YYYY-MM-DD] [--json]` | `COREBANK_DB`, `COREBANK_LOG_DIR` | 0 passed, 1 bad argument, 3 failed, 4 system error | `$COREBANK_LOG_DIR/reconcile.log` |
| `smoke_test.sh` | 23 end-to-end checks on a throw-away database | `smoke_test.sh` | `COREBANK_BIN`, `SMOKE_PORT` (7499) | 0 all passed, 4 a check failed | stderr (`PASS`/`FAIL` per check) |
| `benchmark.sh` | Local synthetic benchmark, text then JSON | `benchmark.sh [--transactions N] [--threads N] [--accounts N]` | `BUILD_DIR`, `BENCH_OUT` (`bench-results.json`) | 0 ok, 4 binary missing or failed | stderr, `BENCH_OUT` |
| `clean.sh` | Remove build output; `--all` also removes runtime state | `clean.sh [--all]` | `BUILD_DIR`, `COREBANK_VAR` | 0 cleaned, 1 engine still running | stderr |
| `demo.sh` | Guided synthetic demo | `demo.sh [--skip-build] [--keep] [--remote]` | `DEMO_DIR` (`.demo`), `DEMO_PORT` (7410), `DEMO_CLIENTS` (8), `DEMO_TRANSFERS_PER_CLIENT` (25) | 0 completed, 3 reconciliation or EOD failed, 4 system error | stdout |

Notes on individual scripts:

* `start.sh` polls `corebank health --host --port` every 0.2 seconds until `START_TIMEOUT`;
  if the process dies or the deadline passes it removes the pid file and exits 4.
* `stop.sh` treats a missing or stale pid file as "not running" and exits 0.
* `run_eod.sh` validates the date with a `YYYY-MM-DD` regular expression, warns when
  `COREBANK_FAIL_STAGE` is set, and on exit 3 tells the operator to fix the cause and re-run
  to resume (see [EOD_BATCH.md](EOD_BATCH.md)).
* `smoke_test.sh` creates a temporary directory, points every `COREBANK_*` path at it,
  runs the CLI flow (accounts, credit, transfer, idempotent replay, conflict, insufficient
  funds, invalid amount, history, reconcile, health), the TCP flow (start, health, transfer,
  malformed message, stop), the EOD flow (run, idempotent re-run, status, reconcile after
  EOD) and a final balance assertion of `225.00`, then stops the engine and deletes the
  directory from an `EXIT` trap.
* `demo.sh --remote` is used by `docker-compose.yml`: the engine is already running, the
  demo shares its database volume and does not build, start or stop anything. Account
  identifiers receive a time-based suffix so the demo can be repeated against the same
  database.
* `clean.sh` refuses to run while the engine is up.

## ShellCheck in CI

The `shellcheck` job in `.github/workflows/ci.yml` runs:

```bash
shellcheck -x -S style scripts/*.sh scripts/lib/common.sh
```

`-x` follows `source` directives (each script carries a `# shellcheck source=` hint), and
`-S style` fails on style-level findings, not only errors. The few suppressions are inline
and explained: `SC2034` on the exit-code constants that are used by other scripts, and
`SC2329` on `cleanup` functions that are only invoked through the `EXIT` trap.

## Typical operator workflows

### Daily run

```bash
scripts/healthcheck.sh --db                 # storage integrity before the close
scripts/reconcile.sh                        # exit 3 stops here if the ledger disagrees
scripts/run_eod.sh                          # today's UTC date
build/corebank eod status                   # COMPLETED, every stage attempts=1
ls -l "$COREBANK_VAR/batch/$(date -u +%Y-%m-%d)/"
```

A scheduler (cron or a systemd timer) can call `scripts/run_eod.sh` directly; the exit
code distinguishes a clean close (0) from a batch that needs attention (3) and from an
environment problem (4).

### Incident re-run

```bash
scripts/run_eod.sh 2026-10-13               # exit 3
build/corebank eod status --date 2026-10-13 # shows the FAILED stage and failure reason
# fix the cause (ledger, filesystem, configuration)
scripts/run_eod.sh 2026-10-13               # resumes: completed stages are skipped, attempt=2
build/corebank audit --type BATCH_RESUMED --limit 5
```

The log file `$COREBANK_LOG_DIR/eod-2026-10-13.log` accumulates both attempts because
`run_eod.sh` appends with `tee -a`.

### Smoke test after a deploy

```bash
scripts/build.sh --release
scripts/smoke_test.sh                        # 23 checks against a temporary database
scripts/start.sh
scripts/healthcheck.sh
```

`smoke_test.sh` uses port 7499 by default so it does not collide with a running engine on
7400; set `SMOKE_PORT` when 7499 is taken.

### Local sanitizer pass

```bash
BUILD_DIR=build-asan scripts/build.sh --debug --asan --ubsan && BUILD_DIR=build-asan scripts/test.sh
BUILD_DIR=build-tsan scripts/build.sh --debug --tsan && BUILD_DIR=build-tsan scripts/test.sh concurrency
```

`clean.sh` removes `build/` and every `build-*` directory in one step.

### Full demonstration

```bash
scripts/demo.sh                   # build, accounts, transfers, concurrent TCP load, reversal,
                                  # reconciliation, EOD with injected failure, resume, audit
scripts/demo.sh --keep            # keep .demo/ for inspection
```
