#include "../../../src/core/native/engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using drop7::Board;
using drop7::MoveResult;
using drop7::State;

constexpr std::uint32_t kTrainingSeedStart = 0x3d70'0000u;
constexpr std::uint32_t kProbeSeedStart = 0x4d70'0000u;
constexpr std::uint32_t kTeacherDomain = 0x5445'4143u;  // "TEAC"
constexpr std::uint32_t kTapeDomain = 0x5441'5045u;     // "TAPE"
constexpr std::uint32_t kTapeRevealDomain = 0x5452'4556u;
constexpr std::uint32_t kTapeDiscDomain = 0x5444'4953u;
constexpr int kMaximumHorizon = 50;
constexpr int kMaximumTapes = 64;
constexpr int kMaximumBeamPerAction = 64;

struct TeacherOptions {
  int horizon = 25;
  int tapes = 14;
  int beam_per_action = 8;
  bool vote_aggregate = false;
  double leaf_scale = 1.0;
  double terminal_penalty = 1'000'000'000.0;
};

struct RunOptions {
  int games = 4;
  int max_moves = 1000;
  std::string range = "train";
  std::uint32_t seed_start = kTrainingSeedStart;
  TeacherOptions teacher;
};

struct TeacherStats {
  std::uint64_t generated_states = 0;
  std::uint64_t deduplicated_states = 0;
  std::size_t peak_candidates = 0;
  std::size_t peak_retained = 0;
};

struct TeacherDecision {
  int column = -1;
  std::array<double, drop7::kBoardSize> mean_values{};
  std::array<int, drop7::kBoardSize> observations{};
};

struct BeamNode {
  State state{};
  double rank = -std::numeric_limits<double>::infinity();
};

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int level = 1;
  bool censored = false;
  TeacherStats stats{};
  double seconds = 0;
};

std::string valueAfter(int argc, char** argv, std::string_view flag,
                       std::string fallback = {}) {
  for (int index = 1; index + 1 < argc; ++index) {
    if (argv[index] == flag) return argv[index + 1];
  }
  return fallback;
}

bool hasFlag(int argc, char** argv, std::string_view flag) {
  for (int index = 1; index < argc; ++index) {
    if (argv[index] == flag) return true;
  }
  return false;
}

