// SPDX-License-Identifier: GPL-3.0-or-later
//
// What edits cost while doc 11's large project plays, how many playback
// graphs they make the engine build, and what one block of audio costs. Not
// a test and not part of the gating suite
// (docs/design/14-build-test-and-ci.md, "Benches"): it reads the clock and
// prints milliseconds. Build the target `lina_projection_large_bench` in the
// release preset and run it by hand, with nothing else busy:
//
//     cmake --build --preset release --target lina_projection_large_bench
//     build/release/tests/engine/lina_projection_large_bench
//
// Two cases: "[large]" is large_project.hpp's project, "[reference]" one
// near doc 11's reference project. Every edit is timed `runs` times, made
// and taken back by a second command so that the project keeps its size,
// each time through Engine::applyChanges as the application calls it while
// playing; the median and the maximum of all of them are printed, with the
// number of graphs the engine built for each. Graphs are counted by the
// sequencer of the first MIDI track, which every graph holds.

#include <catch2/catch_test_macros.hpp>

#include "graph_phases.hpp"
#include "large_project.hpp"
#include "lina/engine/internal/tracktion_access.hpp"
#include "lina/engine/testing/engine_fixture.hpp"

#include <cstdint>
#include <print>
#include <string>
#include <tuple>
#include <vector>

using namespace lina::engine;
using namespace lina::engine::testing;
using namespace lina::model;

namespace {

namespace te = tracktion;

constexpr int runs = 7;
constexpr double sampleRate = 48'000.0;
constexpr int blockSize = 512;
/// Beat 96 at 120 beats per minute.
constexpr std::int64_t playFrom = std::int64_t{96} * 24'000;
/// Blocks timed one by one for the cost of a block.
constexpr int timedBlocks = 600;

/// One whole rebuild of the graph, forced, and the block after each.
void measureRebuild(Engine &engine) {
  std::println("\none rebuild of the playback graph (Engine::syncGraph):");
  std::vector<double> rebuilds;
  std::vector<double> blocks;
  for (int run = 0; run < runs; ++run) {
    rebuilds.push_back(milliseconds([&] { engine.syncGraph(); }));
    blocks.push_back(
        milliseconds([&] { std::ignore = engine.process(blockSize); }));
  }
  reportTiming("whole rebuild, on the message thread", summarise(rebuilds));
  reportTiming("the block right after it (one thread)", summarise(blocks));
}

/// Blocks of a stream that nobody edits.
void measureBlocks(Engine &engine) {
  std::vector<double> blocks;
  blocks.reserve(timedBlocks);
  for (int block = 0; block < timedBlocks; ++block) {
    blocks.push_back(
        milliseconds([&] { std::ignore = engine.process(blockSize); }));
  }
  const Timing timing = summarise(blocks);
  std::println("\n{} blocks of 512 frames in a row, one thread:", timedBlocks);
  reportTiming("one block", timing);
}

/// Times an edit made and taken back `runs` times.
void timeEdit(Engine &engine, const internal::SequencerPlugin &witness,
              Document &document, const BenchEdit &edit) {
  std::vector<double> documentTimes;
  std::vector<double> engineTimes;
  std::string builds;
  for (int run = 0; run < 2 * runs; ++run) {
    INFO(edit.name << ", run " << run);
    ChangeSet changes;
    documentTimes.push_back(milliseconds(
        [&] { changes = performOrFail(document, edit.command(run)); }));
    const std::uint64_t before = witness.graphsPrepared();
    engineTimes.push_back(milliseconds(
        [&] { engine.applyChanges(document.project(), changes); }));
    builds += std::to_string(witness.graphsPrepared() - before);
    std::ignore = engine.process(blockSize);
  }
  std::println(" {} (graphs built per edit: {})", edit.name, builds);
  reportTiming("document", summarise(documentTimes));
  reportTiming("Engine::applyChanges, main thread", summarise(engineTimes));
}

void benchmark(const char *name, const LargeProjectShape &shape) {
  EngineFixture fixture(name);
  Document document;
  LargeProject large;
  const double building = milliseconds(
      [&] { large = buildLargeProject(document, fixture.scratch, shape); });
  std::println("project: {} tracks, {} MIDI clips + {} big, {} audio "
               "clips, {} notes; built command by command in {:.0f} ms",
               document.project().tracks.size(),
               large.midiClips.size() * large.midiClips.front().size(),
               large.bigClips.size(), large.audioClips.size(), large.noteCount,
               building);

  std::println("setProject: {:.0f} ms", milliseconds([&] {
                 fixture.engine.setProject(document.project(), fixture.scratch);
               }));
  std::println("openManualDevice: {:.0f} ms", milliseconds([&] {
                 fixture.engine.openManualDevice(
                     {.sampleRate = sampleRate, .blockSize = blockSize});
               }));
  std::println("first play (first graph, context made): {:.0f} ms",
               milliseconds([&] { fixture.engine.play(playFrom); }));
  std::println("play again: {:.0f} ms",
               milliseconds([&] { fixture.engine.play(playFrom); }));
  std::ignore = fixture.engine.process(std::int64_t{16} * blockSize);

  auto &edit = internal::TracktionAccess::edit(fixture.engine);
  const auto &projection =
      internal::TracktionAccess::projection(fixture.engine);
  const auto &witness =
      *projection.tracks().at(large.midiTracks.front()).sequencer;

  measureBlocks(fixture.engine);
  measureRebuild(fixture.engine);
  reportGraphPhases(measureGraphPhases(edit, sampleRate, blockSize, runs));

  std::println("\nedits while playing:");
  for (const BenchEdit &benchEdit : benchEdits(document, large, shape)) {
    timeEdit(fixture.engine, witness, document, benchEdit);
  }
  std::uint64_t dropped = 0;
  for (const auto &[id, entry] : projection.tracks()) {
    if (entry.sequencer != nullptr) {
      dropped += entry.sequencer->blocksDropped();
    }
  }
  std::println("\nblocks a sequencer did not send for want of room: {}",
               dropped);
  CHECK(dropped == 0);
  CHECK(static_cast<std::size_t>(te::getAudioTracks(edit).size()) ==
        large.midiTracks.size() + large.audioTracks.size() + 2);
}

} // namespace

TEST_CASE("edits while the large project plays", "[bench][large]") {
  benchmark("projection-large-bench", LargeProjectShape{});
}

TEST_CASE("edits while the reference project plays", "[bench][reference]") {
  benchmark("projection-large-bench-reference", referenceProjectShape());
}
