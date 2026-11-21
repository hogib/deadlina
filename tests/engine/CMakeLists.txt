# SPDX-License-Identifier: GPL-3.0-or-later

add_subdirectory(support)
get_target_property(lina_engine_test_scratch lina_engine_testing LINA_TEST_SCRATCH_DIR)

# lina_add_engine_test(<name> <source>...)
#
# A Catch2 test of the engine tier. It runs headless: no display, temporary files inside the
# build directory, and it fails when JUCE reports a failed assertion or a leaked object.
function(lina_add_engine_test name)
  lina_add_catch_test(${name}
    SOURCES ${ARGN}
    LIBRARIES lina::engine_testing
    PROPERTIES
      # JUCE's leak detector reports at static destruction, after the test body has returned.
      FAIL_REGULAR_EXPRESSION "JUCE Assertion failure|Leaked objects detected"
      ENVIRONMENT "TMPDIR=${lina_engine_test_scratch}/tmp"
      # No X server: JUCE must come up without a display, as it does in CI.
      ENVIRONMENT_MODIFICATION "DISPLAY=unset:")
endfunction()

lina_add_engine_test(lina_engine_smoke_test engine_smoke_test.cpp)
target_compile_definitions(lina_engine_smoke_test PRIVATE
  LINA_TEST_SCRATCH_DIR="${lina_engine_test_scratch}")

lina_add_engine_test(lina_engine_playback_test engine_playback_test.cpp)
lina_add_engine_test(lina_engine_render_test engine_render_test.cpp)

# What the projection computes before it touches the engine: plain functions of the document,
# tested without an engine.
lina_add_catch_test(lina_engine_unit_tests
  SOURCES
    audio_parts_test.cpp
    midi_events_test.cpp
    mixing_test.cpp
    note_guard_test.cpp
    time_map_test.cpp
  LIBRARIES lina::engine_internals lina::model_test_support)

# Projection and audio tiers (docs/design/14-build-test-and-ci.md): documents built through
# commands and played through the projection.
function(lina_add_projection_test name)
  lina_add_engine_test(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE lina::model_test_support)
endfunction()

lina_add_projection_test(lina_projection_test projection_test.cpp)
lina_add_projection_test(lina_projection_midi_test projection_midi_test.cpp)
lina_add_projection_test(lina_projection_audio_test projection_audio_test.cpp)
lina_add_projection_test(lina_projection_mixer_test projection_mixer_test.cpp)
lina_add_projection_test(lina_projection_live_test projection_live_test.cpp)

# The oracle: an edit kept in step with generated commands against one built from scratch.
# LINA_PROJECTION_SOAK=<n> in the environment runs n times as many sequences.
lina_add_projection_test(lina_projection_oracle_test projection_oracle_test.cpp)

# Compares a render with the reference file checked in under tests/audio-refs. The test's own
# header says how the file is written anew.
lina_add_projection_test(lina_projection_reference_test projection_reference_test.cpp)
target_compile_definitions(lina_projection_reference_test PRIVATE
  LINA_AUDIO_REFS_DIR="${PROJECT_SOURCE_DIR}/tests/audio-refs")
# A FLAC file for the test of formats that are not played.
target_compile_definitions(lina_projection_audio_test PRIVATE
  LINA_AUDIO_REFS_DIR="${PROJECT_SOURCE_DIR}/tests/audio-refs")

# A bench, not a test: it reads the clock and is not registered with CTest
# (docs/design/14-build-test-and-ci.md, "Benches"). Its header says how to run it.
add_executable(lina_projection_bench projection_bench.cpp)
lina_target_defaults(lina_projection_bench)
target_link_libraries(lina_projection_bench PRIVATE
  Catch2::Catch2WithMain lina::engine_testing lina::model_test_support)

# From a project file to sound: the one test that links the file format as well.
lina_add_projection_test(lina_project_file_render_test project_file_render_test.cpp)
target_link_libraries(lina_project_file_render_test PRIVATE lina::format)
