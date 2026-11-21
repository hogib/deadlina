// SPDX-License-Identifier: GPL-3.0-or-later
//
// Numbers are written and read the same way whatever the system's language
// settings (docs/design/10-project-file-format-requirements.md, "Fidelity"):
// here under a locale whose decimal separator is a comma.
#include "support/documents.hpp"
#include "support/scratch.hpp"

#include "lina/format/document_io.hpp"
#include "lina/format/view_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <clocale>
#include <cstdio>
#include <optional>
#include <string>

namespace lina::format::testing {

namespace {

// Switches the C library's number formatting to the first comma-decimal
// locale that is installed, and back when it goes.
class CommaDecimalLocale {
public:
  // NOLINTNEXTLINE(concurrency-mt-unsafe): the tests have one thread
  CommaDecimalLocale() : previous_(std::setlocale(LC_NUMERIC, nullptr)) {
    for (const char *name : {
             "de_DE.UTF-8",
             "de_DE.utf8",
             "fr_FR.UTF-8",
             "fr_FR.utf8",
             "tr_TR.UTF-8",
             "tr_TR.utf8",
             "nl_NL.UTF-8",
             "es_ES.UTF-8",
             "it_IT.UTF-8",
             "pt_BR.UTF-8",
             "ru_RU.UTF-8",
             "pl_PL.UTF-8",
         }) {
      // NOLINTNEXTLINE(concurrency-mt-unsafe): the tests have one thread
      if (std::setlocale(LC_NUMERIC, name) != nullptr && commaInUse()) {
        name_ = name;
        return;
      }
    }
  }
  CommaDecimalLocale(const CommaDecimalLocale &) = delete;
  CommaDecimalLocale &operator=(const CommaDecimalLocale &) = delete;
  CommaDecimalLocale(CommaDecimalLocale &&) = delete;
  CommaDecimalLocale &operator=(CommaDecimalLocale &&) = delete;
  ~CommaDecimalLocale() {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): the tests have one thread
    restored_ = std::setlocale(LC_NUMERIC, previous_.c_str()) != nullptr;
  }

  [[nodiscard]] const std::optional<std::string> &name() const { return name_; }

  // True when the C library now prints one and a half with a comma.
  static bool commaInUse() {
    std::string text(16, '\0');
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg): asking the C library
    const int length = std::snprintf(text.data(), text.size(), "%.1f", 1.5);
    return length == 3 && text.starts_with("1,5");
  }

private:
  std::string previous_;
  bool restored_{false};
  std::optional<std::string> name_;
};

} // namespace

TEST_CASE(
    "format/locale: a comma-decimal locale changes no byte and no value") {
  const std::string expected = readText(goldenFile("v1/everything.lina"));
  const ViewState view{
      .view = MainView::Arrangement,
      .arrangementZoom = 2.5,
      .arrangementScroll = model::TickPosition{0},
  };
  {
    const CommaDecimalLocale locale;
    if (!locale.name().has_value()) {
      SKIP("no locale with a decimal comma is installed");
    }
    INFO("locale " << *locale.name());
    REQUIRE(CommaDecimalLocale::commaInUse());

    // Written: the same bytes as ever, with decimal points.
    const auto image = writeDocument(everythingDocument(), testWriter);
    REQUIRE(image.has_value());
    REQUIRE(image->bytes() == expected);
    // Read: "93.47" is ninety-three point four seven, not ninety-three.
    const auto loaded = readDocument(expected);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->document.project() == everythingDocument().project());
    REQUIRE(loaded->document.project().tempo.hundredths() == 9'347);
    REQUIRE(loaded->document.project().tracks.front().mixer.volumeDb == -6.5);

    const auto viewText = writeViewState(view);
    REQUIRE(viewText.has_value());
    REQUIRE(viewText->find("\"arrangement_zoom\": 2.5") != std::string::npos);
    const auto viewBack = readViewState(*viewText);
    REQUIRE(viewBack.has_value());
    REQUIRE(*viewBack == view);
  }
  // The locale is back to what it was.
  REQUIRE(!CommaDecimalLocale::commaInUse());
}

} // namespace lina::format::testing
