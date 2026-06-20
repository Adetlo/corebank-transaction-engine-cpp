#!/usr/bin/env bash
# Start the engine in the background (TCP listener + worker pool) and wait until healthy.
#
# Usage: scripts/start.sh [--foreground]
# Env:   COREBANK_DB, COREBANK_PORT, COREBANK_LOG_DIR, COREBANK_PID_FILE, COREBANK_CONFIG,
#        COREBANK_WORKERS, COREBANK_LOG_LEVEL, START_TIMEOUT (seconds, default 15)
# Exit:  0 running, 1 already running, 4 failed to start
SCRIPT_NAME=start
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

foreground=false
[[ "${1:-}" == "--foreground" ]] && foreground=true
require_binary
ensure_dirs

if pid="$(engine_pid)"; then
  log_warn "engine already running" "pid=${pid}" "pid_file=${COREBANK_PID_FILE}"
  exit "${EXIT_USAGE}"
fi

config_args=()
[[ -n "${COREBANK_CONFIG:-}" ]] && config_args=(--config "${COREBANK_CONFIG}")
log_file="${COREBANK_LOG_DIR}/engine.log"
log_info "starting engine" "db=${COREBANK_DB}" "port=${COREBANK_PORT}" "log=${log_file}"

if [[ "${foreground}" == true ]]; then
  exec "${COREBANK_BIN}" ${config_args[@]+"${config_args[@]}"} --db "${COREBANK_DB}" serve --port "${COREBANK_PORT}"
fi

nohup "${COREBANK_BIN}" ${config_args[@]+"${config_args[@]}"} --db "${COREBANK_DB}" serve --port "${COREBANK_PORT}" \
  >>"${log_file}" 2>&1 &
pid=$!
printf '%s\n' "${pid}" >"${COREBANK_PID_FILE}"
chmod 640 "${COREBANK_PID_FILE}"

deadline=$((SECONDS + ${START_TIMEOUT:-15}))
until "${COREBANK_BIN}" health --host "${COREBANK_HOST}" --port "${COREBANK_PORT}" >/dev/null 2>&1; do
  if ! kill -0 "${pid}" 2>/dev/null; then
    rm -f "${COREBANK_PID_FILE}"
    die "${EXIT_SYSTEM}" "engine exited during start-up; see log" "log=${log_file}"
  fi
  if ((SECONDS >= deadline)); then
    kill "${pid}" 2>/dev/null || true
    rm -f "${COREBANK_PID_FILE}"
    die "${EXIT_SYSTEM}" "engine did not become healthy in time" "log=${log_file}"
  fi
  sleep 0.2
done
log_info "engine running" "pid=${pid}" "address=${COREBANK_HOST}:${COREBANK_PORT}"
