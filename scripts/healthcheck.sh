#!/usr/bin/env bash
# Health check for monitoring, systemd ExecStartPost, or Docker HEALTHCHECK.
#
# Usage: scripts/healthcheck.sh [--db]      (TCP PING by default, --db checks the database)
# Env:   COREBANK_HOST, COREBANK_PORT, COREBANK_DB, COREBANK_BIN
# Exit:  0 healthy, 4 unhealthy
SCRIPT_NAME=healthcheck
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

require_binary
if [[ "${1:-}" == "--db" ]]; then
  if output="$("${COREBANK_BIN}" --db "${COREBANK_DB}" health 2>&1)"; then
    log_info "database healthy" "db=${COREBANK_DB}"
    exit "${EXIT_OK}"
  fi
  die "${EXIT_SYSTEM}" "database unhealthy" "detail=\"${output}\""
fi
if output="$("${COREBANK_BIN}" health --host "${COREBANK_HOST}" --port "${COREBANK_PORT}" 2>&1)"; then
  log_info "engine healthy" "address=${COREBANK_HOST}:${COREBANK_PORT}"
  exit "${EXIT_OK}"
fi
die "${EXIT_SYSTEM}" "engine unhealthy" "address=${COREBANK_HOST}:${COREBANK_PORT}" "detail=\"${output}\""
