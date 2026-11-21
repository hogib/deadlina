# SPDX-License-Identifier: GPL-3.0-or-later
#
# Format tier (docs/design/14-build-test-and-ci.md): round trip, byte-stable save, migrations,
# golden files, damaged input and the project folder. Needs the model and the format library
# only. Files are written under the build directory's scratch folder and nowhere else.
set(lina_format_test_scratch "${CMAKE_BINARY_DIR}/tests/format-scratch")
file(MAKE_DIRECTORY "${lina_format_test_scratch}")

add_library(lina_format_test_support STATIC
  support/documents.cpp
  support/faulty_file_system.cpp
  support/reader_invariant.cpp
  support/scratch.cpp)
add_library(lina::format_test_support ALIAS lina_format_test_support)
lina_target_defaults(lina_format_test_support)
target_include_directories(lina_format_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lina_format_test_support
  PUBLIC lina::format_internals lina::model_test_support)
target_compile_definitions(lina_format_test_support PUBLIC
  LINA_FORMAT_SCRATCH_DIR="${lina_format_test_scratch}"
  LINA_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/tests/golden"
  LINA_FUZZ_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fuzz/corpus")

# The fuzz entry point is also compiled into the test executable, which replays the golden
# files through it, so it is built, linted and kept working in every configuration.
lina_add_catch_test(lina_format_tests
  SOURCES
    damaged_input_test.cpp
    document_io_test.cpp
    file_system_test.cpp
    fuzz/fuzz_read_document.cpp
    golden_test.cpp
    json_tree_test.cpp
    json_writer_test.cpp
    locale_test.cpp
    lock_test.cpp
    migration_test.cpp
    project_folder_test.cpp
    round_trip_test.cpp
    save_test.cpp
    sha256_test.cpp
    stores_test.cpp
    strict_reading_test.cpp
    view_state_test.cpp
  LIBRARIES lina::format_test_support)

# The fuzz target itself (docs/design/16-project-file-format-encoding.md, "Testing"): libFuzzer,
# Clang only, not part of the gating suite. Build it on top of the asan preset, in a build
# directory of its own, and run it from the repository root with the seed corpus last, so that
# what it finds goes into the scratch corpus and not into the repository:
#
#   cmake --preset asan -B build/fuzz -DLINA_ENABLE_ENGINE=OFF -DLINA_BUILD_FUZZERS=ON
#   cmake --build build/fuzz --target lina_format_fuzz
#   mkdir -p build/fuzz/corpus
#   build/fuzz/tests/format/lina_format_fuzz -max_total_time=60 -max_len=20000 \
#     -artifact_prefix=build/fuzz/ build/fuzz/corpus tests/format/fuzz/corpus
#
# tests/format/fuzz/corpus holds the seeds: the golden files and a few small inputs.
option(LINA_BUILD_FUZZERS "Build the libFuzzer targets (Clang only)" OFF)
if(LINA_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "LINA_BUILD_FUZZERS needs Clang.")
  endif()
  # Coverage feedback for the code under test, not only for the entry point.
  foreach(target lina_model lina_format lina_format_test_support)
    target_compile_options(${target} PRIVATE -fsanitize=fuzzer-no-link)
  endforeach()
  add_executable(lina_format_fuzz fuzz/fuzz_read_document.cpp)
  lina_target_defaults(lina_format_fuzz)
  target_compile_options(lina_format_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_options(lina_format_fuzz PRIVATE -fsanitize=fuzzer)
  target_link_libraries(lina_format_fuzz PRIVATE lina::format_test_support)
endif()
