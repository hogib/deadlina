// SPDX-License-Identifier: GPL-3.0-or-later
//
// How LINA's sequencer (sequencer.hpp) puts messages into the engine's MIDI
// buffer without the buffer growing on the audio thread.
//
// What the engine does (Tracktion Engine v3.2.0, PluginNode::process): the
// plugin is handed a MidiMessageArray that belongs to the plugin's node. The
// node empties it each block without letting go of its memory, copies the
// block's incoming messages into it, calls the plugin, and then swaps it with
// the node's output buffer. The next block the plugin is handed the other of
// the two. Nothing in the engine reserves room in either, and an array that
// is given more messages than it has room for grows, which allocates. A new
// playback graph brings a new node with two new, empty arrays.
//
// So the sequencer never adds to the engine's array. It writes the block into
// a storage of its own with room reserved on the main thread, swaps that into
// the engine's array, and keeps the memory that was there. The engine's two
// arrays and the one the sequencer holds then turn round between them. The
// sequencer remembers which of its storages the engine holds, by the address
// of their memory, which only it gives out; the engine's two arrays can hold
// at most two of them. Memory that comes back and is one of those has the
// room it was made with. Anything else (the two arrays of every new graph, or
// one the engine has reallocated) has room unknown; the next block that needs
// room exchanges it for a fresh storage that the main thread made, and sends
// it back to the main thread to be freed. A graph uses three fresh storages
// at most: one for each of its two arrays, and one for memory of unknown room
// that the graph before left the sequencer holding. The main thread tops them
// up whenever a graph is being prepared and whenever a sequence is published.
// The graph before can still be taking its share while the next is being
// prepared, so twice that is kept ready.
//
// An array that comes back without any memory is one of a new graph's two
// arrays, which nothing has written to yet. What the engine held of the
// outlet's until then lay in the arrays of the graph before, which the main
// thread is about to free, and is forgotten, so that an address the engine
// frees and allocates again is not taken for the outlet's. The one exception
// is the storage handed out at the block before, when that block met the new
// graph's other array: it lies in the new graph. The engine allocates its
// arrays only for MIDI that comes in from upstream, which LINA's tracks do not
// have yet; once they do, a new graph whose first array arrives already
// holding incoming MIDI has to be told apart another way.
//
// Every storage of one outlet has the same room. When a block needs more, or
// no storage of known room is at hand, acquire() returns null and the caller
// sends nothing that block. More room means a new outlet.
//
// Ownership: every storage is owned by the outlet, on the main thread. The
// audio thread and the two stacks only pass non-owning pointers to them
// around; the main thread frees a storage by letting go of it.
#ifndef LINA_ENGINE_INTERNAL_MIDI_OUTLET_HPP
#define LINA_ENGINE_INTERNAL_MIDI_OUTLET_HPP

#include <tracktion_engine/tracktion_engine.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace lina::engine::internal {

/// A MIDI buffer with room for `capacity` messages.
struct MidiStorage {
  tracktion::MidiMessageArray messages;
  std::size_t capacity{0};
  /// The storage below this one while it lies in a StorageStack.
  MidiStorage *below{nullptr};
};

/// Makes a storage with room for `capacity` messages. Main thread.
std::unique_ptr<MidiStorage> makeMidiStorage(std::size_t capacity);

/// A stack through which storages pass from one thread to the other without
/// a lock and without ever being full. Storages are pushed by one thread and
/// taken by one other thread. The stack owns nothing.
class StorageStack {
public:
  void push(MidiStorage *storage) [[clang::nonblocking]];
  /// The storage on top, or null. Only one thread may pop.
  [[nodiscard]] MidiStorage *pop() [[clang::nonblocking]];
  /// Empties the stack and returns what was in it, chained through `below`,
  /// the top first. Only the thread that pops.
  [[nodiscard]] MidiStorage *takeAll();
  /// How many storages lie in the stack. Only while nobody pushes or pops.
  [[nodiscard]] std::size_t size() const;

private:
  std::atomic<MidiStorage *> top_{nullptr};
};

class MidiOutlet {
public:
  /// How many fresh storages the main thread keeps ready.
  static constexpr std::size_t freshKept = 6;

  // --- Main thread ---

  /// Sets the outlet up anew with storages of room for `capacity` messages.
  /// Only while the audio thread does not use the outlet.
  void prepare(std::size_t capacity);
  /// Frees every storage and keeps the room for the next prepare(). Only
  /// while the audio thread does not use the outlet.
  void release();
  /// Frees every storage; prepare() with `capacity` makes them. Only while
  /// the audio thread does not use the outlet.
  void releaseFor(std::size_t capacity);
  /// Frees the storages the audio thread sent back and keeps `freshKept`
  /// fresh ones ready. At any time.
  void supply();
  [[nodiscard]] std::size_t capacity() const { return capacity_; }
  /// How many fresh storages are ready.
  [[nodiscard]] std::size_t freshCount() const { return freshCount_.load(); }
  /// How many storages the outlet owns.
  [[nodiscard]] std::size_t ownedCount() const { return owned_.size(); }
  /// How many storages the audio thread holds or has sent back, and how
  /// many lie ready: together, every storage the outlet owns. Only while the
  /// audio thread does not use the outlet.
  [[nodiscard]] std::size_t accountedCount() const;

  // --- Audio thread ---

  /// An empty buffer with room for at least `room` messages, or null when
  /// there is none.
  [[nodiscard]] tracktion::MidiMessageArray *acquire(std::size_t room)
      [[clang::nonblocking]];
  /// Swaps the buffer acquire() gave into `engine`, the engine's buffer of
  /// this block, and keeps what was there.
  void handOver(tracktion::MidiMessageArray &engine) [[clang::nonblocking]];

private:
  struct HandedOut {
    const void *memory{nullptr};
    std::size_t capacity{0};
  };

  /// Frees the storages in a chain that takeAll() returned.
  void freeChain(MidiStorage *chain);

  /// Main thread: the room of every storage, and every storage.
  std::size_t capacity_{0};
  std::vector<std::unique_ptr<MidiStorage>> owned_;
  /// Audio thread, or main thread while the audio thread does not use the
  /// outlet: the storage the next block is written into. Its room is 0
  /// while it is not known.
  MidiStorage *held_{nullptr};
  /// Audio thread: whether the last block that handed out met an array
  /// without memory.
  bool lastBackEmpty_{false};
  /// The storages of the outlet's that the engine holds, the newer first;
  /// an empty entry has no memory.
  std::array<HandedOut, 2> outThere_{};
  /// Main thread to audio thread: fresh storages, and how many.
  StorageStack fresh_;
  std::atomic<std::size_t> freshCount_{0};
  /// Audio thread to main thread: storages to free.
  StorageStack returned_;
};

} // namespace lina::engine::internal

#endif // LINA_ENGINE_INTERNAL_MIDI_OUTLET_HPP
