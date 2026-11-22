// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/engine/internal/midi_outlet.hpp"

#include <algorithm>
#include <tuple>
#include <utility>

namespace lina::engine::internal {

std::unique_ptr<MidiStorage> makeMidiStorage(std::size_t capacity) {
  auto storage = std::make_unique<MidiStorage>();
  storage->messages.reserve(static_cast<int>(capacity));
  storage->capacity = capacity;
  return storage;
}

void StorageStack::push(MidiStorage *storage) [[clang::nonblocking]] {
  storage->below = top_.load(std::memory_order_relaxed);
  while (!top_.compare_exchange_weak(storage->below, storage,
                                     std::memory_order_release,
                                     std::memory_order_relaxed)) {
  }
}

MidiStorage *StorageStack::pop() [[clang::nonblocking]] {
  // Only one thread pops, so the storage on top cannot be popped and pushed
  // again between reading it and taking it off.
  MidiStorage *top = top_.load(std::memory_order_acquire);
  while (top != nullptr &&
         !top_.compare_exchange_weak(top, top->below, std::memory_order_acquire,
                                     std::memory_order_acquire)) {
  }
  if (top != nullptr) {
    top->below = nullptr;
  }
  return top;
}

MidiStorage *StorageStack::takeAll() {
  return top_.exchange(nullptr, std::memory_order_acquire);
}

std::size_t StorageStack::size() const {
  std::size_t count = 0;
  for (const MidiStorage *storage = top_.load(); storage != nullptr;
       storage = storage->below) {
    ++count;
  }
  return count;
}

void MidiOutlet::prepare(std::size_t capacity) {
  release();
  capacity_ = capacity;
  owned_.push_back(makeMidiStorage(capacity));
  held_ = owned_.back().get();
  supply();
}

void MidiOutlet::releaseFor(std::size_t capacity) {
  release();
  capacity_ = capacity;
}

void MidiOutlet::release() {
  std::ignore = fresh_.takeAll();
  freshCount_ = 0;
  std::ignore = returned_.takeAll();
  held_ = nullptr;
  outThere_ = {};
  lastBackEmpty_ = false;
  owned_.clear();
}

void MidiOutlet::freeChain(MidiStorage *chain) {
  while (chain != nullptr) {
    const MidiStorage *gone = std::exchange(chain, chain->below);
    std::erase_if(owned_, [gone](const std::unique_ptr<MidiStorage> &owned) {
      return owned.get() == gone;
    });
  }
}

void MidiOutlet::supply() {
  freeChain(returned_.takeAll());
  while (freshCount_.load() < freshKept) {
    owned_.push_back(makeMidiStorage(capacity_));
    fresh_.push(owned_.back().get());
    freshCount_.fetch_add(1);
  }
}

std::size_t MidiOutlet::accountedCount() const {
  return (held_ == nullptr ? 0 : 1) + fresh_.size() + returned_.size();
}

tracktion::MidiMessageArray *MidiOutlet::acquire(std::size_t room)
    [[clang::nonblocking]] {
  if (held_ == nullptr) {
    return nullptr;
  }
  if (held_->capacity == 0) {
    // Memory of unknown room: exchanged for a fresh storage if there is one.
    if (MidiStorage *fresh = fresh_.pop()) {
      freshCount_.fetch_sub(1);
      returned_.push(std::exchange(held_, fresh));
    }
  }
  if (held_->capacity < room) {
    return nullptr;
  }
  held_->messages.clear();
  return &held_->messages;
}

void MidiOutlet::handOver(tracktion::MidiMessageArray &engine)
    [[clang::nonblocking]] {
  engine.swapWith(held_->messages);
  const HandedOut given{
      .memory = engine.begin(),
      .capacity = held_->capacity,
  };
  const void *back = held_->messages.begin();
  if (back == nullptr) {
    // A new graph's array: what the engine held of the outlet's lay in the
    // graph before, except what went out with this graph's other array.
    const HandedOut keep = lastBackEmpty_ ? outThere_.at(0) : HandedOut{};
    outThere_ = {keep, HandedOut{}};
  }
  lastBackEmpty_ = back == nullptr;
  // What came back is the outlet's own if the engine held it.
  held_->capacity = 0;
  for (HandedOut &out : outThere_) {
    if (out.memory != nullptr && out.memory == back) {
      held_->capacity = out.capacity;
      out = {};
    }
  }
  if (outThere_.at(0).memory == nullptr) {
    outThere_.at(0) = std::exchange(outThere_.at(1), {});
  }
  // The newer goes first; an older one the engine still held is from a graph
  // that is gone, since the engine's arrays hold two at most.
  outThere_.at(1) = outThere_.at(0);
  outThere_.at(0) = given;
}

} // namespace lina::engine::internal
