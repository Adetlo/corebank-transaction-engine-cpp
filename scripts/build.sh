#!/usr/bin/env bash
# Configure and build the engine.
#
# Usage: scripts/build.sh [--debug|--release] [--asan] [--ubsan] [--tsan] [--coverage] [--no-tests]
# Env:   BUILD_DIR, BUILD_TYPE, CMAKE_GENERATOR (default: Ninja if available), CXX
# Exit:  0 built, 1 usage, 4 toolchain/build failure
SCRIPT_NAME=build
# shellcheck source=scripts/lib/common.sh
source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lib/common.sh"

cmake_args=()
build_tests=ON
while (($# > 0)); do
  case "$1" in
    --debug) BUILD_TYPE=Debug ;;
    --release) BUILD_TYPE=Release ;;
    --asan) cmake_args+=(-DCOREBANK_ENABLE_ASAN=ON) ;;
    --ubsan) cmake_args+=(-DCOREBANK_ENABLE_UBSAN=ON) ;;
    --tsan) cmake_args+=(-DCOREBANK_ENABLE_TSAN=ON) ;;
    --coverage) cmake_args+=(-DCOREBANK_ENABLE_COVERAGE=ON) ;;
    --no-tests) build_tests=OFF ;;
    -h | --help)
      sed -n '2,7p' "$0"
      exit "${EXIT_OK}"
      ;;
    *) die "${EXIT_USAGE}" "unknown option: $1" ;;
  esac
  shift
done

require_command cmake
generator="${CMAKE_GENERATOR:-}"
if [[ -z "${generator}" ]] && command -v ninja >/dev/null 2>&1; then
  generator=Ninja
fi
gen_args=()
[[ -n "${generator}" ]] && gen_args=(-G "${generator}")

log_info "configuring" "build_dir=${BUILD_DIR}" "build_type=${BUILD_TYPE}" "tests=${build_tests}"
cmake -S "${COREBANK_HOME}" -B "${BUILD_DIR}" ${gen_args[@]+"${gen_args[@]}"} \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" -DCOREBANK_BUILD_TESTS="${build_tests}" \
  ${cmake_args[@]+"${cmake_args[@]}"} \
  || die "${EXIT_SYSTEM}" "cmake configure failed"
log_info "building"
cmake --build "${BUILD_DIR}" --parallel || die "${EXIT_SYSTEM}" "build failed"
log_info "build complete" "binary=${BUILD_DIR}/corebank"
