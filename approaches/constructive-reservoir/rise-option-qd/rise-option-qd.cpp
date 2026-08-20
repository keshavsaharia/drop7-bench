#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Seed-free B0 for a rise-boundary option policy and a deterministic
// MAP-Elites archive.  This executable has no gameplay, corpus, replay, or
// training mode.  Its synthetic transitions place the
// visible disc and exercise the exact row-rise helper, but never pretend to be
// sampled Drop7 games: cascades and future discs are intentionally absent.
namespace drop7::rise_option_qd {

constexpr std::size_t kOptionCapacity = 8;
constexpr std::size_t kArchiveAxis = 4;
constexpr std::size_t kArchiveCells =
    kArchiveAxis * kArchiveAxis * kArchiveAxis;
constexpr std::uint8_t kNoOption = 0xffu;
constexpr std::uint64_t kMaximumWork = 1'000'000;
constexpr std::size_t kMaximumBytes = 1u << 20;
constexpr std::uint64_t kCheckpointMagic = 0x4452'4f51'4442'3031ull;
constexpr std::uint32_t kCheckpointVersion = 1;
constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

static_assert(kMovesPerLevel == 5);
static_assert(kLevelBonus == 17'000);
static_assert(kArchiveCells == 64);

struct PublicState {
  Board board{};
  std::uint8_t next_disc = 1;
  std::uint8_t moves_remaining = kMovesPerLevel;
  bool terminal = false;

  bool operator==(const PublicState&) const = default;
};

void validate(const PublicState& state) {
  if (state.next_disc < 1 || state.next_disc > kBoardSize ||
      state.moves_remaining > kMovesPerLevel ||
      (!state.terminal && state.moves_remaining == 0)) {
    throw std::invalid_argument("invalid public rise-option state");
  }
  for (const std::uint8_t cell : state.board) {
    if (cell > kCracked) {
      throw std::invalid_argument("invalid public board token");
    }
  }
}

PublicState publicState(const State& source) {
  PublicState result{source.board, source.next_disc,
                     static_cast<std::uint8_t>(source.moves_remaining),
                     source.game_over};
  validate(result);
  return result;
}

Board mirrorBoard(const Board& source) {
  Board result{};
  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      result[indexOf(row, kBoardSize - 1 - column)] =
          source[indexOf(row, column)];
    }
  }
  return result;
}

PublicState mirror(const PublicState& source) {
  PublicState result = source;
  result.board = mirrorBoard(source.board);
  return result;
}

enum Feature : std::size_t {
  kBias,
  kTargetErrorImprovement,
  kTargetHeadroom,
  kReservoirImprovement,
  kReservoirMass,
  kImmediateTriggers,
  kQuietBuild,
  kLateRelease,
  kCoverFrontierImprovement,
  kCrackedFrontierImprovement,
  kOpenColumns,
  kMinimumTopSlack,
  kPeakHeight,
  kRoughnessImprovement,
  kAdjacentOnes,
  kTripleTwos,
  kEdgeDistance,
  kLandingHeight,
  kFeatureCount,
};

struct Option {
  std::uint32_t id = 0;
  std::array<std::uint8_t, kBoardSize> target_heights{};
  std::array<double, kFeatureCount> weights{};

  bool operator==(const Option&) const = default;
};

void validate(const Option& option) {
  for (const std::uint8_t height : option.target_heights) {
    if (height > kBoardSize) {
      throw std::invalid_argument("option target height is out of range");
    }
  }
  for (const double weight : option.weights) {
    if (!std::isfinite(weight) || std::abs(weight) > 64.0) {
      throw std::invalid_argument("option weight is invalid");
    }
  }
}

Option reflectOption(const Option& source) {
  Option result = source;
  std::reverse(result.target_heights.begin(), result.target_heights.end());
  return result;
}

struct WorkCounters {
  std::uint64_t decisions = 0;
  std::uint64_t legal_siblings_scored = 0;
  std::uint64_t feature_extractions = 0;
  std::uint64_t synthetic_steps = 0;
  std::uint64_t archive_attempts = 0;
  std::uint64_t archive_insertions = 0;
  std::uint64_t archive_replacements = 0;
  std::uint64_t mutations = 0;
  std::uint64_t peak_archive_entries = 0;

  bool operator==(const WorkCounters&) const = default;

  std::uint64_t work() const {
    return legal_siblings_scored + feature_extractions + synthetic_steps +
           archive_attempts + mutations;
  }
};

struct BoardStats {
  std::array<int, kBoardSize> heights{};
  int open_columns = 0;
  int minimum_top_slack = kBoardSize;
  int peak_height = 0;
  int roughness = 0;
  int triggers = 0;
  int cover_frontier = 0;
  int cracked_frontier = 0;
  int adjacent_ones = 0;
  int triple_twos = 0;
  double reservoir_mass = 0.0;
};

bool numberedNeighbor(const Board& board, int row, int column) {
  constexpr std::array<std::array<int, 2>, 4> directions{{
      {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
  }};
  for (const auto& direction : directions) {
    const int next_row = row + direction[0];
    const int next_column = column + direction[1];
    if (inside(next_row, next_column) &&
        isNumbered(board[indexOf(next_row, next_column)])) {
      return true;
    }
  }
  return false;
}

BoardStats analyzeBoard(const Board& board) {
  BoardStats result;
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      result.heights[column] += board[indexOf(row, column)] != kEmpty;
    }
    const int slack = kBoardSize - result.heights[column];
    result.open_columns += slack > 0;
    result.minimum_top_slack = std::min(result.minimum_top_slack, slack);
    result.peak_height = std::max(result.peak_height, result.heights[column]);
    if (column > 0) {
      result.roughness +=
          std::abs(result.heights[column] - result.heights[column - 1]);
    }
  }

  for (int row = 0; row < kBoardSize; ++row) {
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = board[indexOf(row, column)];
      if (cell == kSolid && numberedNeighbor(board, row, column)) {
        ++result.cover_frontier;
      } else if (cell == kCracked && numberedNeighbor(board, row, column)) {
        ++result.cracked_frontier;
      }
      if (!isNumbered(cell)) continue;

      const int horizontal = lineLength(board, row, column, false);
      const int vertical = lineLength(board, row, column, true);
      if (horizontal == cell || vertical == cell) {
        ++result.triggers;
      } else {
        const int horizontal_gap =
            std::abs(static_cast<int>(cell) - horizontal);
        const int vertical_gap = std::abs(static_cast<int>(cell) - vertical);
        result.reservoir_mass +=
            1.0 / (1.0 + static_cast<double>(std::min(horizontal_gap,
                                                      vertical_gap)));
      }

      if (cell == 1) {
        if (column + 1 < kBoardSize &&
            board[indexOf(row, column + 1)] == 1) {
          ++result.adjacent_ones;
        }
        if (row + 1 < kBoardSize && board[indexOf(row + 1, column)] == 1) {
          ++result.adjacent_ones;
        }
      }
      if (cell == 2) {
        if (column + 2 < kBoardSize &&
            board[indexOf(row, column + 1)] == 2 &&
            board[indexOf(row, column + 2)] == 2) {
          ++result.triple_twos;
        }
        if (row + 2 < kBoardSize &&
            board[indexOf(row + 1, column)] == 2 &&
            board[indexOf(row + 2, column)] == 2) {
          ++result.triple_twos;
        }
      }
    }
  }
  return result;
}

