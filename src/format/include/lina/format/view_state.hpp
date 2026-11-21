// SPDX-License-Identifier: GPL-3.0-or-later
//
// View state: what a user was looking at, kept in a file of its own so that
// scrolling and zooming never change the document
// (docs/design/16-project-file-format-encoding.md, "Project folder").
#ifndef LINA_FORMAT_VIEW_STATE_HPP
#define LINA_FORMAT_VIEW_STATE_HPP

#include "lina/format/error.hpp"
#include "lina/model/time.hpp"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace lina::format {

// The format version of the view state file, counted separately from the
// document's.
inline constexpr int viewFormatVersion = 1;

enum class MainView : std::uint8_t { Session, Arrangement };

struct ViewState {
  // Which view is showing.
  MainView view{MainView::Session};
  // Horizontal zoom of the arrangement: a positive factor, 1 being the default
  // width.
  double arrangementZoom{1.0};
  // The arrangement position at the left edge.
  model::TickPosition arrangementScroll;

  friend bool operator==(const ViewState &, const ViewState &) = default;
};

// Canonical text, in the same layout as the document. Fails for a zoom that is
// not a positive finite number and for a scroll position a document could not
// store.
std::expected<std::string, Error> writeViewState(const ViewState &state);

// Strict, like the document reader.
std::expected<ViewState, Error> readViewState(std::string_view bytes);

} // namespace lina::format

#endif // LINA_FORMAT_VIEW_STATE_HPP
