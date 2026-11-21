# SPDX-License-Identifier: GPL-3.0-or-later
#
# lina_format: reading and writing the project document and the project folder
# (docs/design/10, 16). Depends on the model and a JSON parser: no Qt, JUCE or Tracktion include
# path reaches this target, and nlohmann/json stays out of its public headers.
include(LinaJson)

lina_add_library(lina_format
  SOURCES
    decode.cpp
    encode.cpp
    error.cpp
    file_system.cpp
    guard.cpp
    json_tree.cpp
    json_writer.cpp
    migration.cpp
    project_folder.cpp
    sha256.cpp
    strict_reader.cpp
    text_rules.cpp
    view_state.cpp
  PUBLIC_LIBRARIES lina::model
  PRIVATE_LIBRARIES nlohmann_json::nlohmann_json)
target_include_directories(lina_format PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

# For the format tier's own tests and its fuzz target only: the parsed tree, the canonical
# writer and the migration chain, none of which are part of the library's public interface.
add_library(lina_format_internals INTERFACE)
add_library(lina::format_internals ALIAS lina_format_internals)
target_include_directories(lina_format_internals INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(lina_format_internals INTERFACE lina::format)