double targetError(const std::array<int, kBoardSize>& heights,
                   const Option& option) {
  double result = 0.0;
  for (int column = 0; column < kBoardSize; ++column) {
    result += std::abs(heights[column] -
                       static_cast<int>(option.target_heights[column]));
  }
  return result;
}

std::array<double, kFeatureCount> actionFeatures(
    const PublicState& state, const Option& option, int column,
    const BoardStats& before) {
  Board after_board = state.board;
  if (!placeDisc(after_board, column, state.next_disc)) {
    throw std::invalid_argument("features requested for illegal action");
  }
  const BoardStats after = analyzeBoard(after_board);
  const double target_before = targetError(before.heights, option);
  const double target_after = targetError(after.heights, option);
  const double early = static_cast<double>(state.moves_remaining) /
                       static_cast<double>(kMovesPerLevel);
  const double late = 1.0 - early + 1.0 / kMovesPerLevel;
  const double quiet = after.triggers == 0 ? 1.0 : 0.0;

  std::array<double, kFeatureCount> features{};
  features[kBias] = 1.0;
  features[kTargetErrorImprovement] =
      (target_before - target_after) / kBoardSize;
  features[kTargetHeadroom] =
      (static_cast<int>(option.target_heights[column]) -
       before.heights[column]) /
      static_cast<double>(kBoardSize);
  features[kReservoirImprovement] =
      (after.reservoir_mass - before.reservoir_mass) / kCellCount;
  features[kReservoirMass] = after.reservoir_mass / kCellCount;
  features[kImmediateTriggers] =
      static_cast<double>(after.triggers) / kBoardSize;
  features[kQuietBuild] = quiet * early;
  features[kLateRelease] = after.triggers * late / kBoardSize;
  features[kCoverFrontierImprovement] =
      static_cast<double>(after.cover_frontier - before.cover_frontier) /
      kCellCount;
  features[kCrackedFrontierImprovement] =
      static_cast<double>(after.cracked_frontier - before.cracked_frontier) /
      kCellCount;
  features[kOpenColumns] =
      static_cast<double>(after.open_columns) / kBoardSize;
  features[kMinimumTopSlack] =
      static_cast<double>(after.minimum_top_slack) / kBoardSize;
  features[kPeakHeight] = static_cast<double>(after.peak_height) / kBoardSize;
  features[kRoughnessImprovement] =
      static_cast<double>(before.roughness - after.roughness) /
      (kBoardSize * kBoardSize);
  features[kAdjacentOnes] =
      static_cast<double>(after.adjacent_ones) / kCellCount;
  features[kTripleTwos] =
      static_cast<double>(after.triple_twos) / kCellCount;
  features[kEdgeDistance] = std::abs(column - kBoardSize / 2) /
                            static_cast<double>(kBoardSize / 2);
  features[kLandingHeight] =
      static_cast<double>(after.heights[column]) / kBoardSize;
  return features;
}

double dot(const std::array<double, kFeatureCount>& features,
           const Option& option) {
  double result = 0.0;
  for (std::size_t index = 0; index < kFeatureCount; ++index) {
    result += features[index] * option.weights[index];
  }
  return result;
}

struct CanonicalInput {
  PublicState state{};
  Option option{};
  bool mirrored = false;
};

bool reflectedJointRepresentationIsSmaller(const PublicState& state,
                                            const Option& option) {
  const Board reflected_board = mirrorBoard(state.board);
  if (std::lexicographical_compare(reflected_board.begin(),
                                   reflected_board.end(), state.board.begin(),
                                   state.board.end())) {
    return true;
  }
  if (std::lexicographical_compare(state.board.begin(), state.board.end(),
                                   reflected_board.begin(),
                                   reflected_board.end())) {
    return false;
  }
  const auto reflected_target = reflectOption(option).target_heights;
  return std::lexicographical_compare(
      reflected_target.begin(), reflected_target.end(),
      option.target_heights.begin(), option.target_heights.end());
}

CanonicalInput canonicalize(const PublicState& state, const Option& source,
                            bool option_reflected) {
  validate(state);
  validate(source);
  CanonicalInput result{state,
                        option_reflected ? reflectOption(source) : source,
                        false};
  result.mirrored =
      reflectedJointRepresentationIsSmaller(result.state, result.option);
  if (result.mirrored) {
    result.state = mirror(result.state);
    result.option = reflectOption(result.option);
  }
  return result;
}

struct Decision {
  int action = -1;
  int legal_count = 0;
  bool canonical_mirrored = false;
  std::array<double, kBoardSize> values{};

  bool operator==(const Decision&) const = default;
};

Decision chooseAction(const PublicState& source, const Option& option,
                      bool option_reflected, WorkCounters& counters) {
  Decision result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  validate(source);
  validate(option);
  if (source.terminal) return result;

  const CanonicalInput canonical =
      canonicalize(source, option, option_reflected);
  const BoardStats before = analyzeBoard(canonical.state.board);
  double best = -std::numeric_limits<double>::infinity();
  int selected = -1;
  std::array<double, kBoardSize> canonical_values{};
  canonical_values.fill(-std::numeric_limits<double>::infinity());
  for (const int column : kColumnOrder) {
    if (!isLegal(canonical.state.board, column)) continue;
    const auto features =
        actionFeatures(canonical.state, canonical.option, column, before);
    const double value = dot(features, canonical.option);
    if (!std::isfinite(value)) {
      throw std::runtime_error("non-finite rise-option action value");
    }
    canonical_values[column] = value;
    ++result.legal_count;
    ++counters.legal_siblings_scored;
    ++counters.feature_extractions;
    if (selected < 0 || value > best) {
      selected = column;
      best = value;
    }
  }
  if (selected < 0) return result;

  result.canonical_mirrored = canonical.mirrored;
  if (!canonical.mirrored) {
    result.action = selected;
    result.values = canonical_values;
  } else {
    result.action = kBoardSize - 1 - selected;
    for (int column = 0; column < kBoardSize; ++column) {
      result.values[kBoardSize - 1 - column] = canonical_values[column];
    }
  }
  ++counters.decisions;
  return result;
}

