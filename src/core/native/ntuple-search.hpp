#pragma once

#include "ntuple.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace drop7::ntuple::search {

struct Options {
  int depth = 2;
  int reveal_samples = 3;
  int internal_action_width = 2;
  std::uint64_t max_work = 10'000;
  int games = 16;
  int max_moves = 500;
  std::uint32_t seed_start = 0x5d70'0000u;
  std::string checkpoint =
      "/tmp/drop7-ntuple-chance-hierarchical-l1-500k.bin";
};

struct SearchContext {
  const Model& model;
  const Options& options;
  std::uint64_t work = 0;
  bool complete = true;
};

struct Decision {
  int physical_column = -1;
  float value = -std::numeric_limits<float>::infinity();
  std::uint64_t work = 0;
  bool complete = true;
};

inline float decisionValue(const State& source, int depth,
                           SearchContext& context);

inline float actionValue(const State& canonical, int canonical_action,
                         int depth, SearchContext& context) {
  if (depth < 1) return context.model.value(canonical);
  const std::uint32_t hash = observableHash(canonical);
  const int reveal_offset =
      static_cast<int>(mix32(hash ^ 0x5245'564cu) % 7u);
  double total = 0;
  int completed_samples = 0;
  for (int sample = 0; sample < context.options.reveal_samples; ++sample) {
    if (context.work >= context.options.max_work) {
      context.complete = false;
      break;
    }
    const auto target_reveal = static_cast<std::uint8_t>(
        ((reveal_offset + sample) % kBoardSize) + 1);
    const std::uint32_t base_seed = mix32(
        hash ^ (static_cast<std::uint32_t>(sample + 1) * 0xc2b2'ae35u) ^
        0x5345'4152u);
    Mulberry32 random(seedWithFirstDisc(base_seed, target_reveal));
    MoveResult move;
    if (!playMove(canonical, canonical_action, random, move)) {
      throw std::runtime_error("sparse search selected an illegal action");
    }
    ++context.work;
    ++completed_samples;
    if (move.state.game_over) {
      total += 1.0;
      continue;
    }
    if (depth == 1) {
      total += 1.0 + context.model.value(move.state);
      continue;
    }

    // U(board, phase) lives just before the next disc is observed. Enumerate
    // that newly observed disc exactly, then maximize conditionally.
    double future_total = 0;
    const int disc_offset =
        static_cast<int>(mix32(hash ^ 0x4e45'5854u) % 7u);
    for (int disc_sample = 0; disc_sample < kBoardSize; ++disc_sample) {
      State child = move.state;
      child.next_disc = static_cast<std::uint8_t>(
          ((disc_offset + disc_sample) % kBoardSize) + 1);
      future_total += decisionValue(child, depth - 1, context);
    }
    total += 1.0 + future_total / kBoardSize;
  }
  if (completed_samples == 0) return context.model.value(canonical);
  return static_cast<float>(total / completed_samples);
}

inline std::vector<int> candidateActions(const State& canonical, int depth,
                                         SearchContext& context) {
  int legal_count = 0;
  const auto legal = legalColumns(canonical.board, legal_count);
  std::vector<int> result(legal.begin(), legal.begin() + legal_count);
  if (depth <= 1 || legal_count <= context.options.internal_action_width) {
    return result;
  }

  struct RankedAction {
    int action = -1;
    float value = 0;
  };
  std::vector<RankedAction> ranked;
  ranked.reserve(result.size());
  for (int action : result) {
    ranked.push_back({action, actionValue(canonical, action, 1, context)});
  }
  constexpr std::array<int, kBoardSize> tie_rank{{5, 3, 1, 0, 2, 4, 6}};
  std::sort(ranked.begin(), ranked.end(), [&](const auto& first,
                                               const auto& second) {
    if (first.value != second.value) return first.value > second.value;
    return tie_rank[first.action] < tie_rank[second.action];
  });
  result.clear();
  for (int index = 0;
       index < std::min(context.options.internal_action_width,
                        static_cast<int>(ranked.size()));
       ++index) {
    result.push_back(ranked[index].action);
  }
  return result;
}

inline float decisionValue(const State& source, int depth,
                           SearchContext& context) {
  if (source.game_over || depth <= 0 ||
      context.work >= context.options.max_work) {
    if (context.work >= context.options.max_work) context.complete = false;
    return context.model.value(source);
  }
  const State canonical = canonicalize(source).state;
  const auto actions = candidateActions(canonical, depth, context);
  float best = -std::numeric_limits<float>::infinity();
  for (int action : actions) {
    best = std::max(best, actionValue(canonical, action, depth, context));
  }
  return best;
}

inline Decision chooseMove(const Model& model, const State& source,
                           const Options& options) {
  const auto canonical = canonicalize(source);
  int legal_count = 0;
  const auto legal = legalColumns(canonical.state.board, legal_count);
  SearchContext context{model, options};
  constexpr std::array<int, kBoardSize> tie_rank{{5, 3, 1, 0, 2, 4, 6}};
  int best_action = -1;
  float best = -std::numeric_limits<float>::infinity();
  for (int offset = 0; offset < legal_count; ++offset) {
    const int action = legal[offset];
    const float value = actionValue(canonical.state, action, options.depth,
                                    context);
    if (value > best ||
        (value == best && best_action >= 0 &&
         tie_rank[action] < tie_rank[best_action])) {
      best = value;
      best_action = action;
    }
  }
  return {
      physicalAction(best_action, canonical.mirrored),
      best,
      context.work,
      context.complete,
  };
}

inline bool selfTest(std::ostream& output) {
  Options options;
  options.depth = 2;
  options.reveal_samples = 2;
  options.max_work = 20'000;
  Model model(23.0f, true, false, true);
  State state = initialHeadlessState(0x2d70'0042u);
  for (int action : {3, 1, 5, 2, 4, 0}) {
    MoveResult move;
    if (!playHeadlessMove(state, 0x2d70'0042u, action, move)) break;
  }
  State mirrored = state;
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      mirrored.board[indexOf(row, column)] =
          state.board[indexOf(row, kBoardSize - 1 - column)];
    }
  }
  const Decision first = chooseMove(model, state, options);
  const Decision repeat = chooseMove(model, state, options);
  const Decision reflected = chooseMove(model, mirrored, options);
  const bool deterministic =
      first.physical_column == repeat.physical_column &&
      first.value == repeat.value && first.work == repeat.work;
  const bool mirror_safe =
      first.physical_column == kBoardSize - 1 - reflected.physical_column &&
      std::abs(first.value - reflected.value) < 1e-5f;
  const bool bounded = first.work <= options.max_work;
  output << "NTUPLE_SEARCH_SELF_TEST {\"passed\":"
         << (deterministic && mirror_safe && bounded ? "true" : "false")
         << ",\"deterministic\":" << (deterministic ? "true" : "false")
         << ",\"mirrorSafe\":" << (mirror_safe ? "true" : "false")
         << ",\"bounded\":" << (bounded ? "true" : "false")
         << ",\"work\":" << first.work << "}\n";
  return deterministic && mirror_safe && bounded;
}

inline int benchmark(const Options& options) {
  if (options.depth < 1 || options.depth > 3 || options.reveal_samples < 1 ||
      options.reveal_samples > 7 || options.internal_action_width < 1 ||
      options.internal_action_width > 7 || options.max_work < 1 ||
      options.games < 1 || options.max_moves < 1) {
    throw std::invalid_argument("invalid n-tuple search option");
  }
  Model model(0, true, false, true);
  model.load(options.checkpoint);
  const auto started = std::chrono::steady_clock::now();
  double total_score = 0;
  double total_moves = 0;
  std::int64_t minimum = std::numeric_limits<std::int64_t>::max();
  std::int64_t maximum = std::numeric_limits<std::int64_t>::min();
  std::uint64_t work = 0;
  int incomplete = 0;
  for (int game = 0; game < options.games; ++game) {
    const std::uint32_t seed =
        options.seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.max_moves) {
      const Decision decision = chooseMove(model, state, options);
      work += decision.work;
      if (!decision.complete) ++incomplete;
      MoveResult move;
      if (!playHeadlessMove(state, seed, decision.physical_column, move)) {
        throw std::runtime_error("sparse search chose an illegal move");
      }
    }
    total_score += state.score;
    total_moves += state.moves_played;
    minimum = std::min(minimum, state.score);
    maximum = std::max(maximum, state.score);
  }
  const double seconds = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  std::cout << std::fixed << std::setprecision(3)
            << "NTUPLE_SEARCH {\"depth\":" << options.depth
            << ",\"revealSamples\":" << options.reveal_samples
            << ",\"internalActionWidth\":"
            << options.internal_action_width << ",\"maxWork\":"
            << options.max_work << ",\"games\":" << options.games
            << ",\"seedStart\":" << options.seed_start
            << ",\"meanScore\":" << total_score / options.games
            << ",\"meanMoves\":" << total_moves / options.games
            << ",\"minimumScore\":" << minimum
            << ",\"maximumScore\":" << maximum
            << ",\"meanWorkPerMove\":" << work / total_moves
            << ",\"incompleteDecisions\":" << incomplete
            << ",\"seconds\":" << seconds
            << ",\"peakResidentBytes\":" << peakResidentBytes() << "}\n";
  return 0;
}

}  // namespace drop7::ntuple::search
