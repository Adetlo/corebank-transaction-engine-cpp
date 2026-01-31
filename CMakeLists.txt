cmake_minimum_required(VERSION 3.22)

project(corebank_transaction_engine
  VERSION 0.1.0
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

option(COREBANK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

find_package(SQLite3 REQUIRED)
find_package(Threads REQUIRED)
# CMake < 3.31 only provides the older SQLite::SQLite3 target name.
if(TARGET SQLite3::SQLite3)
  set(COREBANK_SQLITE_TARGET SQLite3::SQLite3)
else()
  set(COREBANK_SQLITE_TARGET SQLite::SQLite3)
endif()

# ---------------------------------------------------------------------------
# Core library: domain + application + infrastructure
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
  src/application/id_generator.cpp
  src/application/transaction_service.cpp
  src/infrastructure/sqlite/connection.cpp
  src/infrastructure/sqlite/statement.cpp
  src/infrastructure/sqlite/schema.cpp
  src/infrastructure/sqlite/sqlite_repository.cpp
)
add_library(corebank::core ALIAS corebank_core)
target_include_directories(corebank_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(corebank_core PUBLIC ${COREBANK_SQLITE_TARGET} Threads::Threads)
target_compile_definitions(corebank_core PUBLIC COREBANK_VERSION="${PROJECT_VERSION}")
corebank_set_warnings(corebank_core)
corebank_platform_flags(corebank_core)
