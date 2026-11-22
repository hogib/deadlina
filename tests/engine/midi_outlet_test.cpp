// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sequencer's buffers (midi_outlet.hpp), handed through a stand-in for
// the engine's plugin node that treats its two arrays the way Tracktion
// Engine v3.2.0's PluginNode does, without an engine. Under
// RealtimeSanitizer the audio thread's side runs in a real-time context, so
// an allocation there fails the test.

#include <catch2/catch_test_macros.hpp>

#include "lina/engine/internal/midi_outlet.hpp"

#include <cstddef>
#include <memory>
#include <utility>

using namespace lina::engine::internal;

namespace {

namespace te = tracktion;

constexpr std::size_t kept = MidiOutlet::freshKept;

juce::MidiMessage someNote() {
  return juce::MidiMessage::noteOn(1, 60, juce::uint8{100});
}

/// Writes `count` note-ons, as the sequencer does on the audio thread.
void fill(te::MidiMessageArray &buffer, std::size_t count)
    [[clang::nonblocking]] {
  for (std::size_t index = 0; index < count; ++index) {
    buffer.addMidiMessage(someNote(), 0.0, te::MPESourceID{});
  }
}

/// One block of the sequencer, on the audio thread, which moves what came
/// in into its buffer and adds `count` messages of its own. False when there
/// was no buffer to write into.
bool sequencerBlock(MidiOutlet &outlet, te::MidiMessageArray &engine,
                    std::size_t count) [[clang::nonblocking]] {
  // A block with nothing to send leaves the engine's array as it is.
  if (count == 0) {
    return true;
  }
  auto *buffer =
      outlet.acquire(static_cast<std::size_t>(engine.size()) + count);
  if (buffer == nullptr) {
    return false;
  }
  for (auto &message : engine) {
    buffer->add(std::move(message));
  }
  fill(*buffer, count);
  outlet.handOver(engine);
  return true;
}

/// The plugin node of one playback graph: the array it hands the plugin
/// and its output, which it swaps after every block.
struct Node {
  /// One block: the arrays are emptied, `incoming` messages are copied in,
  /// the sequencer adds `count`, the arrays are swapped. Returns what the
  /// sequencer's block returned.
  bool block(MidiOutlet &outlet, std::size_t count, std::size_t incoming = 0) {
    output.clear();
    array.clear();
    // The engine grows its array here when it has to, as Tracktion does.
    for (std::size_t index = 0; index < incoming; ++index) {
      array.addMidiMessage(someNote(), 0.0, te::MPESourceID{});
    }
    const bool sent = sequencerBlock(outlet, array, count);
    output.swapWith(array);
    return sent;
  }

  te::MidiMessageArray array;
  te::MidiMessageArray output;
};

/// Takes fresh buffers until none is left: each new graph whose arrays are
/// exchanged twice takes two.
void useUpFresh(MidiOutlet &outlet) {
  while (outlet.freshCount() > 0) {
    Node graph;
    for (int block = 0; block < 4; ++block) {
      graph.block(outlet, 1);
    }
  }
}

} // namespace

TEST_CASE("a stack hands storages over last in, first out", "[midi-outlet]") {
  StorageStack stack;
  CHECK(stack.pop() == nullptr);
  const auto small = makeMidiStorage(4);
  const auto large = makeMidiStorage(8);
  stack.push(small.get());
  stack.push(large.get());
  CHECK(stack.size() == 2);
  MidiStorage *top = stack.pop();
  CHECK(top == large.get());
  CHECK(top->capacity == 8);
  CHECK(top->below == nullptr);
  stack.push(top);
  // Taking everything hands over the chain, top first.
  MidiStorage *chain = stack.takeAll();
  CHECK(chain == large.get());
  CHECK(chain->below == small.get());
  CHECK(stack.pop() == nullptr);
  CHECK(stack.size() == 0);
}

TEST_CASE("the outlet's own buffers come round and the engine's are "
          "exchanged once per graph",
          "[midi-outlet]") {
  MidiOutlet outlet;
  outlet.prepare(16);
  CHECK(outlet.capacity() == 16);
  CHECK(outlet.freshCount() == kept);
  Node graph;
  // The first two blocks get the graph's two arrays back, which have no
  // room the outlet knows of; each is exchanged for a fresh buffer at the
  // next block that needs one.
  CHECK(graph.block(outlet, 3));
  CHECK(graph.output.size() == 3);
  CHECK(outlet.freshCount() == kept);
  CHECK(graph.block(outlet, 5));
  CHECK(graph.output.size() == 5);
  CHECK(outlet.freshCount() == kept - 1);
  CHECK(graph.block(outlet, 16));
  CHECK(outlet.freshCount() == kept - 2);
  // From here on only the outlet's own buffers turn round.
  for (int block = 0; block < 10; ++block) {
    CHECK(graph.block(outlet, 16));
    CHECK(graph.output.size() == 16);
  }
  // Blocks without messages from the sequencer leave the arrays alone, and
  // the buffers still come round, whichever comes back first.
  for (int skipped = 1; skipped <= 3; ++skipped) {
    for (int block = 0; block < skipped; ++block) {
      CHECK(graph.block(outlet, 0));
    }
    for (int block = 0; block < 4; ++block) {
      CHECK(graph.block(outlet, 16));
    }
  }
  CHECK(outlet.freshCount() == kept - 2);

  // A new graph takes two more; the main thread tops them up.
  outlet.supply();
  CHECK(outlet.freshCount() == kept);
  Node next;
  for (int block = 0; block < 6; ++block) {
    CHECK(next.block(outlet, 16));
  }
  CHECK(outlet.freshCount() == kept - 2);
  // Every storage is the outlet's, and each is held, ready or sent back.
  CHECK(outlet.accountedCount() == outlet.ownedCount());
  outlet.supply();
  CHECK(outlet.accountedCount() == outlet.ownedCount());
  CHECK(outlet.ownedCount() == 1 + kept);
}

