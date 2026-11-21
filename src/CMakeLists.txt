# SPDX-License-Identifier: GPL-3.0-or-later
#
# LINA's own libraries (docs/design/13-architecture.md). Dependencies point one way:
# model <- format, model <- engine <- bridge <- ui <- app.
add_subdirectory(model)
add_subdirectory(format)
if(LINA_ENABLE_ENGINE)
  add_subdirectory(engine)
  add_subdirectory(bridge)
endif()