using PublicPolicy = Decision (*)(const PublicState&, const Option&, bool,
                                  WorkCounters&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&, const Option&,
                                   bool, WorkCounters&>);

struct OptionLibrary {
  std::array<Option, kOptionCapacity> options{};
  std::uint8_t count = 0;

  bool operator==(const OptionLibrary&) const = default;

  void add(const Option& option) {
    validate(option);
    if (count >= kOptionCapacity) {
      throw std::runtime_error("rise-option library is full");
    }
    for (std::uint8_t index = 0; index < count; ++index) {
      if (options[index].id == option.id) {
        throw std::invalid_argument("duplicate rise-option id");
      }
    }
    options[count++] = option;
  }
};

struct ControllerState {
  std::uint8_t active_slot = kNoOption;
  bool active_reflected = false;
  std::uint8_t committed_decisions_remaining = 0;
  bool selection_open = true;
  bool bootstrapped = false;

  bool operator==(const ControllerState&) const = default;
};

struct Session {
  PublicState observation{};
  ControllerState controller{};

  bool operator==(const Session&) const = default;
};

Session beginSession(const PublicState& initial) {
  validate(initial);
  if (!initial.terminal && initial.moves_remaining != kMovesPerLevel) {
    throw std::invalid_argument(
        "a rise-option session must begin at a five-drop boundary");
  }
  Session result;
  result.observation = initial;
  result.controller.selection_open = !initial.terminal;
  return result;
}

void selectOption(Session& session, const OptionLibrary& library,
                  std::uint8_t slot, bool reflected) {
  if (session.observation.terminal || !session.controller.selection_open ||
      session.observation.moves_remaining != kMovesPerLevel ||
      slot >= library.count) {
    throw std::invalid_argument("rise option cannot be selected now");
  }
  session.controller.active_slot = slot;
  session.controller.active_reflected = reflected;
  session.controller.committed_decisions_remaining = kMovesPerLevel;
  session.controller.selection_open = false;
  session.controller.bootstrapped = true;
}

Decision chooseCommittedAction(Session& session, const OptionLibrary& library,
                               WorkCounters& counters) {
  const ControllerState& controller = session.controller;
  if (!controller.bootstrapped || controller.selection_open ||
      controller.active_slot >= library.count ||
      controller.committed_decisions_remaining == 0 ||
      controller.committed_decisions_remaining !=
          session.observation.moves_remaining) {
    throw std::logic_error("rise-option commitment is inconsistent");
  }
  return chooseAction(session.observation,
                      library.options[controller.active_slot],
                      controller.active_reflected, counters);
}

void observeTransition(Session& session, int action,
                       const PublicState& after) {
  validate(after);
  const PublicState before = session.observation;
  ControllerState& controller = session.controller;
  if (before.terminal || controller.selection_open ||
      controller.active_slot == kNoOption ||
      controller.committed_decisions_remaining != before.moves_remaining ||
      !isLegal(before.board, action)) {
    throw std::logic_error("invalid committed option transition");
  }

  if (after.terminal) {
    controller.committed_decisions_remaining = 0;
    controller.selection_open = false;
  } else if (before.moves_remaining > 1) {
    if (after.moves_remaining != before.moves_remaining - 1) {
      throw std::logic_error("option transition skipped a rise phase");
    }
    --controller.committed_decisions_remaining;
  } else {
    if (after.moves_remaining != kMovesPerLevel) {
      throw std::logic_error("option transition omitted the row rise");
    }
    controller.committed_decisions_remaining = 0;
    controller.selection_open = true;
  }
  session.observation = after;
}

std::uint64_t fnvByte(std::uint64_t hash, std::uint8_t byte) {
  hash ^= byte;
  return hash * 0x0000'0100'0000'01b3ull;
}

std::uint64_t optionFingerprint(const Option& option) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (int byte = 0; byte < 4; ++byte) {
    hash = fnvByte(hash, static_cast<std::uint8_t>(option.id >> (byte * 8)));
  }
  for (const std::uint8_t height : option.target_heights) {
    hash = fnvByte(hash, height);
  }
  for (const double weight : option.weights) {
    const std::uint64_t bits = std::bit_cast<std::uint64_t>(weight);
    for (int byte = 0; byte < 8; ++byte) {
      hash = fnvByte(
          hash, static_cast<std::uint8_t>(bits >> (static_cast<int>(byte) * 8)));
    }
  }
  return hash;
}

std::uint64_t mix64(std::uint64_t value) {
  value += 0x9e37'79b9'7f4a'7c15ull;
  value = (value ^ (value >> 30)) * 0xbf58'476d'1ce4'e5b9ull;
  value = (value ^ (value >> 27)) * 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31);
}

Option deterministicMutation(const Option& parent, std::uint64_t serial,
                             WorkCounters& counters) {
  validate(parent);
  if (serial == 0) {
    throw std::invalid_argument("mutation serial must be positive");
  }
  Option result = parent;
  std::uint64_t word = mix64(optionFingerprint(parent) ^ serial);
  constexpr std::size_t coordinates = kBoardSize + kFeatureCount;
  for (int edit = 0; edit < 3; ++edit) {
    word = mix64(word + static_cast<std::uint64_t>(edit + 1));
    const std::size_t coordinate = word % coordinates;
    const int direction = ((word >> 17) & 1u) == 0 ? -1 : 1;
    if (coordinate < kBoardSize) {
      int target = result.target_heights[coordinate] + direction;
      if (target < 0 || target > kBoardSize) {
        target = result.target_heights[coordinate] - direction;
      }
      result.target_heights[coordinate] =
          static_cast<std::uint8_t>(target);
    } else {
      const std::size_t feature = coordinate - kBoardSize;
      const double magnitude =
          0.125 * (1.0 + static_cast<double>((word >> 21) % 8u));
      result.weights[feature] = std::clamp(
          result.weights[feature] + direction * magnitude, -64.0, 64.0);
    }
  }
  result.id = static_cast<std::uint32_t>(
      mix64(optionFingerprint(result) ^ serial ^ 0x4d45'4c49'5445ull));
  ++counters.mutations;
  validate(result);
  return result;
}

