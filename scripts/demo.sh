#!/usr/bin/env bash
# Guided synthetic demo: build, start the engine, accounts, credits, transfers (CLI and TCP),
# a concurrent workload, reconciliation, end-of-day, audit trail, clean stop.
#
# Usage: scripts/demo.sh [--skip-build] [--keep] [--remote]
#        --remote: the engine is already running at COREBANK_HOST:COREBANK_PORT (Docker
#        compose); the demo shares its database and does not start, stop or build anything.
# Env:   DEMO_DIR (default: .demo under the repo), DEMO_PORT (default: 7410),
#        DEMO_CLIENTS (parallel TCP clients, default 8), DEMO_TRANSFERS_PER_CLIENT (default 25)
# Exit:  0 demo completed, 3 reconciliation/EOD failed, 4 system error
SCRIPT_NAME=demo
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

skip_build=false
keep=false
remote=false
for arg in "$@"; do
  case "${arg}" in
    --skip-build) skip_build=true ;;
    --keep) keep=true ;;
    --remote) remote=true skip_build=true keep=true ;;
    *) die "${EXIT_USAGE}" "unknown option: ${arg}" ;;
  esac
done

if [[ "${remote}" == true ]]; then
  # Shared database with the running engine (Docker volume); keep its paths.
  demo_dir="${COREBANK_VAR}"
  ensure_dirs
else
  demo_dir="${DEMO_DIR:-${COREBANK_HOME}/.demo}"
  rm -rf "${demo_dir}"
  mkdir -p "${demo_dir}"
  export COREBANK_VAR="${demo_dir}" COREBANK_DB="${demo_dir}/demo.db" COREBANK_BATCH_DIR="${demo_dir}/batch"
  export COREBANK_LOG_DIR="${demo_dir}/log" COREBANK_PID_FILE="${demo_dir}/corebank.pid"
  export COREBANK_PORT="${DEMO_PORT:-7410}"
fi
export COREBANK_LOG_LEVEL="${COREBANK_LOG_LEVEL:-WARN}"
clients="${DEMO_CLIENTS:-8}"
per_client="${DEMO_TRANSFERS_PER_CLIENT:-25}"
# Unique suffix so that a remote demo can be re-run against the same database.
run_id="$(date -u +%H%M%S)"

# shellcheck disable=SC2329  # invoked through the EXIT trap
cleanup() {
  if [[ "${remote}" != true ]]; then
    "${COREBANK_HOME}/scripts/stop.sh" >/dev/null 2>&1 || true
  fi
  if [[ "${keep}" != true ]]; then
    rm -rf "${demo_dir}"
  else
    log_info "demo state kept" "dir=${demo_dir}"
  fi
}
trap cleanup EXIT

step() { printf '\n==> %s\n' "$*"; }
cb() { "${COREBANK_BIN}" --db "${COREBANK_DB}" "$@"; }
send() { cb send --host "${COREBANK_HOST}" --port "${COREBANK_PORT}" "$@"; }

if [[ "${skip_build}" != true ]]; then
  step "Building (Release)"
  "${COREBANK_HOME}/scripts/build.sh" --release --no-tests >/dev/null
fi
require_binary

settle="SETTLEMENT-GBP-${run_id}"
fees="FEE-INCOME-GBP-${run_id}"
cust_a="CUSTOMER-A-${run_id}"
cust_b="CUSTOMER-B-${run_id}"
k="DEMO-${run_id}"

step "Creating synthetic accounts"
cb account create --id "${settle}" --name "Settlement Account" --currency GBP --type SETTLEMENT >/dev/null
cb account create --id "${fees}" --name "Fee Income Account" --currency GBP --type FEE_INCOME >/dev/null
cb account create --id "${cust_a}" --name "Customer A" --currency GBP >/dev/null
cb account create --id "${cust_b}" --name "Customer B" --currency GBP >/dev/null
cb account list

step "Crediting the settlement account (external funds in)"
cb transaction credit --account "${settle}" --amount 1000000.00 --currency GBP --key "${k}-OPENING" --reference "opening float" | sed -n '1,4p'

