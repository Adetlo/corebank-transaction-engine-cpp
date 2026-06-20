#!/usr/bin/env bash
# Stop the engine gracefully (SIGTERM, then SIGKILL after a grace period).
#
# Usage: scripts/stop.sh
# Env:   COREBANK_PID_FILE, STOP_TIMEOUT (seconds, default 20)
# Exit:  0 stopped or not running, 4 had to be killed
SCRIPT_NAME=stop
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

if ! pid="$(engine_pid)"; then
  log_info "engine not running" "pid_file=${COREBANK_PID_FILE}"
  rm -f "${COREBANK_PID_FILE}"
  exit "${EXIT_OK}"
fi
log_info "sending SIGTERM" "pid=${pid}"
kill -TERM "${pid}"
deadline=$((SECONDS + ${STOP_TIMEOUT:-20}))
while kill -0 "${pid}" 2>/dev/null; do
  if ((SECONDS >= deadline)); then
    log_error "engine ignored SIGTERM; sending SIGKILL" "pid=${pid}"
    kill -KILL "${pid}" 2>/dev/null || true
    rm -f "${COREBANK_PID_FILE}"
    exit "${EXIT_SYSTEM}"
  fi
  sleep 0.2
done
rm -f "${COREBANK_PID_FILE}"
log_info "engine stopped gracefully" "pid=${pid}"