struct Descriptor {
  std::uint8_t spread_bin = 0;
  std::uint8_t release_bin = 0;
  std::uint8_t edge_bin = 0;

  bool operator==(const Descriptor&) const = default;
};

std::size_t descriptorIndex(const Descriptor& descriptor) {
  if (descriptor.spread_bin >= kArchiveAxis ||
      descriptor.release_bin >= kArchiveAxis ||
      descriptor.edge_bin >= kArchiveAxis) {
    throw std::invalid_argument("MAP-Elites descriptor is out of range");
  }
  return (descriptor.spread_bin * kArchiveAxis + descriptor.release_bin) *
             kArchiveAxis +
         descriptor.edge_bin;
}

struct Elite {
  Option option{};
  Descriptor descriptor{};
  double synthetic_quality = 0.0;
  std::uint64_t fingerprint = 0;

  bool operator==(const Elite&) const = default;
};

struct Archive {
  std::array<std::optional<Elite>, kArchiveCells> cells{};
  std::size_t entries = 0;

  bool operator==(const Archive&) const = default;

  bool insert(Elite elite, WorkCounters& counters) {
    validate(elite.option);
    if (!std::isfinite(elite.synthetic_quality)) {
      throw std::invalid_argument("non-finite synthetic archive quality");
    }
    elite.fingerprint = optionFingerprint(elite.option);
    const std::size_t index = descriptorIndex(elite.descriptor);
    ++counters.archive_attempts;
    auto& incumbent = cells[index];
    if (!incumbent.has_value()) {
      incumbent = elite;
      ++entries;
      ++counters.archive_insertions;
      counters.peak_archive_entries =
          std::max(counters.peak_archive_entries,
                   static_cast<std::uint64_t>(entries));
      return true;
    }
    const bool replace =
        elite.synthetic_quality > incumbent->synthetic_quality ||
        (elite.synthetic_quality == incumbent->synthetic_quality &&
         elite.fingerprint < incumbent->fingerprint);
    if (replace) {
      incumbent = elite;
      ++counters.archive_replacements;
    }
    return replace;
  }
};

struct SyntheticTrace {
  std::array<int, kMovesPerLevel> actions{};
  int action_count = 0;
  int distinct_columns = 0;
  int edge_actions = 0;
  int trigger_placements = 0;
  int peak_height = 0;
  PublicState final_state{};

  bool operator==(const SyntheticTrace&) const = default;
};

PublicState syntheticStructuralStep(const PublicState& source, int action,
                                    bool& triggered) {
  validate(source);
  if (source.terminal || !isLegal(source.board, action)) {
    throw std::invalid_argument("illegal synthetic structural step");
  }
  PublicState result = source;
  if (!placeDisc(result.board, action, result.next_disc)) {
    throw std::logic_error("legal synthetic placement failed");
  }
  int popper_count = 0;
  (void)findPoppers(result.board, popper_count);
  triggered = popper_count > 0;

  if (source.moves_remaining > 1) {
    result.moves_remaining = source.moves_remaining - 1;
  } else {
    Board raised{};
    if (!raiseCoveredRow(result.board, raised)) {
      result.terminal = true;
      result.moves_remaining = 0;
    } else {
      result.board = raised;
      result.moves_remaining = kMovesPerLevel;
    }
  }
  result.next_disc = 7;  // Literal fixture observation, not a sampled disc.
  int legal_count = 0;
  (void)legalColumns(result.board, legal_count);
  if (legal_count == 0) result.terminal = true;
  return result;
}

SyntheticTrace syntheticTrajectory(const PublicState& initial,
                                    const OptionLibrary& library,
                                    std::uint8_t slot, bool reflected,
                                    WorkCounters& counters) {
  Session session = beginSession(initial);
  selectOption(session, library, slot, reflected);
  SyntheticTrace trace;
  std::array<bool, kBoardSize> used{};
  for (int step = 0; step < kMovesPerLevel; ++step) {
    const Decision decision = chooseCommittedAction(session, library, counters);
    if (decision.action < 0 ||
        !isLegal(session.observation.board, decision.action)) {
      throw std::runtime_error("synthetic option selected an illegal action");
    }
    trace.actions[trace.action_count++] = decision.action;
    used[decision.action] = true;
    trace.edge_actions += decision.action == 0 || decision.action == 6;
    bool triggered = false;
    const PublicState after = syntheticStructuralStep(
        session.observation, decision.action, triggered);
    trace.trigger_placements += triggered;
    trace.peak_height =
        std::max(trace.peak_height, analyzeBoard(after.board).peak_height);
    observeTransition(session, decision.action, after);
    ++counters.synthetic_steps;
  }
  trace.distinct_columns =
      static_cast<int>(std::count(used.begin(), used.end(), true));
  trace.final_state = session.observation;
  if (!trace.final_state.terminal && !session.controller.selection_open) {
    throw std::logic_error("synthetic cycle did not reopen option selection");
  }
  return trace;
}

Elite syntheticElite(const Option& option, const SyntheticTrace& trace) {
  Elite result;
  result.option = option;
  result.descriptor.spread_bin = static_cast<std::uint8_t>(
      std::min<int>(kArchiveAxis - 1, trace.distinct_columns));
  result.descriptor.release_bin = static_cast<std::uint8_t>(
      std::min<int>(kArchiveAxis - 1, trace.trigger_placements));
  result.descriptor.edge_bin = static_cast<std::uint8_t>(
      std::min<int>(kArchiveAxis - 1, trace.edge_actions));
  result.synthetic_quality = 10.0 * trace.distinct_columns -
                             2.0 * trace.trigger_placements -
                             static_cast<double>(trace.peak_height);
  result.fingerprint = optionFingerprint(option);
  return result;
}

