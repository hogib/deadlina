// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the engine could not play of a project as the document describes it.
// Nothing here is an error: the project plays on, and the interface can show
// what is listed.
#ifndef LINA_ENGINE_PROJECT_REPORT_HPP
#define LINA_ENGINE_PROJECT_REPORT_HPP

#include "lina/model/ids.hpp"

#include <cstdint>

namespace lina::engine {

enum class MediaProblem : std::uint8_t {
  /// There is no file at the media reference's path. Its clips are silent.
  Missing,
  /// The file is there but cannot be read as audio. Its clips are silent.
  Unreadable,
  /// The file is audio in a format the engine cannot map into memory
  /// (anything but uncompressed WAV and AIFF). Playing it would depend on a
  /// background reader keeping up, so its clips are silent until files are
  /// converted on import.
  UnsupportedFormat,
  /// The file's sample rate is not the one the media reference states. The
  /// clips play, with the rate found in the file.
  SampleRateDiffers,
};

/// One media reference used by an arrangement clip, and what is wrong with
/// its file.
struct MediaReport {
  model::MediaId media;
  MediaProblem problem{MediaProblem::Missing};
  /// The sample rate found in the file; 0 when the file could not be read.
  double fileSampleRate{0.0};

  friend bool operator==(const MediaReport &, const MediaReport &) = default;
};

} // namespace lina::engine

#endif // LINA_ENGINE_PROJECT_REPORT_HPP
