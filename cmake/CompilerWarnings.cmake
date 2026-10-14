# Strict, portable warning set shared by every target in the project.
function(corebank_set_warnings target)
  set(_warnings
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    -Wsign-conversion
    -Wnon-virtual-dtor
    -Wold-style-cast
    -Wcast-align
    -Wunused
    -Woverloaded-virtual
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
    -Wimplicit-fallthrough
    # Designated initialisers deliberately rely on default member initialisers for the
    # fields they leave out (query and option structs); this warning would forbid that.
    -Wno-missing-field-initializers
  )
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # -Wuseless-cast is omitted: casts between int64_t and long long are needed on
    # platforms where the typedefs differ (macOS) and would be flagged on Linux.
    list(APPEND _warnings -Wduplicated-cond -Wduplicated-branches -Wlogical-op)
  endif()
  if(COREBANK_WARNINGS_AS_ERRORS)
    list(APPEND _warnings -Werror)
  endif()
  target_compile_options(${target} PRIVATE ${_warnings})
endfunction()

# Apple's libc++ ships std::jthread / std::stop_token behind an experimental flag.
# Linux (libstdc++ and libc++ 18+) needs nothing; this only eases local macOS builds.
function(corebank_platform_flags target)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(${target} PUBLIC -fexperimental-library)
    target_link_options(${target} PUBLIC -fexperimental-library)
  endif()
endfunction()