Option makeBuilderOption() {
  Option result;
  result.id = 0x4255'494cu;
  result.target_heights = {{1, 2, 4, 7, 4, 2, 1}};
  result.weights[kTargetErrorImprovement] = 4.0;
  result.weights[kTargetHeadroom] = 8.0;
  result.weights[kReservoirImprovement] = 5.0;
  result.weights[kReservoirMass] = 1.0;
  result.weights[kImmediateTriggers] = -5.0;
  result.weights[kQuietBuild] = 7.0;
  result.weights[kLateRelease] = 2.0;
  result.weights[kCoverFrontierImprovement] = 3.0;
  result.weights[kCrackedFrontierImprovement] = 2.0;
  result.weights[kOpenColumns] = 2.0;
  result.weights[kMinimumTopSlack] = 4.0;
  result.weights[kPeakHeight] = -3.0;
  result.weights[kRoughnessImprovement] = 2.0;
  result.weights[kAdjacentOnes] = -10.0;
  result.weights[kTripleTwos] = -12.0;
  result.weights[kEdgeDistance] = -1.0;
  return result;
}

Option makeEdgeReservoirOption() {
  Option result;
  result.id = 0x4544'4745u;
  result.target_heights = {{7, 6, 4, 3, 2, 1, 1}};
  result.weights[kTargetErrorImprovement] = 3.0;
  result.weights[kTargetHeadroom] = 10.0;
  result.weights[kReservoirImprovement] = 4.0;
  result.weights[kImmediateTriggers] = -3.0;
  result.weights[kQuietBuild] = 5.0;
  result.weights[kLateRelease] = 4.0;
  result.weights[kOpenColumns] = 1.0;
  result.weights[kMinimumTopSlack] = 2.0;
  result.weights[kPeakHeight] = -1.0;
  result.weights[kAdjacentOnes] = -10.0;
  result.weights[kTripleTwos] = -12.0;
  result.weights[kEdgeDistance] = 3.0;
  return result;
}

OptionLibrary defaultLibrary() {
  OptionLibrary result;
  result.add(makeBuilderOption());
  result.add(makeEdgeReservoirOption());
  return result;
}

struct Prototype {
  OptionLibrary library{};
  Session session{};
  Archive archive{};
  WorkCounters counters{};

  bool operator==(const Prototype&) const = default;
};

std::size_t estimatedMemoryBytes(const Prototype&) {
  return sizeof(Prototype);
}

void validate(const Prototype& prototype) {
  if (prototype.library.count == 0 ||
      prototype.library.count > kOptionCapacity) {
    throw std::invalid_argument("invalid rise-option library size");
  }
  for (std::uint8_t index = 0; index < prototype.library.count; ++index) {
    validate(prototype.library.options[index]);
  }
  validate(prototype.session.observation);
  const ControllerState& controller = prototype.session.controller;
  if (controller.committed_decisions_remaining > kMovesPerLevel ||
      (!controller.bootstrapped &&
       (controller.active_slot != kNoOption ||
        controller.committed_decisions_remaining != 0)) ||
      (controller.selection_open &&
       (controller.committed_decisions_remaining != 0 ||
        prototype.session.observation.moves_remaining != kMovesPerLevel)) ||
      (prototype.session.observation.terminal &&
       controller.selection_open)) {
    throw std::invalid_argument("checkpoint has invalid controller state");
  }
  if (controller.bootstrapped && controller.active_slot >=
                                     prototype.library.count) {
    throw std::invalid_argument("checkpoint has invalid active option");
  }
  if (!controller.selection_open && !prototype.session.observation.terminal &&
      controller.committed_decisions_remaining !=
          prototype.session.observation.moves_remaining) {
    throw std::invalid_argument("checkpoint commitment phase mismatch");
  }
  std::size_t entries = 0;
  for (std::size_t index = 0; index < kArchiveCells; ++index) {
    if (!prototype.archive.cells[index].has_value()) continue;
    ++entries;
    const Elite& elite = *prototype.archive.cells[index];
    validate(elite.option);
    if (descriptorIndex(elite.descriptor) != index ||
        !std::isfinite(elite.synthetic_quality) ||
        elite.fingerprint != optionFingerprint(elite.option)) {
      throw std::invalid_argument("checkpoint has invalid archive elite");
    }
  }
  if (entries != prototype.archive.entries ||
      prototype.counters.peak_archive_entries < entries ||
      prototype.counters.work() > kMaximumWork ||
      estimatedMemoryBytes(prototype) > kMaximumBytes) {
    throw std::invalid_argument("rise-option resource accounting failed");
  }
}

void putU8(std::vector<std::uint8_t>& output, std::uint8_t value) {
  output.push_back(value);
}

void putU32(std::vector<std::uint8_t>& output, std::uint32_t value) {
  for (int byte = 0; byte < 4; ++byte) {
    putU8(output, static_cast<std::uint8_t>(value >> (byte * 8)));
  }
}

void putU64(std::vector<std::uint8_t>& output, std::uint64_t value) {
  for (int byte = 0; byte < 8; ++byte) {
    putU8(output, static_cast<std::uint8_t>(value >> (byte * 8)));
  }
}

void putDouble(std::vector<std::uint8_t>& output, double value) {
  putU64(output, std::bit_cast<std::uint64_t>(value));
}

class Reader {
 public:
  explicit Reader(std::string_view bytes)
      : data_(reinterpret_cast<const std::uint8_t*>(bytes.data())),
        size_(bytes.size()) {}

  std::uint8_t u8() {
    require(1);
    return data_[position_++];
  }

  std::uint32_t u32() {
    std::uint32_t value = 0;
    for (int byte = 0; byte < 4; ++byte) {
      value |= static_cast<std::uint32_t>(u8()) << (byte * 8);
    }
    return value;
  }

  std::uint64_t u64() {
    std::uint64_t value = 0;
    for (int byte = 0; byte < 8; ++byte) {
      value |= static_cast<std::uint64_t>(u8()) << (byte * 8);
    }
    return value;
  }

  double number() { return std::bit_cast<double>(u64()); }

  std::size_t remaining() const { return size_ - position_; }

  std::string_view take(std::size_t count) {
    require(count);
    const char* begin = reinterpret_cast<const char*>(data_ + position_);
    position_ += count;
    return {begin, count};
  }

 private:
  void require(std::size_t count) const {
    if (count > size_ - position_) {
      throw std::runtime_error("truncated rise-option checkpoint");
    }
  }

  const std::uint8_t* data_ = nullptr;
  std::size_t size_ = 0;
  std::size_t position_ = 0;
};

bool readBool(Reader& input) {
  const std::uint8_t value = input.u8();
  if (value > 1) {
    throw std::runtime_error("invalid checkpoint boolean");
  }
  return value != 0;
}

std::uint64_t checksum(std::string_view bytes) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const unsigned char byte : bytes) hash = fnvByte(hash, byte);
  return hash;
}

