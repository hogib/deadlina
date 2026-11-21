# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_engine_testing: what tests of the engine tier share. A test instrument and a test
# effect of LINA's own (docs/design/14-build-test-and-ci.md, "Test tiers"), a builder for
# edits made by hand, and the headless test environment. Never linked into anything that ships.
lina_add_library(lina_engine_testing
  SOURCES
    edit_building.cpp
    projection_support.cpp
    test_environment.cpp
    test_plugins.cpp
  PUBLIC_LIBRARIES
    lina::engine_internals)

# JUCE's LV2 host unpacks its bundled LV2 specification files into the system temporary
# directory whenever an engine is created; tests point TMPDIR into this folder, and every test
# case keeps its engine's files in a folder of its own below it.
set(lina_engine_test_scratch "${CMAKE_BINARY_DIR}/tests/scratch")
file(MAKE_DIRECTORY "${lina_engine_test_scratch}/tmp")
set_target_properties(lina_engine_testing PROPERTIES
  LINA_TEST_SCRATCH_DIR "${lina_engine_test_scratch}")
target_compile_definitions(lina_engine_testing PRIVATE
  LINA_TEST_SCRATCH_DIR="${lina_engine_test_scratch}")
