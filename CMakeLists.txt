cmake_minimum_required(VERSION 3.22)

project(corebank_transaction_engine
  VERSION 1.0.0
  DESCRIPTION "Production-style educational C++ core-banking transaction engine"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(COREBANK_BUILD_TESTS "Build the test suite" ON)
option(COREBANK_BUILD_BENCH "Build the benchmark executable" ON)
option(COREBANK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(COREBANK_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(COREBANK_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(COREBANK_ENABLE_TSAN "Enable ThreadSanitizer" OFF)
option(COREBANK_ENABLE_COVERAGE "Enable gcov/lcov coverage instrumentation" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
# CMake < 3.31 only provides the older SQLite::SQLite3 target name.
if(TARGET SQLite3::SQLite3)
  set(COREBANK_SQLITE_TARGET SQLite3::SQLite3)
else()
  set(COREBANK_SQLITE_TARGET SQLite::SQLite3)
endif()

# ---------------------------------------------------------------------------
# Core library: domain + application + infrastructure + messaging + batch
# ---------------------------------------------------------------------------
add_library(corebank_core STATIC
  src/domain/error.cpp
  src/domain/currency.cpp
  src/domain/money.cpp
  src/domain/identifiers.cpp
  src/domain/time.cpp
  src/domain/account.cpp
  src/domain/transaction.cpp
  src/domain/posting.cpp
  src/domain/audit.cpp
  src/domain/batch.cpp
  src/domain/reconciliation.cpp
  src/application/id_generator.cpp
  src/application/account_lock_manager.cpp
  src/application/idempotency_gate.cpp
  src/application/transaction_service.cpp
  src/application/reconciliation_service.cpp
  src/application/transaction_processor.cpp
  src/batch/eod_service.cpp
  src/infrastructure/config.cpp
  src/infrastructure/logger.cpp
  src/infrastructure/json_writer.cpp
  src/infrastructure/signal_handler.cpp
  src/infrastructure/errno_text.cpp
  src/infrastructure/sqlite/connection.cpp
  src/infrastructure/sqlite/statement.cpp
  src/infrastructure/sqlite/schema.cpp
  src/infrastructure/sqlite/sqlite_repository.cpp
  src/messaging/protocol.cpp
  src/messaging/message_handler.cpp
  src/messaging/tcp_server.cpp
  src/messaging/tcp_client.cpp
)
add_library(corebank::core ALIAS corebank_core)
target_include_directories(corebank_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(corebank_core PUBLIC ${COREBANK_SQLITE_TARGET} Threads::Threads)
target_compile_definitions(corebank_core PUBLIC COREBANK_VERSION="${PROJECT_VERSION}")
corebank_set_warnings(corebank_core)
corebank_platform_flags(corebank_core)
corebank_enable_sanitizers(corebank_core)

# ---------------------------------------------------------------------------
# CLI executable
# ---------------------------------------------------------------------------
add_library(corebank_cli STATIC
  src/cli/args.cpp
  src/cli/output.cpp
  src/cli/app.cpp
  src/cli/commands_account.cpp
  src/cli/commands_transaction.cpp
  src/cli/commands_ops.cpp
  src/cli/commands_serve.cpp
)
add_library(corebank::cli ALIAS corebank_cli)
target_link_libraries(corebank_cli PUBLIC corebank::core)
corebank_set_warnings(corebank_cli)
corebank_enable_sanitizers(corebank_cli)

add_executable(corebank src/cli/main.cpp)
target_link_libraries(corebank PRIVATE corebank::cli)
corebank_set_warnings(corebank)
corebank_enable_sanitizers(corebank)

# ---------------------------------------------------------------------------
# Benchmark executable
# ---------------------------------------------------------------------------
if(COREBANK_BUILD_BENCH)
  add_executable(corebank-bench src/bench/main.cpp)
  target_link_libraries(corebank-bench PRIVATE corebank::core)
  corebank_set_warnings(corebank-bench)
  corebank_enable_sanitizers(corebank-bench)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(COREBANK_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS corebank RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(COREBANK_BUILD_BENCH)
  install(TARGETS corebank-bench RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(FILES config/default.yaml DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/corebank)
