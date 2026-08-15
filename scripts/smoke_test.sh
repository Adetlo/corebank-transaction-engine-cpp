#!/usr/bin/env bash
# End-to-end smoke test against a throw-away database: CLI flow, TCP flow, reconciliation, EOD.
#
# Usage: scripts/smoke_test.sh
# Env:   COREBANK_BIN, SMOKE_PORT (default: 7499)
# Exit:  0 all checks passed, 4 a check failed
SCRIPT_NAME=smoke_test
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

require_binary
work="$(mktemp -d "${TMPDIR:-/tmp}/corebank-smoke.XXXXXX")"
export COREBANK_DB="${work}/smoke.db" COREBANK_BATCH_DIR="${work}/batch" COREBANK_LOG_DIR="${work}/log"
export COREBANK_PID_FILE="${work}/corebank.pid" COREBANK_PORT="${SMOKE_PORT:-7499}" COREBANK_LOG_LEVEL=WARN
export COREBANK_VAR="${work}"
# shellcheck disable=SC2317,SC2329  # invoked through the EXIT trap
cleanup() {
  "${COREBANK_HOME}/scripts/stop.sh" >/dev/null 2>&1 || true
  rm -rf "${work}"
}
trap cleanup EXIT

checks=0
failures=0
check() {
  local name="$1" expected="$2"
  shift 2
  local rc=0
  "$@" >/dev/null 2>&1 || rc=$?
  checks=$((checks + 1))
  if ((rc == expected)); then
    log_info "PASS ${name}" "exit=${rc}"
  else
    failures=$((failures + 1))
    log_error "FAIL ${name}" "expected=${expected}" "actual=${rc}"
  fi
}
cb() { "${COREBANK_BIN}" --db "${COREBANK_DB}" "$@"; }

log_info "smoke test starting" "workdir=${work}"
check "create settlement account" 0 cb account create --id SMOKE-SETTLE --name "Settlement" --currency GBP --type SETTLEMENT
check "create customer A" 0 cb account create --id SMOKE-A --name "Customer A" --currency GBP
check "create customer B" 0 cb account create --id SMOKE-B --name "Customer B" --currency GBP
check "duplicate account rejected" 2 cb account create --id SMOKE-A --name "Again" --currency GBP
check "credit settlement" 0 cb transaction credit --account SMOKE-SETTLE --amount 10000.00 --currency GBP --key SMOKE-K1
check "transfer" 0 cb transfer --from SMOKE-SETTLE --to SMOKE-A --amount 250.00 --currency GBP --key SMOKE-T1
check "idempotent replay" 0 cb transfer --from SMOKE-SETTLE --to SMOKE-A --amount 250.00 --currency GBP --key SMOKE-T1
check "idempotency conflict" 2 cb transfer --from SMOKE-SETTLE --to SMOKE-A --amount 251.00 --currency GBP --key SMOKE-T1
check "insufficient funds" 2 cb transfer --from SMOKE-B --to SMOKE-A --amount 1.00 --currency GBP
check "invalid amount" 1 cb transfer --from SMOKE-A --to SMOKE-B --amount 1.234 --currency GBP
check "account history" 0 cb account history --id SMOKE-A
check "reconcile" 0 cb reconcile
check "database health" 0 cb health

check "engine start" 0 "${COREBANK_HOME}/scripts/start.sh"
check "tcp health" 0 "${COREBANK_HOME}/scripts/healthcheck.sh"
check "tcp transfer" 0 cb send --host 127.0.0.1 --port "${COREBANK_PORT}" "TRANSFER|key=SMOKE-N1|from=SMOKE-A|to=SMOKE-B|amount=25.00|ccy=GBP|ref=smoke"
check "tcp malformed" 2 cb send --host 127.0.0.1 --port "${COREBANK_PORT}" "not a message"
check "engine stop" 0 "${COREBANK_HOME}/scripts/stop.sh"

check "eod run" 0 "${COREBANK_HOME}/scripts/run_eod.sh"
check "eod idempotent rerun" 0 "${COREBANK_HOME}/scripts/run_eod.sh"
check "eod status" 0 cb eod status
check "reconcile after eod" 0 "${COREBANK_HOME}/scripts/reconcile.sh"

balance_a="$(cb --json account show --id SMOKE-A | sed -E 's/.*"balance":"([^"]+)".*/\1/')"
checks=$((checks + 1))
if [[ "${balance_a}" == "225.00" ]]; then
  log_info "PASS final balance" "SMOKE-A=${balance_a}"
else
  failures=$((failures + 1))
  log_error "FAIL final balance" "expected=225.00" "actual=${balance_a}"
fi

if ((failures == 0)); then
  log_info "smoke test passed" "checks=${checks}"
  exit "${EXIT_OK}"
fi
die "${EXIT_SYSTEM}" "smoke test failed" "checks=${checks}" "failures=${failures}"
