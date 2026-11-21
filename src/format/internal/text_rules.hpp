// SPDX-License-Identifier: GPL-3.0-or-later
//
// Private to lina_format: rules on text that the writer and the reader both
// enforce, so that whatever is written is read back.
#ifndef LINA_FORMAT_INTERNAL_TEXT_RULES_HPP
#define LINA_FORMAT_INTERNAL_TEXT_RULES_HPP

#include <string_view>

namespace lina::format::detail {

// True for well-formed UTF-8: no overlong forms, no surrogates, nothing past
// U+10FFFF.
bool isValidUtf8(std::string_view text);

// True for 64 lower-case hexadecimal digits: a SHA-256 as the format spells
// it.
bool isContentHash(std::string_view text);

// The two shapes a media path may have
// (docs/design/16-project-file-format-encoding.md, "Media"), told apart by
// the first character:
//   - inside the project folder: relative and normalised, that is segments
//     separated by single slashes, none of them empty, "." or "..";
//   - a reference to a file outside: absolute, starting with a slash.
// Neither may be empty or hold a null character.
bool isProjectMediaPath(std::string_view path);
bool isExternalMediaPath(std::string_view path);
inline bool isAllowedMediaPath(std::string_view path) {
  return isProjectMediaPath(path) || isExternalMediaPath(path);
}

} // namespace lina::format::detail

#endif // LINA_FORMAT_INTERNAL_TEXT_RULES_HPP
