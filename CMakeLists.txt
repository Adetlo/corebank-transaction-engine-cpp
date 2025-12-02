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
