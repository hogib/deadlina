// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/format/view_state.hpp"

#include "internal/codec.hpp"
#include "internal/guard.hpp"
#include "internal/json_tree.hpp"
#include "internal/json_writer.hpp"
#include "internal/strict_reader.hpp"
#include "lina/format/document_io.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <utility>

namespace lina::format {

namespace {

using detail::EnumName;
using detail::member;
using detail::Node;

constexpr std::array mainViewNames{
    EnumName{.value = MainView::Session, .name = "session"},
    EnumName{.value = MainView::Arrangement, .name = "arrangement"},
};

bool zoomAllowed(double zoom) { return std::isfinite(zoom) && zoom > 0.0; }

std::expected<std::string, Error> write(const ViewState &state) {
  if (!zoomAllowed(state.arrangementZoom)) {
    return std::unexpected(Error{
        .code = ErrorCode::Value,
        .where = "/arrangement_zoom",
        .message = "the zoom must be a positive number",
    });
  }
  if (!model::isStorable(state.arrangementScroll)) {
    return std::unexpected(Error{
        .code = ErrorCode::Value,
        .where = "/arrangement_scroll",
        .message = "the scroll position is not a storable position",
    });
  }
  detail::JsonWriter out;
  out.beginObject(detail::Layout::Block);
  out.key("format");
  out.signedInteger(viewFormatVersion);
  out.key("view");
  out.string(detail::nameOf(mainViewNames, state.view));
  out.key("arrangement_zoom");
  out.number(state.arrangementZoom);
  out.key("arrangement_scroll");
  out.signedInteger(state.arrangementScroll.ticks);
  out.endObject();
  return out.finish();
}

std::expected<ViewState, Error> read(std::string_view bytes) {
  if (bytes.size() > maxDocumentBytes) {
    return std::unexpected(
        detail::tooLargeError(bytes.size(), maxDocumentBytes));
  }
  const auto tree = detail::parseJson(bytes);
  if (!tree.has_value()) {
    return std::unexpected(tree.error());
  }
  detail::StrictReader in;
  const Node root = detail::rootNode(*tree);
  ViewState state;
  if (in.expectObject(
          root, {"format", "view", "arrangement_zoom", "arrangement_scroll"})) {
    const Node format = member(root, "format");
    const std::uint64_t version =
        in.unsignedInteger(format, std::numeric_limits<std::uint64_t>::max());
    if (!in.failed() && version != viewFormatVersion) {
      in.fail(format, ErrorCode::Version,
              "view state of format version " + std::to_string(version) +
                  " cannot be read; this LINA reads version " +
                  std::to_string(viewFormatVersion));
    }
    state.view = in.enumerator(member(root, "view"), mainViewNames);
    const Node zoom = member(root, "arrangement_zoom");
    state.arrangementZoom = in.number(zoom);
    if (!in.failed() && !zoomAllowed(state.arrangementZoom)) {
      in.fail(zoom, ErrorCode::Value, "the zoom must be a positive number");
    }
    const Node scroll = member(root, "arrangement_scroll");
    state.arrangementScroll = {in.signedInteger(scroll, 0, model::maxTicks)};
  }
  if (auto error = in.takeError()) {
    return std::unexpected(std::move(*error));
  }
  return state;
}

} // namespace

std::expected<std::string, Error> writeViewState(const ViewState &state) {
  return detail::guarded([&state] { return write(state); });
}

std::expected<ViewState, Error> readViewState(std::string_view bytes) {
  return detail::guarded([bytes] { return read(bytes); });
}

} // namespace lina::format
