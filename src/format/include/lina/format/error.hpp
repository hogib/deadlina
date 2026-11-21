// SPDX-License-Identifier: GPL-3.0-or-later
//
// How the format library reports that something could not be read or written:
// as a value, never as an exception
// (docs/design/16-project-file-format-encoding.md).
#ifndef LINA_FORMAT_ERROR_HPP
#define LINA_FORMAT_ERROR_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace lina::format {

enum class ErrorCode : std::uint8_t {
  // The bytes are not one well-formed JSON text in UTF-8.
  Syntax,
  // A key is unknown or missing, or a value has the wrong JSON type.
  Structure,
  // A value has the right type but is not allowed: out of range, not a known
  // name, an identity used twice, a path or name of the wrong shape.
  Value,
  // The format version is newer than this build reads, or was never written.
  Version,
  // The document is well-formed but breaks a rule of the model.
  Rule,
  // A document or file is larger than the library accepts.
  TooLarge,
  // A file or folder that was asked for is not there.
  NotFound,
  // A file that must be created is already there.
  AlreadyExists,
  // Something is there but is not a regular file where one is required: a
  // symbolic link, a folder, a device, a pipe.
  NotAFile,
  // The folder cannot be written to: a read-only file system, or no
  // permission. The project can still be opened to look at.
  ReadOnly,
  // The file system refused an operation for any other reason.
  Io,
  // A stored file does not hold what its name promises.
  Corrupt,
  // Something went wrong inside the library itself, running out of memory
  // included.
  Internal,
};

// A stable, lower-case name for an error code, for messages and logs.
std::string_view errorCodeName(ErrorCode code);

// One failure. `where` names the place: a JSON pointer into the document
// ("/tracks/0/name"; empty for the whole document), a line and column for
// malformed text, an object of the model, or a path in the file system.
struct Error {
  ErrorCode code{ErrorCode::Io};
  std::string where;
  std::string message;

  friend bool operator==(const Error &, const Error &) = default;
};

// "<where>: <message>", or the message alone when there is no place.
std::string describe(const Error &error);

} // namespace lina::format

#endif // LINA_FORMAT_ERROR_HPP
