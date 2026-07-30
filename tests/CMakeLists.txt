include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_library(corebank_test_support STATIC support/test_support.cpp)
target_include_directories(corebank_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(corebank_test_support PUBLIC corebank::core GTest::gtest)
corebank_platform_flags(corebank_test_support)

function(corebank_add_test name category)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE corebank_test_support GTest::gtest_main)
  corebank_set_warnings(${name})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES LABELS "${category}" TIMEOUT 300)
endfunction()

# Unit
corebank_add_test(money_test unit unit/money_test.cpp)
corebank_add_test(currency_test unit unit/currency_test.cpp)
corebank_add_test(identifiers_test unit unit/identifiers_test.cpp)
corebank_add_test(result_test unit unit/result_test.cpp)
corebank_add_test(time_test unit unit/time_test.cpp)
corebank_add_test(state_machine_test unit unit/state_machine_test.cpp)
corebank_add_test(account_test unit unit/account_test.cpp)
corebank_add_test(config_test unit unit/config_test.cpp)
corebank_add_test(logger_test unit unit/logger_test.cpp)
corebank_add_test(json_writer_test unit unit/json_writer_test.cpp)
corebank_add_test(protocol_test unit unit/protocol_test.cpp)
corebank_add_test(bounded_queue_test unit unit/bounded_queue_test.cpp)
corebank_add_test(lock_manager_test unit unit/lock_manager_test.cpp)
corebank_add_test(args_test unit unit/args_test.cpp)
target_link_libraries(args_test PRIVATE corebank::cli)