void writeOption(std::vector<std::uint8_t>& output, const Option& option) {
  putU32(output, option.id);
  for (const std::uint8_t height : option.target_heights) {
    putU8(output, height);
  }
  for (const double weight : option.weights) putDouble(output, weight);
}

Option readOption(Reader& input) {
  Option result;
  result.id = input.u32();
  for (std::uint8_t& height : result.target_heights) height = input.u8();
  for (double& weight : result.weights) weight = input.number();
  validate(result);
  return result;
}

void writePublicState(std::vector<std::uint8_t>& output,
                      const PublicState& state) {
  for (const std::uint8_t cell : state.board) putU8(output, cell);
  putU8(output, state.next_disc);
  putU8(output, state.moves_remaining);
  putU8(output, state.terminal ? 1 : 0);
}

PublicState readPublicState(Reader& input) {
  PublicState result;
  for (std::uint8_t& cell : result.board) cell = input.u8();
  result.next_disc = input.u8();
  result.moves_remaining = input.u8();
  result.terminal = readBool(input);
  validate(result);
  return result;
}

void writeCounters(std::vector<std::uint8_t>& output,
                   const WorkCounters& counters) {
  putU64(output, counters.decisions);
  putU64(output, counters.legal_siblings_scored);
  putU64(output, counters.feature_extractions);
  putU64(output, counters.synthetic_steps);
  putU64(output, counters.archive_attempts);
  putU64(output, counters.archive_insertions);
  putU64(output, counters.archive_replacements);
  putU64(output, counters.mutations);
  putU64(output, counters.peak_archive_entries);
}

WorkCounters readCounters(Reader& input) {
  WorkCounters result;
  result.decisions = input.u64();
  result.legal_siblings_scored = input.u64();
  result.feature_extractions = input.u64();
  result.synthetic_steps = input.u64();
  result.archive_attempts = input.u64();
  result.archive_insertions = input.u64();
  result.archive_replacements = input.u64();
  result.mutations = input.u64();
  result.peak_archive_entries = input.u64();
  return result;
}

std::string writeCheckpoint(const Prototype& prototype) {
  validate(prototype);
  std::vector<std::uint8_t> payload;
  putU8(payload, prototype.library.count);
  for (std::uint8_t index = 0; index < prototype.library.count; ++index) {
    writeOption(payload, prototype.library.options[index]);
  }
  writePublicState(payload, prototype.session.observation);
  const ControllerState& controller = prototype.session.controller;
  putU8(payload, controller.active_slot);
  putU8(payload, controller.active_reflected ? 1 : 0);
  putU8(payload, controller.committed_decisions_remaining);
  putU8(payload, controller.selection_open ? 1 : 0);
  putU8(payload, controller.bootstrapped ? 1 : 0);
  writeCounters(payload, prototype.counters);
  putU8(payload, static_cast<std::uint8_t>(prototype.archive.entries));
  for (std::size_t index = 0; index < kArchiveCells; ++index) {
    if (!prototype.archive.cells[index].has_value()) continue;
    const Elite& elite = *prototype.archive.cells[index];
    putU8(payload, static_cast<std::uint8_t>(index));
    putU8(payload, elite.descriptor.spread_bin);
    putU8(payload, elite.descriptor.release_bin);
    putU8(payload, elite.descriptor.edge_bin);
    putDouble(payload, elite.synthetic_quality);
    putU64(payload, elite.fingerprint);
    writeOption(payload, elite.option);
  }

  const std::string_view payload_view(
      reinterpret_cast<const char*>(payload.data()), payload.size());
  std::vector<std::uint8_t> output;
  putU64(output, kCheckpointMagic);
  putU32(output, kCheckpointVersion);
  putU32(output, static_cast<std::uint32_t>(payload.size()));
  putU64(output, checksum(payload_view));
  output.insert(output.end(), payload.begin(), payload.end());
  return {reinterpret_cast<const char*>(output.data()), output.size()};
}

Prototype readCheckpoint(std::string_view bytes) {
  Reader header(bytes);
  const std::uint64_t magic = header.u64();
  const std::uint32_t version = header.u32();
  const std::uint32_t payload_size = header.u32();
  const std::uint64_t expected_checksum = header.u64();
  if (magic != kCheckpointMagic || version != kCheckpointVersion ||
      header.remaining() != payload_size) {
    throw std::runtime_error("invalid rise-option checkpoint header");
  }
  const std::string_view payload = header.take(payload_size);
  if (checksum(payload) != expected_checksum) {
    throw std::runtime_error("rise-option checkpoint checksum mismatch");
  }

  Reader input(payload);
  Prototype result;
  const std::uint8_t library_count = input.u8();
  if (library_count == 0 || library_count > kOptionCapacity) {
    throw std::runtime_error("invalid checkpoint library count");
  }
  for (std::uint8_t index = 0; index < library_count; ++index) {
    result.library.add(readOption(input));
  }
  result.session.observation = readPublicState(input);
  ControllerState& controller = result.session.controller;
  controller.active_slot = input.u8();
  controller.active_reflected = readBool(input);
  controller.committed_decisions_remaining = input.u8();
  controller.selection_open = readBool(input);
  controller.bootstrapped = readBool(input);
  result.counters = readCounters(input);
  const std::uint8_t entries = input.u8();
  if (entries > kArchiveCells) {
    throw std::runtime_error("invalid checkpoint archive count");
  }
  for (std::uint8_t entry = 0; entry < entries; ++entry) {
    const std::size_t index = input.u8();
    Elite elite;
    elite.descriptor.spread_bin = input.u8();
    elite.descriptor.release_bin = input.u8();
    elite.descriptor.edge_bin = input.u8();
    elite.synthetic_quality = input.number();
    elite.fingerprint = input.u64();
    elite.option = readOption(input);
    if (index >= kArchiveCells ||
        descriptorIndex(elite.descriptor) != index ||
        result.archive.cells[index].has_value()) {
      throw std::runtime_error("invalid checkpoint archive slot");
    }
    result.archive.cells[index] = elite;
    ++result.archive.entries;
  }
  if (input.remaining() != 0) {
    throw std::runtime_error("trailing rise-option checkpoint payload");
  }
  validate(result);
  return result;
}

struct AccessAudit {
  std::uint64_t gameplay_seed_attempts = 0;
  std::uint64_t corpus_attempts = 0;
};

