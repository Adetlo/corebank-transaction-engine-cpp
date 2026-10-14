#!/usr/bin/env bash
# Run (or resume) the end-of-day batch for a business date and record the outcome.
#
# Usage: scripts/run_eod.sh [YYYY-MM-DD]        (default: today, UTC)
# Env:   COREBANK_DB, COREBANK_BATCH_DIR, COREBANK_LOG_DIR, COREBANK_FAIL_STAGE (testing only)
# Exit:  0 completed (or already complete), 3 batch failed (see `corebank eod status`), 4 system error
# Logs:  $COREBANK_LOG_DIR/eod-<date>.log
SCRIPT_NAME=run_eod
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

require_binary
ensure_dirs
date_arg="${1:-$(date -u +%Y-%m-%d)}"
[[ "${date_arg}" =~ ^[0-9]{4}-[0-9]{2}-[0-9]{2}$ ]] || die "${EXIT_USAGE}" "business date must be YYYY-MM-DD" "given=${date_arg}"
eod_log="${COREBANK_LOG_DIR}/eod-${date_arg}.log"
log_info "starting end-of-day" "business_date=${date_arg}" "db=${COREBANK_DB}" "log=${eod_log}"
[[ -n "${COREBANK_FAIL_STAGE:-}" ]] && log_warn "failure injection active" "stage=${COREBANK_FAIL_STAGE}"

rc=0
"${COREBANK_BIN}" --db "${COREBANK_DB}" eod run --date "${date_arg}" 2>&1 | tee -a "${eod_log}" || rc=${PIPESTATUS[0]}
case "${rc}" in
  0) log_info "end-of-day finished" "business_date=${date_arg}" ;;
  3) log_error "end-of-day failed; fix the cause and re-run to resume" "business_date=${date_arg}" "log=${eod_log}" ;;
  *) log_error "end-of-day aborted" "exit=${rc}" "reason=\"$(describe_exit "${rc}")\"" ;;
esac
exit "${rc}"
