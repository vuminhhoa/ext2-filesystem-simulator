cmake_minimum_required(VERSION 3.25)
project(ext2sim VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(EXT2SIM_WERROR "Treat compiler warnings as errors" ON)
option(EXT2SIM_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(EXT2SIM_HARDEN "Add the OpenSSF hardening flags the toolchain supports" ON)
option(EXT2SIM_COVERAGE "Instrument for code coverage" OFF)
option(EXT2SIM_LIBFUZZER "Build fuzz_ext2 as a libFuzzer target (Clang only)" OFF)

include(CheckCCompilerFlag)
include(CheckLinkerFlag)
include(CheckPIESupported)

add_library(ext2sim_options INTERFACE)

if(MSVC)
  target_compile_options(ext2sim_options INTERFACE /W4 $<$<BOOL:${EXT2SIM_WERROR}>:/WX>)
else()
  target_compile_options(ext2sim_options INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes -Wmissing-prototypes
    -Wconversion -Wsign-conversion -Wformat=2 -Wcast-qual -Wvla
    $<$<BOOL:${EXT2SIM_WERROR}>:-Werror>)
endif()

# Hardening, following the OpenSSF Compiler Options Hardening Guide for C and C++.
if(EXT2SIM_HARDEN AND NOT MSVC)
  check_pie_supported()
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)

  # With -Werror, a flag the compiler accepts but ignores on this target (and warns about) is left out.
  set(CMAKE_REQUIRED_FLAGS -Werror)
  foreach(flag -fstack-protector-strong -fstack-clash-protection -ftrivial-auto-var-init=zero
               -fcf-protection=full -mbranch-protection=standard -fstrict-flex-arrays=3)
    string(MAKE_C_IDENTIFIER "HAVE${flag}" have_flag)
    check_c_compiler_flag(${flag} ${have_flag})
    if(${have_flag})
      target_compile_options(ext2sim_options INTERFACE ${flag})
    endif()
  endforeach()

  # _FORTIFY_SOURCE needs optimization and does not mix with the sanitizers.
  if(NOT EXT2SIM_SANITIZE AND NOT EXT2SIM_LIBFUZZER)
    target_compile_options(ext2sim_options INTERFACE
      $<$<NOT:$<CONFIG:Debug>>:-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3>)
  endif()

  foreach(flag -Wl,-z,relro -Wl,-z,now -Wl,-z,noexecstack)
    string(MAKE_C_IDENTIFIER "HAVE${flag}" have_flag)
    check_linker_flag(C ${flag} ${have_flag})
    if(${have_flag})
      target_link_options(ext2sim_options INTERFACE ${flag})
    endif()
  endforeach()
  unset(CMAKE_REQUIRED_FLAGS)
endif()

if(EXT2SIM_SANITIZE OR EXT2SIM_LIBFUZZER)
  target_compile_options(ext2sim_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer
    -fno-sanitize-recover=all)
  target_link_options(ext2sim_options INTERFACE -fsanitize=address,undefined)
endif()

if(EXT2SIM_LIBFUZZER)
  if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "EXT2SIM_LIBFUZZER needs Clang (configure with CC=clang)")
  endif()
  target_compile_options(ext2sim_options INTERFACE -fsanitize=fuzzer-no-link)
endif()

if(EXT2SIM_COVERAGE)
  if(CMAKE_C_COMPILER_ID MATCHES "Clang")
    target_compile_options(ext2sim_options INTERFACE -fprofile-instr-generate -fcoverage-mapping)
    target_link_options(ext2sim_options INTERFACE -fprofile-instr-generate)
  else()
    target_compile_options(ext2sim_options INTERFACE --coverage)
    target_link_options(ext2sim_options INTERFACE --coverage)
  endif()
endif()

# The file system and the simulated disk, shared by the shell and the tests.
add_library(ext2 STATIC
  src/disk/disksim.c
  src/ext2/alloc.c src/ext2/block.c src/ext2/dir.c src/ext2/file.c
  src/ext2/format.c src/ext2/inode.c src/ext2/name.c src/ext2/node.c)
target_include_directories(ext2 PUBLIC src)
target_link_libraries(ext2 PRIVATE ext2sim_options)

add_executable(ext2-shell src/shell/shell.c src/shell/ext2_shell.c src/shell/entrylist.c)
target_link_libraries(ext2-shell PRIVATE ext2 ext2sim_options)

include(CTest)
if(BUILD_TESTING)
  add_library(ext2_fsck STATIC tests/fsck.c)
  target_link_libraries(ext2_fsck PUBLIC ext2 PRIVATE ext2sim_options)

  add_executable(test_ext2 tests/test_ext2.c)
  target_link_libraries(test_ext2 PRIVATE ext2_fsck ext2sim_options)
  add_test(NAME ext2 COMMAND test_ext2)

  add_executable(test_faults tests/test_faults.c)
  target_link_libraries(test_faults PRIVATE ext2 ext2sim_options)
  add_test(NAME disk_faults COMMAND test_faults)

  add_test(NAME shell_session
    COMMAND ${CMAKE_COMMAND} -DSHELL=$<TARGET_FILE:ext2-shell>
      -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/tests/shell_session.txt
      -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/shell_session.expected
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_shell_session.cmake)

  add_executable(fuzz_ext2 tests/fuzz_ext2.c)
  target_link_libraries(fuzz_ext2 PRIVATE ext2_fsck ext2sim_options)
  if(EXT2SIM_LIBFUZZER)
    target_compile_definitions(fuzz_ext2 PRIVATE EXT2SIM_LIBFUZZER)
    target_link_options(fuzz_ext2 PRIVATE -fsanitize=fuzzer)
  else()
    # Without libFuzzer, replay the checked-in corpus and a batch of random inputs on every test run.
    file(GLOB corpus CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/corpus/*)
    add_test(NAME fuzz_corpus COMMAND fuzz_ext2 ${corpus})
    add_test(NAME fuzz_random COMMAND fuzz_ext2 --iterations 200)
  endif()
endif()
