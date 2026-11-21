# SPDX-License-Identifier: GPL-3.0-or-later
#
# Model tier (docs/design/14-build-test-and-ci.md): needs nothing but the model library.

# Builders, a document differ and the seeded command generator, for this tier and for the
# tiers above it that drive the model with generated edits.
add_library(lina_model_test_support STATIC
  support/builders.cpp
  support/diff.cpp
  support/generator.cpp)
add_library(lina::model_test_support ALIAS lina_model_test_support)
lina_target_defaults(lina_model_test_support)
target_include_directories(lina_model_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lina_model_test_support PUBLIC lina::model)

lina_add_catch_test(lina_model_tests
  SOURCES
    automation_command_test.cpp
    change_set_test.cpp
    clip_command_test.cpp
    device_command_test.cpp
    document_test.cpp
    ids_test.cpp
    note_command_test.cpp
    project_command_test.cpp
    property_test.cpp
    storable_rules_test.cpp
    time_test.cpp
    track_command_test.cpp
    validate_test.cpp
  LIBRARIES lina::model lina::model_test_support)
