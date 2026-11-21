# SPDX-License-Identifier: GPL-3.0-or-later
#
# nlohmann/json: the system package. Used by lina_format for parsing only
# (docs/design/16-project-file-format-encoding.md); writing is LINA's own canonical writer.
#
# 3.12 at least: lina_format converts numbers itself from the literal that the parser's SAX
# interface hands over, and relies on that literal being the text of the file whatever the
# system's locale. Older releases are believed to hand over a locale-formatted one.

find_package(nlohmann_json 3.12 REQUIRED)
message(STATUS "nlohmann/json ${nlohmann_json_VERSION}: system package")
