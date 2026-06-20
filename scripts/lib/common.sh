#!/usr/bin/env bash
# Shared helpers for every operational script. Source it, do not execute it.
#
# Environment (all optional, documented in docs/SHELL_AUTOMATION.md):
#   COREBANK_HOME      repository/installation root   (default: parent of scripts/)
#   BUILD_DIR          CMake build directory           (default: $COREBANK_HOME/build)
#   BUILD_TYPE         CMake build type                (default: Release)
#   COREBANK_BIN       path to the corebank binary     (default: $BUILD_DIR/corebank)
#   COREBANK_VAR       runtime state directory         (default: $COREBANK_HOME/var)
#   COREBANK_DB        SQLite database path            (default: $COREBANK_VAR/corebank.db)
#   COREBANK_BATCH_DIR batch output directory          (default: $COREBANK_VAR/batch)
#   COREBANK_LOG_DIR   log directory                   (default: $COREBANK_VAR/log)
#   COREBANK_PID_FILE  engine pid file                 (default: $COREBANK_VAR/run/corebank.pid)
#   COREBANK_HOST      engine host for TCP checks      (default: 127.0.0.1)
#   COREBANK_PORT      engine TCP port                 (default: 7400)
#
# Exit codes used by the scripts:
#   0 success, 1 usage/precondition error, 2 business rejection, 3 control failure
#   (reconciliation/EOD), 4 system error (database, network, missing binary).

if [[ -n "${_COREBANK_COMMON_LOADED:-}" ]]; then
  return 0
fi
_COREBANK_COMMON_LOADED=1

set -euo pipefail

_common_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COREBANK_HOME="${COREBANK_HOME:-$(cd "${_common_dir}/../.." && pwd)}"
BUILD_DIR="${BUILD_DIR:-${COREBANK_HOME}/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"
COREBANK_BIN="${COREBANK_BIN:-${BUILD_DIR}/corebank}"
COREBANK_VAR="${COREBANK_VAR:-${COREBANK_HOME}/var}"
COREBANK_DB="${COREBANK_DB:-${COREBANK_VAR}/corebank.db}"
COREBANK_BATCH_DIR="${COREBANK_BATCH_DIR:-${COREBANK_VAR}/batch}"
COREBANK_LOG_DIR="${COREBANK_LOG_DIR:-${COREBANK_VAR}/log}"
COREBANK_PID_FILE="${COREBANK_PID_FILE:-${COREBANK_VAR}/run/corebank.pid}"
COREBANK_HOST="${COREBANK_HOST:-127.0.0.1}"
COREBANK_PORT="${COREBANK_PORT:-7400}"
export COREBANK_DB COREBANK_BATCH_DIR COREBANK_PORT

# Exit codes used by the scripts that source this file.
# shellcheck disable=SC2034
readonly EXIT_OK=0
# shellcheck disable=SC2034
readonly EXIT_USAGE=1
# shellcheck disable=SC2034
readonly EXIT_REJECTED=2
# shellcheck disable=SC2034
readonly EXIT_CONTROL=3
readonly EXIT_SYSTEM=4

# Structured log line to stderr: ts=... level=... script=... msg="..." extra...
log() {
  local level="$1"
  shift
  local msg="$1"
  shift
  printf 'ts=%s level=%s script=%s msg="%s"' \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "${level}" "${SCRIPT_NAME:-$(basename "$0")}" "${msg}" >&2
  if (($# > 0)); then
    printf ' %s' "$@" >&2
  fi
  printf '\n' >&2
}
log_info() { log INFO "$@"; }
log_warn() { log WARN "$@"; }
log_error() { log ERROR "$@"; }

die() {
  local code="$1"
  shift
  log_error "$@"
  exit "${code}"
}

require_command() {
  local cmd
  for cmd in "$@"; do
    command -v "${cmd}" >/dev/null 2>&1 || die "${EXIT_SYSTEM}" "required command not found: ${cmd}"
  done
}

require_binary() {
  [[ -x "${COREBANK_BIN}" ]] || die "${EXIT_SYSTEM}" "corebank binary not found; run scripts/build.sh first" "path=${COREBANK_BIN}"
}

ensure_dirs() {
  mkdir -p "${COREBANK_VAR}" "${COREBANK_BATCH_DIR}" "${COREBANK_LOG_DIR}" "$(dirname "${COREBANK_PID_FILE}")"
  chmod 750 "${COREBANK_VAR}" "${COREBANK_BATCH_DIR}" "${COREBANK_LOG_DIR}" 2>/dev/null || true
}

# Prints the engine pid if the pid file points at a live process, else returns 1.
engine_pid() {
  local pid
  [[ -f "${COREBANK_PID_FILE}" ]] || return 1
  pid="$(cat "${COREBANK_PID_FILE}" 2>/dev/null || true)"
  [[ -n "${pid}" ]] || return 1
  if kill -0 "${pid}" 2>/dev/null; then
    printf '%s\n' "${pid}"
    return 0
  fi
  return 1
}

# Portable timeout: run "$@" with a deadline in seconds (coreutils timeout is not on macOS).
with_timeout() {
  local seconds="$1"
  shift
  "$@" &
  local pid=$!
  (
    sleep "${seconds}"
    kill "${pid}" 2>/dev/null || true
  ) &
  local watchdog=$!
  local rc=0
  wait "${pid}" || rc=$?
  kill "${watchdog}" 2>/dev/null || true
  wait "${watchdog}" 2>/dev/null || true
  return "${rc}"
}

# Converts a corebank exit code into a log level for operator-facing messages.
describe_exit() {
  case "$1" in
    0) printf 'ok' ;;
    1) printf 'usage error' ;;
    2) printf 'business rejection' ;;
    3) printf 'control failure' ;;
    4) printf 'system error' ;;
    *) printf 'unexpected exit %s' "$1" ;;
  esac
}
