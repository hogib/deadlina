// SPDX-License-Identifier: GPL-3.0-or-later
//
// From a project file to sound: a document is saved into a project folder,
// loaded back from disk, handed to the engine and rendered. This is the one
// test that goes through the file format, the document model and the
// projection in a single run.

#include <catch2/catch_test_macros.hpp>

#include "lina/format/document_io.hpp"
#include "lina/format/file_system.hpp"
#include "lina/format/project_folder.hpp"
#include "projection_fixture.hpp"
#include "reference_signal.hpp"

#include <cstdint>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;
using lina::model::testing::beat;
using lina::model::testing::beats;
using lina::model::testing::makeMidiClip;
using lina::model::testing::makeNote;

TEST_CASE("a project loaded from its file sounds as it did when saved",
          "[projection][format][audio]") {
  constexpr double sampleRate = 48'000.0;
  constexpr std::int64_t frameCount = 96'000;

  ProjectionFixture fixture("project-file-render");
  const TrackId track = fixture.addToneTrack();
  // 120 beats a minute at 48 kHz: a beat is 24,000 frames. The clip starts at
  // beat 1 and its first note at the clip's start.
  fixture.addClip(track, makeMidiClip(beat(1), beats(2),
                                      {
                                          makeNote(69, beat(0), beats(1), 127),
                                          makeNote(72, beat(1), beats(1), 64),
                                      }));
  fixture.set(track, VolumeValue{-6.0});

  const AudioBuffer saved = fixture.playFromStart(sampleRate, frameCount);
  REQUIRE(firstSoundingFrame(saved.channels.at(0)) == 24'000);

  lina::format::PosixFileSystem files;
  lina::format::ProjectFolder folder(files, fixture.scratch / "project");
  REQUIRE(folder.create().has_value());
  const auto image = lina::format::writeDocument(fixture.document, "LINA test");
  REQUIRE(image.has_value());
  REQUIRE(folder.save(*image).has_value());

  auto loaded = folder.load();
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->document.project() == fixture.project());

  // The same engine is given the loaded project from scratch.
  fixture.engine.stop();
  fixture.engine.setProject(loaded->document.project(), fixture.scratch);
  fixture.engine.play();
  const AudioBuffer reloaded = fixture.engine.process(frameCount);

  CHECK(firstSoundingFrame(reloaded.channels.at(0)) == 24'000);
  CHECK(identical(saved, reloaded));
}