step "Transfers through the CLI (with idempotency keys)"
cb transfer --from "${settle}" --to "${cust_a}" --amount 5000.00 --currency GBP --key "${k}-T1" --reference "salary" | sed -n '1,4p'
cb transfer --from "${settle}" --to "${cust_b}" --amount 2500.00 --currency GBP --key "${k}-T2" --reference "salary" | sed -n '1,4p'
printf -- '-- same key, same request: replayed, not re-posted\n'
cb transfer --from "${settle}" --to "${cust_a}" --amount 5000.00 --currency GBP --key "${k}-T1" --reference "salary" | sed -n '3p'
printf -- '-- same key, different amount: conflict (exit 2)\n'
cb transfer --from "${settle}" --to "${cust_a}" --amount 5001.00 --currency GBP --key "${k}-T1" --reference "salary" || true
printf -- '-- insufficient funds: recorded as REJECTED (exit 2)\n'
cb transfer --from "${cust_b}" --to "${cust_a}" --amount 99999.00 --currency GBP --key "${k}-T3" --reference "too much" | sed -n '3,4p;8p' || true
printf -- '-- fee: customer pays 1.50 to fee income\n'
cb transfer --from "${cust_a}" --to "${fees}" --amount 1.50 --currency GBP --key "${k}-FEE1" --reference "transfer fee" | sed -n '3p'

if [[ "${remote}" == true ]]; then
  step "Engine already running at ${COREBANK_HOST}:${COREBANK_PORT}"
  cb health --host "${COREBANK_HOST}" --port "${COREBANK_PORT}"
else
  step "Starting the engine (TCP listener on 127.0.0.1:${COREBANK_PORT})"
  "${COREBANK_HOME}/scripts/start.sh"
  "${COREBANK_HOME}/scripts/healthcheck.sh"
fi

step "Concurrent workload: ${clients} TCP clients x ${per_client} transfers, opposite directions"
start_ns=$(date +%s)
for ((c = 0; c < clients; c++)); do
  (
    for ((i = 0; i < per_client; i++)); do
      if ((c % 2 == 0)); then
        msg="TRANSFER|key=${k}-C${c}-${i}|from=${cust_a}|to=${cust_b}|amount=1.25|ccy=GBP|ref=load"
      else
        msg="TRANSFER|key=${k}-C${c}-${i}|from=${cust_b}|to=${cust_a}|amount=0.75|ccy=GBP|ref=load"
      fi
      send "${msg}" >/dev/null || true
    done
  ) &
done
wait
elapsed=$(($(date +%s) - start_ns))
printf 'completed %d transfers in ~%ss\n' "$((clients * per_client))" "${elapsed}"
printf -- '-- duplicate message over TCP (replayed=1):\n'
send "TRANSFER|key=${k}-C0-0|from=${cust_a}|to=${cust_b}|amount=1.25|ccy=GBP|ref=load"
printf -- '-- malformed message:\n'
send "TRANSFER|key=${k}-X|from=${cust_a}|to=${cust_b}|amount=abc|ccy=GBP" || true

step "Balances after the workload"
cb account list

step "Reversal: undo the fee"
fee_txn="$(cb --json account history --id "${fees}" --limit 1 | sed -E 's/.*"transaction_id":"([^"]+)".*/\1/')"
cb transaction reverse --id "${fee_txn}" --key "${k}-REV1" --reference "fee waived" | sed -n '1,3p'
printf -- '-- second reversal of the same transaction is rejected (exit 2)\n'
cb transaction reverse --id "${fee_txn}" --key "${k}-REV2" --reference "again" || true

step "Reconciliation"
"${COREBANK_HOME}/scripts/reconcile.sh"

if [[ "${remote}" != true ]]; then
  step "Stopping the engine before end-of-day"
  "${COREBANK_HOME}/scripts/stop.sh"
fi

step "End-of-day batch with an injected failure at RECONCILIATION"
COREBANK_FAIL_STAGE=RECONCILIATION "${COREBANK_HOME}/scripts/run_eod.sh" 2>/dev/null | tail -9 || true
step "End-of-day restarted: completed stages are skipped, the batch completes"
"${COREBANK_HOME}/scripts/run_eod.sh" | tail -9
step "End-of-day status"
cb eod status | sed -n '1,6p'

step "Audit trail (last 12 events)"
cb audit --limit 2000 | tail -12 | cut -c1-140

step "Batch output files"
ls -l "${COREBANK_BATCH_DIR}"/*/

printf '\nDemo complete. Synthetic data only; nothing here represents a real bank or customer.\n'