void guardArgument(std::string_view argument, AccessAudit& audit) {
  if (argument.find("seed") != std::string_view::npos ||
      argument.find("replay") != std::string_view::npos ||
      argument.find("game") != std::string_view::npos) {
    ++audit.gameplay_seed_attempts;
    throw std::invalid_argument(
        "B0 forbids gameplay seeds and replay arguments");
  }
  if (argument.find("corpus") != std::string_view::npos ||
      argument.find("train") != std::string_view::npos) {
    ++audit.corpus_attempts;
    throw std::invalid_argument("B0 forbids corpora and production training");
  }
  if (argument != "--selftest") {
    throw std::invalid_argument("usage: drop7_rise_option_qd --selftest");
  }
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Callable>
bool throws(Callable&& callable) {
  try {
    std::forward<Callable>(callable)();
  } catch (const std::exception&) {
    return true;
  }
  return false;
}

PublicState syntheticInitialState() {
  PublicState result;
  result.board = initialBoard();
  result.next_disc = 7;
  result.moves_remaining = kMovesPerLevel;
  return result;
}

bool allActionsLegalAlongTrace(const PublicState& initial,
                               const SyntheticTrace& trace) {
  PublicState state = initial;
  for (int index = 0; index < trace.action_count; ++index) {
    const int action = trace.actions[index];
    if (!isLegal(state.board, action)) return false;
    bool ignored = false;
    state = syntheticStructuralStep(state, action, ignored);
  }
  return true;
}

struct SelfTestReport {
  SyntheticTrace builder_trace{};
  SyntheticTrace edge_trace{};
  std::size_t checkpoint_bytes = 0;
  std::size_t estimated_bytes = 0;
  std::size_t archive_entries = 0;
  WorkCounters counters{};
};

SelfTestReport runSelfTest() {
  const OptionLibrary library = defaultLibrary();
  const PublicState initial = syntheticInitialState();

  State metadata;
  metadata.board = initial.board;
  metadata.next_disc = initial.next_disc;
  metadata.moves_remaining = initial.moves_remaining;
  metadata.score = 9'999'999;
  metadata.level = 99;
  metadata.moves_played = 777;
  expect(publicState(metadata) == initial,
         "public boundary leaked engine metadata");

  WorkCounters trajectory_work;
  const SyntheticTrace builder =
      syntheticTrajectory(initial, library, 0, false, trajectory_work);
  const SyntheticTrace edge =
      syntheticTrajectory(initial, library, 1, false, trajectory_work);
  expect(builder.actions != edge.actions,
         "distinct options produced the same synthetic trajectory");
  expect(allActionsLegalAlongTrace(initial, builder) &&
             allActionsLegalAlongTrace(initial, edge),
         "synthetic option trajectory was not legal");
  expect(builder.action_count == kMovesPerLevel &&
             edge.action_count == kMovesPerLevel &&
             builder.final_state.moves_remaining == kMovesPerLevel &&
             edge.final_state.moves_remaining == kMovesPerLevel,
         "synthetic trajectory did not span exactly one rise cycle");

  WorkCounters full_sibling_work;
  const Decision complete =
      chooseAction(initial, library.options[0], false, full_sibling_work);
  int legal_count = 0;
  (void)legalColumns(initial.board, legal_count);
  expect(complete.legal_count == legal_count &&
             full_sibling_work.legal_siblings_scored ==
                 static_cast<std::uint64_t>(legal_count),
         "policy did not score every legal sibling");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(std::isfinite(complete.values[column]) ==
               isLegal(initial.board, column),
           "legal sibling mask was incorrect");
  }

  PublicState asymmetric = initial;
  asymmetric.board[indexOf(5, 1)] = 6;
  asymmetric.board[indexOf(4, 1)] = 5;
  WorkCounters reflection_work;
  const Decision forward =
      chooseAction(asymmetric, library.options[1], false, reflection_work);
  const Decision reflected = chooseAction(mirror(asymmetric),
                                          library.options[1], true,
                                          reflection_work);
  expect(reflected.action == kBoardSize - 1 - forward.action,
         "joint state/option action reflection failed");
  for (int column = 0; column < kBoardSize; ++column) {
    expect(reflected.values[kBoardSize - 1 - column] ==
               forward.values[column],
           "joint reflected sibling value failed");
  }
  WorkCounters symmetric_reflection_work;
  const Decision symmetric_forward =
      chooseAction(initial, library.options[1], false,
                   symmetric_reflection_work);
  const Decision symmetric_reflected =
      chooseAction(initial, library.options[1], true,
                   symmetric_reflection_work);
  expect(symmetric_reflected.action ==
             kBoardSize - 1 - symmetric_forward.action,
         "symmetric-board option orientation reflection failed");

  Session lifecycle = beginSession(initial);
  selectOption(lifecycle, library, 0, false);
  WorkCounters lifecycle_work;
  for (int step = 0; step < kMovesPerLevel; ++step) {
    expect(lifecycle.controller.active_slot == 0 &&
               lifecycle.controller.committed_decisions_remaining ==
                   kMovesPerLevel - step,
           "active option did not remain committed");
    expect(throws([&] { selectOption(lifecycle, library, 1, false); }),
           "mid-cycle option switch was accepted");
    const Decision decision =
        chooseCommittedAction(lifecycle, library, lifecycle_work);
    bool ignored = false;
    const PublicState after = syntheticStructuralStep(
        lifecycle.observation, decision.action, ignored);
    observeTransition(lifecycle, decision.action, after);
  }
  expect(lifecycle.controller.selection_open,
         "row rise did not reopen option selection");
  selectOption(lifecycle, library, 1, false);
  expect(lifecycle.controller.active_slot == 1 &&
             lifecycle.controller.committed_decisions_remaining ==
                 kMovesPerLevel,
         "post-rise option switch failed");

  WorkCounters archive_work;
  Archive archive;
  const Elite builder_elite = syntheticElite(library.options[0], builder);
  const Elite edge_elite = syntheticElite(library.options[1], edge);
  expect(archive.insert(builder_elite, archive_work),
         "first MAP-Elites insertion failed");
  Elite worse = builder_elite;
  worse.synthetic_quality -= 1.0;
  expect(!archive.insert(worse, archive_work),
         "worse archive collision replaced incumbent");
  Elite better = builder_elite;
  better.synthetic_quality += 1.0;
  expect(archive.insert(better, archive_work),
         "better archive collision did not replace incumbent");
  (void)archive.insert(edge_elite, archive_work);
  WorkCounters mutation_work_a;
  WorkCounters mutation_work_b;
  const Option mutation_a =
      deterministicMutation(library.options[0], 17, mutation_work_a);
  const Option mutation_b =
      deterministicMutation(library.options[0], 17, mutation_work_b);
  expect(mutation_a == mutation_b && mutation_a != library.options[0] &&
             mutation_work_a == mutation_work_b,
         "deterministic mutation was not reproducible");
  OptionLibrary mutated_library = library;
  mutated_library.add(mutation_a);
  WorkCounters mutation_fixture_work;
  const SyntheticTrace mutation_trace = syntheticTrajectory(
      initial, mutated_library, 2, false, mutation_fixture_work);
  const Elite mutation_elite = syntheticElite(mutation_a, mutation_trace);
  (void)archive.insert(mutation_elite, archive_work);

  Archive reordered_archive;
  WorkCounters reordered_work;
  (void)reordered_archive.insert(worse, reordered_work);
  (void)reordered_archive.insert(edge_elite, reordered_work);
  (void)reordered_archive.insert(mutation_elite, reordered_work);
  (void)reordered_archive.insert(builder_elite, reordered_work);
  (void)reordered_archive.insert(better, reordered_work);
  expect(reordered_archive == archive,
         "MAP-Elites insertion order changed the deterministic archive");

  Prototype prototype;
  prototype.library = library;
  prototype.archive = archive;
  prototype.counters = trajectory_work;
  prototype.counters.archive_attempts += archive_work.archive_attempts;
  prototype.counters.archive_insertions += archive_work.archive_insertions;
  prototype.counters.archive_replacements += archive_work.archive_replacements;
  prototype.counters.peak_archive_entries = archive_work.peak_archive_entries;
  prototype.counters.mutations += mutation_work_a.mutations;
  prototype.counters.decisions += mutation_fixture_work.decisions;
  prototype.counters.legal_siblings_scored +=
      mutation_fixture_work.legal_siblings_scored;
  prototype.counters.feature_extractions +=
      mutation_fixture_work.feature_extractions;
  prototype.counters.synthetic_steps += mutation_fixture_work.synthetic_steps;
  prototype.session = beginSession(initial);
  selectOption(prototype.session, prototype.library, 1, true);
  for (int step = 0; step < 2; ++step) {
    const Decision decision = chooseCommittedAction(
        prototype.session, prototype.library, prototype.counters);
    bool ignored = false;
    const PublicState after = syntheticStructuralStep(
        prototype.session.observation, decision.action, ignored);
    observeTransition(prototype.session, decision.action, after);
    ++prototype.counters.synthetic_steps;
  }
  expect(prototype.session.controller.active_slot == 1 &&
             prototype.session.controller.active_reflected &&
             prototype.session.controller.committed_decisions_remaining == 3,
         "checkpoint fixture lost active option before serialization");
  const std::string checkpoint = writeCheckpoint(prototype);
  const Prototype restored = readCheckpoint(checkpoint);
  expect(restored == prototype,
         "checkpoint did not round-trip the active option/session");
  Prototype uninterrupted = prototype;
  Prototype resumed = restored;
  const Decision next_uninterrupted = chooseCommittedAction(
      uninterrupted.session, uninterrupted.library, uninterrupted.counters);
  const Decision next_resumed = chooseCommittedAction(
      resumed.session, resumed.library, resumed.counters);
  expect(next_uninterrupted == next_resumed &&
             uninterrupted.counters == resumed.counters,
         "checkpoint resume changed the next option decision");
  std::string corrupt = checkpoint;
  corrupt.back() = static_cast<char>(corrupt.back() ^ 1);
  expect(throws([&] { (void)readCheckpoint(corrupt); }),
         "corrupt checkpoint was accepted");
  expect(throws([&] { (void)readCheckpoint(checkpoint + "x"); }),
         "checkpoint with trailing bytes was accepted");
  Prototype non_finite = prototype;
  non_finite.library.options[0].weights[0] =
      std::numeric_limits<double>::quiet_NaN();
  expect(throws([&] { (void)writeCheckpoint(non_finite); }),
         "non-finite option checkpoint was accepted");
  Prototype over_work = prototype;
  over_work.counters.legal_siblings_scored = kMaximumWork + 1;
  expect(throws([&] { validate(over_work); }),
         "over-budget work counter was accepted");

  AccessAudit argument_probe;
  expect(throws([&] { guardArgument("--seed", argument_probe); }) &&
             argument_probe.gameplay_seed_attempts == 1,
         "gameplay-seed argument firewall failed");
  expect(throws([&] { guardArgument("--corpus", argument_probe); }) &&
             argument_probe.corpus_attempts == 1,
         "corpus argument firewall failed");
  expect(prototype.counters.work() <= kMaximumWork &&
             estimatedMemoryBytes(prototype) <= kMaximumBytes,
         "B0 work or memory bound failed");

  return {builder,
          edge,
          checkpoint.size(),
          estimatedMemoryBytes(prototype),
          archive.entries,
          prototype.counters};
}