TEST_CASE("after a graph change, memory the engine frees and allocates "
          "again at an address of the outlet's is not taken for the "
          "outlet's",
          "[midi-outlet]") {
  MidiOutlet outlet;
  outlet.prepare(16);
  auto first = std::make_unique<Node>();
  for (int block = 0; block < 4; ++block) {
    CHECK(first->block(outlet, 4));
  }
  CHECK(outlet.freshCount() == kept - 2);
  // A new graph. Its first array arrives without memory.
  Node next;
  CHECK(next.block(outlet, 4));
  // The engine frees the old graph and gives the new graph's other array
  // memory at the address of the storage the outlet handed out last there
  // (here: that very memory, moved over; its room is the engine's
  // business now).
  next.array.swapWith(first->output);
  first.reset();
  // It comes back of unknown room: it and the new graph's first array are
  // each exchanged for a fresh storage.
  CHECK(next.block(outlet, 4));
  CHECK(next.block(outlet, 4));
  CHECK(outlet.freshCount() == kept - 4);
  CHECK(outlet.accountedCount() == outlet.ownedCount());
}

TEST_CASE("a graph replaced before its arrays were exchanged leaves one "
          "more to exchange",
          "[midi-outlet]") {
  MidiOutlet outlet;
  outlet.prepare(16);
  Node first;
  CHECK(first.block(outlet, 1));
  CHECK(first.block(outlet, 1));
  CHECK(outlet.freshCount() == kept - 1);
  // The sequencer holds the first graph's second array when the next graph
  // comes: that one and the next graph's two take three in all.
  Node second;
  for (int block = 0; block < 6; ++block) {
    CHECK(second.block(outlet, 1));
  }
  CHECK(outlet.freshCount() == kept - 4);
}

TEST_CASE("without a fresh buffer for an unknown one the block gets none",
          "[midi-outlet]") {
  MidiOutlet outlet;
  outlet.prepare(16);
  useUpFresh(outlet);
  // A graph whose arrays the outlet has never seen, and nothing fresh: the
  // first block goes out in the buffer the outlet holds, the next has none.
  Node next;
  CHECK(next.block(outlet, 1));
  CHECK(!next.block(outlet, 1));
  CHECK(next.output.isEmpty());
  // Once the main thread supplies, blocks go out again.
  outlet.supply();
  for (int block = 0; block < 4; ++block) {
    CHECK(next.block(outlet, 1));
  }
}

TEST_CASE("a buffer the engine has grown is not trusted", "[midi-outlet]") {
  MidiOutlet outlet;
  outlet.prepare(16);
  Node graph;
  for (int block = 0; block < 4; ++block) {
    CHECK(graph.block(outlet, 4));
  }
  CHECK(outlet.freshCount() == kept - 2);
  // The engine copies more incoming messages into one of the outlet's
  // buffers than it has room for and moves it elsewhere in memory. The
  // sequencer sends nothing that block; when the buffer comes back, it is
  // exchanged for a fresh one.
  CHECK(graph.block(outlet, 0, 200));
  CHECK(graph.output.size() == 200);
  for (int block = 0; block < 3; ++block) {
    CHECK(graph.block(outlet, 4));
  }
  CHECK(outlet.freshCount() == kept - 3);
}

TEST_CASE("a block that needs more room than the buffers have gets none",
          "[midi-outlet]") {
  MidiOutlet outlet;
  outlet.prepare(16);
  Node graph;
  for (int block = 0; block < 3; ++block) {
    CHECK(graph.block(outlet, 16));
  }
  outlet.supply();
  CHECK(!graph.block(outlet, 17));
  // What came in counts.
  CHECK(!graph.block(outlet, 1, 16));
  CHECK(graph.output.size() == 16);
  // The fresh buffers are not spent on a block that wants too much.
  CHECK(outlet.freshCount() == kept);

  // Released, the outlet hands out nothing until it is prepared again.
  outlet.releaseFor(32);
  CHECK(outlet.capacity() == 32);
  CHECK(outlet.freshCount() == 0);
  CHECK(outlet.ownedCount() == 0);
  CHECK(outlet.accountedCount() == 0);
  Node next;
  CHECK(!next.block(outlet, 1));
  outlet.prepare(outlet.capacity());
  CHECK(next.block(outlet, 32));
}
