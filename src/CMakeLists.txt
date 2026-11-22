# SPDX-License-Identifier: GPL-3.0-or-later
#
# LINA's own libraries (docs/design/13-architecture.md). Dependencies point one way:
# model <- format, model <- engine <- bridge <- ui <- app.
add_subdirectory(model)
add_subdirectory(format)
if(LINA_ENABLE_ENGINE)
  add_subdirectory(engine)
  add_subdirectory(bridge)
  add_subdirectory(ui)
  # The QML of the interface lives at the top of the tree (docs/design/14-build-test-and-ci.md,
  # "Repository layout").
  add_subdirectory("${PROJECT_SOURCE_DIR}/qml" "${PROJECT_BINARY_DIR}/qml-shell")
  add_subdirectory(app)
endif()