void writeActions(std::ostream& output,
                  const std::array<int, kMovesPerLevel>& actions) {
  output << '[';
  for (int index = 0; index < kMovesPerLevel; ++index) {
    if (index != 0) output << ',';
    output << actions[index];
  }
  output << ']';
}

}  // namespace drop7::rise_option_qd

int main(int argc, char** argv) {
  using namespace drop7::rise_option_qd;
  AccessAudit audit;
  try {
    if (argc != 2) {
      throw std::invalid_argument("usage: drop7_rise_option_qd --selftest");
    }
    guardArgument(argv[1], audit);
    const SelfTestReport report = runSelfTest();
    std::cout << "RISE_OPTION_QD_B0 {\"passed\":true,"
              << "\"syntheticOnly\":true,\"productionTraining\":false,"
              << "\"gameplaySeedAccesses\":"
              << audit.gameplay_seed_attempts << ",\"corpusAccesses\":"
              << audit.corpus_attempts << ",\"builderActions\":";
    writeActions(std::cout, report.builder_trace.actions);
    std::cout << ",\"edgeActions\":";
    writeActions(std::cout, report.edge_trace.actions);
    std::cout << ",\"archiveEntries\":" << report.archive_entries
              << ",\"checkpointBytes\":" << report.checkpoint_bytes
              << ",\"estimatedMemoryBytes\":" << report.estimated_bytes
              << ",\"work\":" << report.counters.work()
              << ",\"legalSiblingsScored\":"
              << report.counters.legal_siblings_scored << "}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "rise-option QD B0 failure: " << error.what() << '\n';
    return 1;
  }
}
