// SPDX-License-Identifier: GPL-3.0-or-later
//
// For the benches only: the phases of one rebuild of the engine's playback
// graph, done by hand the way the engine does them
// (EditPlaybackContext::createNode, then
// LockFreeMultiThreadedNodePlayer::setNode in Tracktion Engine v3.2.0), on a
// graph of the engine's own kind that is never handed to the audio thread.
//
// It includes headers of the engine that its module header leaves out; they
// are read, not changed.
#ifndef LINA_TESTS_ENGINE_GRAPH_PHASES_HPP
#define LINA_TESTS_ENGINE_GRAPH_PHASES_HPP

#include "large_project.hpp"

#include <tracktion_engine/tracktion_engine.h>
#include <tracktion_graph/tracktion_graph.h>

#include <tracktion_engine/playback/graph/tracktion_TracktionEngineNode.h>

#include <tracktion_engine/playback/graph/tracktion_EditNodeBuilder.h>

#include <cstddef>
#include <memory>
#include <print>
#include <string>
#include <vector>

namespace lina::engine::testing {

struct GraphPhases {
  /// EditNodeBuilder: the node tree made from the edit.
  std::vector<double> build;
  /// createNodeGraph: transformed, ordered, mapped by node identity.
  std::vector<double> transform;
  /// Node::initialise of every node, given the graph being replaced.
  std::vector<double> initialise;
  /// The same without a graph being replaced.
  std::vector<double> initialiseWithoutOld;
  /// The graph being replaced, destroyed.
  std::vector<double> destroy;
  std::size_t orderedNodes = 0;
  std::size_t allNodes = 0;
};

inline GraphPhases measureGraphPhases(tracktion::Edit &edit, double sampleRate,
                                      int blockSize, int runs) {
  namespace te = tracktion;
  namespace tg = tracktion::graph;
  GraphPhases phases;
  tg::PlayHead playHead;
  tg::PlayHeadState playHeadState{playHead};
  te::ProcessState processState{playHeadState, edit.tempoSequence};
  // As EditPlaybackContext::createNode: the nodes are made for the device's
  // block size, and the player prepares them for that plus 10 %
  // (EditPlaybackContext::NodePlaybackContext::setNode). Another size here
  // would make the plugins initialise afresh on the next real rebuild.
  const te::CreateNodeParams params{
      .processState = processState,
      .sampleRate = sampleRate,
      .blockSize = blockSize,
  };
  const int preparedBlockSize = juce::roundToInt(blockSize * 1.1);

  const auto prepare = [&](tg::NodeGraph *old, std::vector<double> &into) {
    std::unique_ptr<tg::Node> node;
    phases.build.push_back(
        milliseconds([&] { node = te::createNodeForEdit(edit, params); }));
    std::unique_ptr<tg::NodeGraph> graph;
    phases.transform.push_back(milliseconds(
        [&] { graph = tg::createNodeGraph(std::move(node), false); }));
    into.push_back(milliseconds([&] {
      const tg::PlaybackInitialisationInfo info{
          .sampleRate = sampleRate,
          .blockSize = preparedBlockSize,
          .nodeGraph = *graph,
          .nodeGraphToReplace = old,
      };
      for (auto *n : graph->orderedNodes) {
        n->initialise(info);
      }
    }));
    phases.orderedNodes = graph->orderedNodes.size();
    phases.allNodes = graph->sortedNodes.size();
    return graph;
  };

  std::vector<double> unused;
  auto previous = prepare(nullptr, unused);
  for (int run = 0; run < runs; ++run) {
    auto next = prepare(previous.get(), phases.initialise);
    phases.destroy.push_back(milliseconds([&] { previous.reset(); }));
    previous = std::move(next);
  }
  phases.destroy.push_back(milliseconds([&] { previous.reset(); }));
  for (int run = 0; run < runs; ++run) {
    std::ignore = prepare(nullptr, phases.initialiseWithoutOld);
  }
  return phases;
}

inline void reportTiming(const std::string &what, const Timing &timing) {
  std::println("  {:<46} median {:8.2f} ms   max {:8.2f} ms", what,
               timing.median, timing.max);
}

inline void reportGraphPhases(const GraphPhases &phases) {
  std::println("  (graph: {} nodes in processing order, {} with inner ones)",
               phases.orderedNodes, phases.allNodes);
  reportTiming("engine: createNodeForEdit", summarise(phases.build));
  reportTiming("engine: createNodeGraph (transform, order)",
               summarise(phases.transform));
  reportTiming("engine: initialise nodes, old graph given",
               summarise(phases.initialise));
  reportTiming("engine: initialise nodes, no old graph",
               summarise(phases.initialiseWithoutOld));
  reportTiming("engine: destroy the old graph", summarise(phases.destroy));
}

} // namespace lina::engine::testing

#endif // LINA_TESTS_ENGINE_GRAPH_PHASES_HPP