int parsePositive(const std::string& value, std::string_view name) {
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const long long parsed = std::stoll(value, &consumed, 10);
  if (consumed != value.size() || parsed < 1 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

double parsePositiveDouble(const std::string& value, std::string_view name) {
  if (value.empty()) throw std::invalid_argument(std::string(name) + " is required");
  std::size_t consumed = 0;
  const double parsed = std::stod(value, &consumed);
  if (consumed != value.size() || !std::isfinite(parsed) || parsed <= 0) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return parsed;
}

Board mirrorBoard(const Board& board) {
  Board mirrored{};
  for (int row = 0; row < drop7::kBoardSize; ++row) {
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      mirrored[drop7::indexOf(row, drop7::kBoardSize - 1 - column)] =
          board[drop7::indexOf(row, column)];
    }
  }
  return mirrored;
}

State canonicalState(const State& state, bool& mirrored) {
  const Board reflected = mirrorBoard(state.board);
  mirrored = std::lexicographical_compare(
      reflected.begin(), reflected.end(), state.board.begin(), state.board.end());
  if (!mirrored) return state;
  State result = state;
  result.board = reflected;
  return result;
}

std::uint32_t observableHash(const State& canonical) {
  // Only mechanics-relevant public state participates. In particular, no
  // environment seed, future tape, score, level, or move index can leak in.
  std::uint32_t hash = 0x811c'9dc5u;
  for (std::uint8_t cell : canonical.board) {
    hash ^= static_cast<std::uint32_t>(cell + 1u);
    hash *= 0x0100'0193u;
  }
  hash ^= static_cast<std::uint32_t>(canonical.next_disc) * 0x9e37'79b9u;
  hash ^= static_cast<std::uint32_t>(canonical.moves_remaining) * 0x85eb'ca6bu;
  return drop7::mix32(hash ^ kTeacherDomain);
}

std::uint32_t tapeKey(std::uint32_t root_hash, int tape) {
  return drop7::mix32(
      root_hash ^ kTapeDomain ^
      (static_cast<std::uint32_t>(tape + 1) * 0x9e37'79b9u));
}

std::uint32_t tapeRevealSeed(std::uint32_t tape_key, int ply) {
  return drop7::mix32(
      tape_key ^ kTapeRevealDomain ^
      (static_cast<std::uint32_t>(ply + 1) * 0x85eb'ca6bu));
}

std::uint8_t tapeDisc(std::uint32_t tape_key, int ply) {
  const std::uint32_t bits = drop7::mix32(
      tape_key ^ kTapeDiscDomain ^
      (static_cast<std::uint32_t>(ply + 1) * 0x27d4'eb2du));
  return static_cast<std::uint8_t>(
      ((static_cast<std::uint64_t>(bits) * 7u) >> 32) + 1u);
}

std::array<int, drop7::kBoardSize> columnHeights(const Board& board) {
  std::array<int, drop7::kBoardSize> heights{};
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    for (int row = 0; row < drop7::kBoardSize; ++row) {
      if (board[drop7::indexOf(row, column)] != drop7::kEmpty) {
        ++heights[column];
      }
    }
  }
  return heights;
}

double readiness(int cost) {
  return cost >= 1 ? std::ldexp(1.0, 1 - cost) : 0.0;
}

double unionReadiness(double first, double second) {
  return 1.0 - (1.0 - first) * (1.0 - second);
}

double observableLeafUtility(const State& state) {
  if (state.game_over) return -250'000.0;
  const Board& board = state.board;
  const auto heights = columnHeights(board);
  int occupied = 0;
  int covers = 0;
  int solid = 0;
  int cracked = 0;
  int numbered = 0;
  int open_columns = 0;
  int high_low = 0;
  int maximum_height = 0;
  double height_load = 0;
  double cover_altitude = 0;
  double direct_potential = 0;
  double low_cap_load = 0;
  double adjacent_low_cap_load = 0;
  std::array<bool, drop7::kBoardSize> low_caps{};

  for (int column = 0; column < drop7::kBoardSize; ++column) {
    if (board[column] == drop7::kEmpty) ++open_columns;
    maximum_height = std::max(maximum_height, heights[column]);
  }

  for (int row = 0; row < drop7::kBoardSize; ++row) {
    const int elevation = drop7::kBoardSize - row;
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      const std::uint8_t cell = board[drop7::indexOf(row, column)];
      if (cell == drop7::kEmpty) continue;
      ++occupied;
      height_load += elevation * elevation;
      if (cell == drop7::kSolid || cell == drop7::kCracked) {
        ++covers;
        if (cell == drop7::kSolid) ++solid;
        else ++cracked;
        const double cover_factor = cell == drop7::kSolid ? 1.0 : 0.65;
        const double edge_factor =
            column == 0 || column == drop7::kBoardSize - 1 ? 1.3 : 1.0;
        cover_altitude +=
            elevation * elevation * cover_factor * edge_factor;
        continue;
      }
      if (!drop7::isNumbered(cell)) continue;
      ++numbered;
      if (cell <= 2 && elevation >= 5) ++high_low;
      const int horizontal = drop7::lineLength(board, row, column, false);
      const int vertical = drop7::lineLength(board, row, column, true);
      const double horizontal_ready =
          cell > horizontal ? readiness(static_cast<int>(cell) - horizontal)
                            : 0.0;
      const double vertical_ready =
          cell > vertical ? readiness(static_cast<int>(cell) - vertical) : 0.0;
      direct_potential += unionReadiness(horizontal_ready, vertical_ready);
    }
  }

  for (int column = 0; column < drop7::kBoardSize; ++column) {
    const int height = heights[column];
    if (height == 0) continue;
    const std::uint8_t cap =
        board[drop7::indexOf(drop7::kBoardSize - height, column)];
    if (cap != 1 && cap != 2) continue;
    low_caps[column] = true;
    low_cap_load += height * height * (cap == 1 ? 1.5 : 1.0);
    if (column > 0 && low_caps[column - 1]) {
      const int shared = std::min(heights[column - 1], height);
      adjacent_low_cap_load += shared * shared;
    }
  }

  const int moves_until_rise =
      std::max(1, std::min(drop7::kMovesPerLevel, state.moves_remaining));
  const double rise_urgency =
      static_cast<double>(drop7::kMovesPerLevel - moves_until_rise) /
      static_cast<double>(drop7::kMovesPerLevel - 1);
  const double projected = occupied + drop7::kBoardSize -
                           1.4 * moves_until_rise;
  const double occupancy_debt =
      std::pow(std::max(0.0, projected - 14.0), 2.0);
  const double residual_covers =
      std::max(0.0, covers - 1.4 * moves_until_rise);
  const double cover_debt = residual_covers * residual_covers;
  const double peak_risk = std::pow(
      std::max(0.0, maximum_height + rise_urgency - 3.0), 3.0);

  int placement_triggers = 0;
  int quiet_options = 0;
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    if (heights[column] >= drop7::kBoardSize) continue;
    Board placed = board;
    if (!drop7::placeDisc(placed, column, state.next_disc)) continue;
    int popper_count = 0;
    drop7::findPoppers(placed, popper_count);
    if (popper_count > 0) placement_triggers += popper_count;
    else ++quiet_options;
  }

  int rise_triggers = 0;
  Board raised{};
  if (drop7::raiseCoveredRow(board, raised)) {
    drop7::findPoppers(raised, rise_triggers);
  }
  const double rise_trigger_readiness =
      rise_triggers * (moves_until_rise == 1
                           ? 1.0
                           : readiness(moves_until_rise - 1));

  return 180.0 * open_columns - 10.0 * height_load - 620.0 * solid -
         220.0 * cracked - 18.0 * numbered - 90.0 * high_low +
         360.0 * direct_potential - 240.0 * occupancy_debt -
         200.0 * cover_debt - 50.0 * cover_altitude -
         70.0 * cover_altitude * rise_urgency - 1800.0 * peak_risk -
         120.0 * low_cap_load - 180.0 * adjacent_low_cap_load +
         600.0 * placement_triggers + 300.0 * quiet_options +
         1200.0 * rise_trigger_readiness;
}

double rankState(const State& state, const TeacherOptions& options) {
  const double terminal =
      state.game_over ? -options.terminal_penalty : 0.0;
  return static_cast<double>(state.score) +
         options.leaf_scale * observableLeafUtility(state) + terminal;
}

bool playTapeMove(const State& state, int column, std::uint32_t tape_key,
                  int ply, MoveResult& move) {
  drop7::Mulberry32 random(tapeRevealSeed(tape_key, ply));
  if (!drop7::playMove(state, column, random, move)) return false;
  if (!move.state.game_over) move.state.next_disc = tapeDisc(tape_key, ply);
  return true;
}

bool sameDynamics(const State& first, const State& second) {
  return first.board == second.board && first.next_disc == second.next_disc &&
         first.moves_remaining == second.moves_remaining &&
         first.game_over == second.game_over;
}

void insertDeduplicated(std::vector<BeamNode>& candidates, BeamNode node,
                        TeacherStats& stats) {
  for (BeamNode& existing : candidates) {
    if (!sameDynamics(existing.state, node.state)) continue;
    ++stats.deduplicated_states;
    if (node.rank > existing.rank) existing = std::move(node);
    return;
  }
  candidates.push_back(std::move(node));
}

bool betterNode(const BeamNode& first, const BeamNode& second) {
  if (first.rank != second.rank) return first.rank > second.rank;
  if (first.state.score != second.state.score) {
    return first.state.score > second.state.score;
  }
  return drop7::serializeBoard(first.state.board) <
         drop7::serializeBoard(second.state.board);
}

std::array<double, drop7::kBoardSize> evaluateOneTape(
    const State& root, std::uint32_t tape_key,
    const TeacherOptions& options, TeacherStats& stats) {
  std::array<double, drop7::kBoardSize> values{};
  values.fill(-options.terminal_penalty);
  std::array<std::vector<BeamNode>, drop7::kBoardSize> beams;

  int legal_count = 0;
  const auto legal = drop7::legalColumns(root.board, legal_count);
  for (int offset = 0; offset < legal_count; ++offset) {
    const int column = legal[offset];
    MoveResult move;
    if (!playTapeMove(root, column, tape_key, 0, move)) continue;
    ++stats.generated_states;
    beams[column].push_back({move.state, rankState(move.state, options)});
  }

  for (int ply = 1; ply < options.horizon; ++ply) {
    for (int root_column = 0; root_column < drop7::kBoardSize;
         ++root_column) {
      if (beams[root_column].empty()) continue;
      std::vector<BeamNode> candidates;
      candidates.reserve(static_cast<std::size_t>(options.beam_per_action) *
                         drop7::kBoardSize);
      for (const BeamNode& node : beams[root_column]) {
        if (node.state.game_over) {
          insertDeduplicated(candidates, node, stats);
          continue;
        }
        int continuation_count = 0;
        const auto continuations =
            drop7::legalColumns(node.state.board, continuation_count);
        for (int offset = 0; offset < continuation_count; ++offset) {
          MoveResult move;
          if (!playTapeMove(node.state, continuations[offset], tape_key, ply,
                            move)) {
            continue;
          }
          ++stats.generated_states;
          insertDeduplicated(
              candidates,
              {move.state, rankState(move.state, options)}, stats);
        }
      }
      stats.peak_candidates =
          std::max(stats.peak_candidates, candidates.size());
      std::sort(candidates.begin(), candidates.end(), betterNode);
      if (static_cast<int>(candidates.size()) > options.beam_per_action) {
        candidates.resize(options.beam_per_action);
      }
      stats.peak_retained =
          std::max(stats.peak_retained, candidates.size());
      beams[root_column] = std::move(candidates);
    }
  }

  for (int offset = 0; offset < legal_count; ++offset) {
    const int column = legal[offset];
    if (beams[column].empty()) continue;
    const auto best =
        std::max_element(beams[column].begin(), beams[column].end(),
                         [](const BeamNode& first, const BeamNode& second) {
                           return betterNode(second, first);
                         });
    values[column] = best->rank;
  }
  return values;
}

int tieRank(int column) {
  constexpr std::array<int, drop7::kBoardSize> ranks{{5, 3, 1, 0, 2, 4, 6}};
  return ranks[column];
}

TeacherDecision teacherDecisionCanonical(const State& canonical,
                                         const TeacherOptions& options,
                                         TeacherStats& stats) {
  TeacherDecision decision;
  std::array<int, drop7::kBoardSize> votes{};
  State normalized = canonical;
  normalized.score = 0;
  normalized.level = 1;
  normalized.moves_played = 0;
  const std::uint32_t root_hash = observableHash(normalized);
  for (int tape = 0; tape < options.tapes; ++tape) {
    const auto values = evaluateOneTape(
        normalized, tapeKey(root_hash, tape), options, stats);
    int tape_choice = -1;
    double tape_best = -std::numeric_limits<double>::infinity();
    for (int column = 0; column < drop7::kBoardSize; ++column) {
      if (!drop7::isLegal(normalized.board, column)) continue;
      decision.mean_values[column] += values[column];
      ++decision.observations[column];
      if (values[column] > tape_best + 1e-9 ||
          (std::abs(values[column] - tape_best) <= 1e-9 &&
           (tape_choice < 0 || tieRank(column) < tieRank(tape_choice)))) {
        tape_best = values[column];
        tape_choice = column;
      }
    }
    if (tape_choice >= 0) ++votes[tape_choice];
  }

  double best_value = -std::numeric_limits<double>::infinity();
  int best_votes = -1;
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    if (decision.observations[column] == 0) continue;
    decision.mean_values[column] /= decision.observations[column];
    const double value = decision.mean_values[column];
    const bool better_vote = options.vote_aggregate &&
        (votes[column] > best_votes ||
         (votes[column] == best_votes &&
          (value > best_value + 1e-9 ||
           (std::abs(value - best_value) <= 1e-9 &&
            (decision.column < 0 ||
             tieRank(column) < tieRank(decision.column))))));
    const bool better_mean = !options.vote_aggregate &&
        (value > best_value + 1e-9 ||
         (std::abs(value - best_value) <= 1e-9 &&
          (decision.column < 0 || tieRank(column) < tieRank(decision.column))));
    if (better_vote || better_mean) {
      best_votes = votes[column];
      best_value = value;
      decision.column = column;
    }
  }
  return decision;
}

TeacherDecision teacherDecision(const State& public_state,
                                const TeacherOptions& options,
                                TeacherStats& stats) {
  // Entire teacher interface: no environment seed or environment RNG exists.
  bool mirrored = false;
  const State canonical = canonicalState(public_state, mirrored);
  TeacherDecision decision =
      teacherDecisionCanonical(canonical, options, stats);
  if (!mirrored || decision.column < 0) return decision;
  decision.column = drop7::kBoardSize - 1 - decision.column;
  std::array<double, drop7::kBoardSize> mapped_values{};
  std::array<int, drop7::kBoardSize> mapped_observations{};
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    mapped_values[drop7::kBoardSize - 1 - column] =
        decision.mean_values[column];
    mapped_observations[drop7::kBoardSize - 1 - column] =
        decision.observations[column];
  }
  decision.mean_values = mapped_values;
  decision.observations = mapped_observations;
  return decision;
}

void validateTeacherOptions(const TeacherOptions& options) {
  if (options.horizon < 1 || options.horizon > kMaximumHorizon) {
    throw std::invalid_argument("--horizon must be from 1 to 50");
  }
  if (options.tapes < 1 || options.tapes > kMaximumTapes) {
    throw std::invalid_argument("--tapes must be from 1 to 64");
  }
  if (options.beam_per_action < 1 ||
      options.beam_per_action > kMaximumBeamPerAction) {
    throw std::invalid_argument("--beam-per-action must be from 1 to 64");
  }
  if (!std::isfinite(options.leaf_scale) || options.leaf_scale <= 0 ||
      !std::isfinite(options.terminal_penalty) ||
      options.terminal_penalty <= 0) {
    throw std::invalid_argument("teacher utility scales must be positive");
  }
}

TeacherOptions parseTeacherOptions(int argc, char** argv) {
  TeacherOptions options;
  options.vote_aggregate = hasFlag(argc, argv, "--vote-aggregate");
  options.horizon = parsePositive(
      valueAfter(argc, argv, "--horizon", std::to_string(options.horizon)),
      "--horizon");
  options.tapes = parsePositive(
      valueAfter(argc, argv, "--tapes", std::to_string(options.tapes)),
      "--tapes");
  options.beam_per_action = parsePositive(
      valueAfter(argc, argv, "--beam-per-action",
                 std::to_string(options.beam_per_action)),
      "--beam-per-action");
  options.leaf_scale = parsePositiveDouble(
      valueAfter(argc, argv, "--leaf-scale", std::to_string(options.leaf_scale)),
      "--leaf-scale");
  options.terminal_penalty = parsePositiveDouble(
      valueAfter(argc, argv, "--terminal-penalty",
                 std::to_string(options.terminal_penalty)),
      "--terminal-penalty");
  validateTeacherOptions(options);
  return options;
}

RunOptions parseRunOptions(int argc, char** argv) {
  RunOptions options;
  options.games = parsePositive(
      valueAfter(argc, argv, "--games", std::to_string(options.games)),
      "--games");
  options.max_moves = parsePositive(
      valueAfter(argc, argv, "--max-moves", std::to_string(options.max_moves)),
      "--max-moves");
  options.range = valueAfter(argc, argv, "--range", options.range);
  if (options.range == "train") options.seed_start = kTrainingSeedStart;
  else if (options.range == "probe") options.seed_start = kProbeSeedStart;
  else throw std::invalid_argument("--range must be train or probe");
  if (options.games > 64) throw std::invalid_argument("--games is bounded at 64");
  if (options.max_moves > 5000) {
    throw std::invalid_argument("--max-moves is bounded at 5000");
  }
  options.teacher = parseTeacherOptions(argc, argv);
  return options;
}

std::uint64_t maximumResidentBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

GameResult runGame(std::uint32_t environment_seed,
                   const RunOptions& options) {
  const auto started = Clock::now();
  State state = drop7::initialHeadlessState(environment_seed);
  TeacherStats stats;
  while (!state.game_over && state.moves_played < options.max_moves) {
    const TeacherDecision decision =
        teacherDecision(state, options.teacher, stats);
    if (decision.column < 0) {
      throw std::runtime_error("teacher found no move in a live game");
    }
    // The actual game seed enters only after the teacher commits its action.
    MoveResult move;
    if (!drop7::playHeadlessMove(state, environment_seed, decision.column,
                                 move)) {
      throw std::runtime_error("teacher committed an illegal move");
    }
  }
  return {environment_seed,
          state.score,
          state.moves_played,
          state.level,
          !state.game_over,
          stats,
          std::chrono::duration<double>(Clock::now() - started).count()};
}

double percentile(std::vector<std::int64_t> values, double quantile) {
  std::sort(values.begin(), values.end());
  const double position = quantile * (values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - lower;
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

void printArray(const std::vector<GameResult>& results, bool scores) {
  std::cout << '[';
  for (std::size_t index = 0; index < results.size(); ++index) {
    if (index != 0) std::cout << ',';
    if (scores) std::cout << results[index].score;
    else std::cout << results[index].moves;
  }
  std::cout << ']';
}

int runBenchmark(int argc, char** argv) {
  const RunOptions options = parseRunOptions(argc, argv);
  const auto started = Clock::now();
  std::vector<GameResult> results;
  results.reserve(options.games);
  for (int game = 0; game < options.games; ++game) {
    const std::uint32_t seed =
        options.seed_start + static_cast<std::uint32_t>(game);
    GameResult result = runGame(seed, options);
    std::cout << "GAME {\"seed\":\"0x" << std::hex << std::setw(8)
              << std::setfill('0') << seed << std::dec << std::setfill(' ')
              << "\",\"score\":" << result.score << ",\"moves\":"
              << result.moves << ",\"level\":" << result.level
              << ",\"censored\":" << (result.censored ? "true" : "false")
              << ",\"seconds\":" << std::fixed << std::setprecision(6)
              << result.seconds << ",\"generatedStates\":"
              << result.stats.generated_states << ",\"deduplicatedStates\":"
              << result.stats.deduplicated_states << "}\n";
    results.push_back(result);
  }

  std::int64_t score_sum = 0;
  std::int64_t move_sum = 0;
  std::uint64_t generated_sum = 0;
  std::uint64_t deduplicated_sum = 0;
  std::size_t peak_candidates = 0;
  std::size_t peak_retained = 0;
  int censored = 0;
  std::vector<std::int64_t> scores;
  for (const GameResult& result : results) {
    score_sum += result.score;
    move_sum += result.moves;
    generated_sum += result.stats.generated_states;
    deduplicated_sum += result.stats.deduplicated_states;
    peak_candidates =
        std::max(peak_candidates, result.stats.peak_candidates);
    peak_retained = std::max(peak_retained, result.stats.peak_retained);
    if (result.censored) ++censored;
    scores.push_back(result.score);
  }
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::cout << "RESULT {\"range\":\"" << options.range
            << "\",\"seedStart\":\"0x" << std::hex << std::setw(8)
            << std::setfill('0') << options.seed_start << std::dec
            << std::setfill(' ') << "\",\"games\":" << options.games
            << ",\"horizon\":" << options.teacher.horizon
            << ",\"tapes\":" << options.teacher.tapes
            << ",\"beamPerAction\":" << options.teacher.beam_per_action
            << ",\"aggregation\":\""
            << (options.teacher.vote_aggregate ? "vote" : "mean") << '"'
            << ",\"meanScore\":" << std::fixed << std::setprecision(3)
            << static_cast<double>(score_sum) / results.size()
            << ",\"medianScore\":" << percentile(scores, 0.5)
            << ",\"p25Score\":" << percentile(scores, 0.25)
            << ",\"minimumScore\":"
            << *std::min_element(scores.begin(), scores.end())
            << ",\"maximumScore\":"
            << *std::max_element(scores.begin(), scores.end())
            << ",\"meanMoves\":"
            << static_cast<double>(move_sum) / results.size()
            << ",\"censored\":" << censored << ",\"seconds\":"
            << seconds << ",\"generatedStates\":" << generated_sum
            << ",\"statesPerSecond\":" << generated_sum / seconds
            << ",\"deduplicatedStates\":" << deduplicated_sum
            << ",\"peakCandidatesPerAction\":" << peak_candidates
            << ",\"peakRetainedPerAction\":" << peak_retained
            << ",\"maxRssBytes\":" << maximumResidentBytes()
            << ",\"scores\":";
  printArray(results, true);
  std::cout << ",\"moves\":";
  printArray(results, false);
  std::cout << "}\n";
  return 0;
}

State syntheticState() {
  State state;
  state.board.fill(drop7::kEmpty);
  state.board[drop7::indexOf(6, 0)] = drop7::kSolid;
  state.board[drop7::indexOf(6, 1)] = drop7::kCracked;
  state.board[drop7::indexOf(5, 1)] = 5;
  state.board[drop7::indexOf(6, 2)] = 6;
  state.board[drop7::indexOf(5, 2)] = 2;
  state.board[drop7::indexOf(6, 3)] = 7;
  state.board[drop7::indexOf(6, 4)] = 4;
  state.next_disc = 3;
  state.moves_remaining = 2;
  return state;
}

bool runSelfTest() {
  TeacherOptions options;
  options.horizon = 4;
  options.tapes = 3;
  options.beam_per_action = 2;
  const State state = syntheticState();
  TeacherStats first_stats;
  const TeacherDecision first = teacherDecision(state, options, first_stats);
  TeacherStats repeat_stats;
  const TeacherDecision repeat = teacherDecision(state, options, repeat_stats);
  if (first.column < 0 || first.column != repeat.column ||
      first.mean_values != repeat.mean_values ||
      first_stats.generated_states != repeat_stats.generated_states) {
    std::cerr << "teacher determinism test failed\n";
    return false;
  }

  State altered = state;
  altered.score = 987'654;
  altered.level = 70;
  altered.moves_played = 345;
  TeacherStats altered_stats;
  const TeacherDecision seed_blind =
      teacherDecision(altered, options, altered_stats);
  if (seed_blind.column != first.column ||
      seed_blind.mean_values != first.mean_values) {
    std::cerr << "teacher seed-blind test failed\n";
    return false;
  }

  State mirrored = state;
  mirrored.board = mirrorBoard(state.board);
  TeacherStats mirror_stats;
  const TeacherDecision reflected =
      teacherDecision(mirrored, options, mirror_stats);
  if (reflected.column != drop7::kBoardSize - 1 - first.column) {
    std::cerr << "teacher mirror test failed\n";
    return false;
  }
  for (int column = 0; column < drop7::kBoardSize; ++column) {
    if (reflected.mean_values[drop7::kBoardSize - 1 - column] !=
        first.mean_values[column]) {
      std::cerr << "teacher mirrored values test failed\n";
      return false;
    }
  }

  TeacherOptions vote_options = options;
  vote_options.vote_aggregate = true;
  TeacherStats vote_stats;
  const TeacherDecision vote =
      teacherDecision(state, vote_options, vote_stats);
  TeacherStats vote_repeat_stats;
  const TeacherDecision vote_repeat =
      teacherDecision(state, vote_options, vote_repeat_stats);
  TeacherStats vote_mirror_stats;
  const TeacherDecision vote_reflected =
      teacherDecision(mirrored, vote_options, vote_mirror_stats);
  TeacherStats vote_altered_stats;
  const TeacherDecision vote_seed_blind =
      teacherDecision(altered, vote_options, vote_altered_stats);
  if (vote.column < 0 || vote.column != vote_repeat.column ||
      vote.column != vote_seed_blind.column ||
      vote_reflected.column != drop7::kBoardSize - 1 - vote.column ||
      vote.mean_values != vote_repeat.mean_values ||
      vote_stats.generated_states != vote_repeat_stats.generated_states) {
    std::cerr << "teacher vote aggregation invariants failed\n";
    return false;
  }

  bool ignored = false;
  const std::uint32_t hash = observableHash(canonicalState(state, ignored));
  const std::uint32_t first_tape = tapeKey(hash, 0);
  const std::uint32_t second_tape = tapeKey(hash, 1);
  if (first_tape == second_tape ||
      (tapeRevealSeed(first_tape, 0) == tapeRevealSeed(second_tape, 0) &&
       tapeDisc(first_tape, 0) == tapeDisc(second_tape, 0))) {
    std::cerr << "independent tape test failed\n";
    return false;
  }
  const std::size_t candidate_bound =
      static_cast<std::size_t>(options.beam_per_action) *
      drop7::kBoardSize;
  if (first_stats.peak_candidates > candidate_bound ||
      first_stats.peak_retained >
          static_cast<std::size_t>(options.beam_per_action)) {
    std::cerr << "teacher memory bound test failed\n";
    return false;
  }
  if (maximumResidentBytes() == 0) {
    std::cerr << "teacher RSS test failed\n";
    return false;
  }
  std::cout << "SELF_TEST {\"deterministic\":true,\"seedBlind\":true,"
               "\"mirrorEquivariant\":true,\"independentTapes\":true,"
               "\"voteAggregation\":true,\"boundedMemory\":true,"
               "\"selectedColumn\":"
            << first.column << ",\"generatedStates\":"
            << first_stats.generated_states << ",\"peakCandidatesPerAction\":"
            << first_stats.peak_candidates << ",\"maxRssBytes\":"
            << maximumResidentBytes() << "}\n";
  return true;
}

void printUsage() {
  std::cerr
      << "Usage:\n"
      << "  drop7_teacher --self-test\n"
      << "  drop7_teacher --benchmark [--range train|probe] [--games N] "
         "[--max-moves N]\n"
      << "      [--horizon 1..50] [--tapes 1..64] "
         "[--beam-per-action 1..64] [--vote-aggregate]\n"
      << "      [--leaf-scale X] [--terminal-penalty X]\n";
}

}  // namespace

int main(int argc, char** argv) {
  try {
    if (hasFlag(argc, argv, "--self-test")) return runSelfTest() ? 0 : 1;
    if (hasFlag(argc, argv, "--benchmark")) return runBenchmark(argc, argv);
    printUsage();
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
