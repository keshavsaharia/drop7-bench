#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <compare>
#include <cstdint>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// Analysis-only comparison between a public-state exact expectimax policy and
// a deliberately privileged oracle which sees the realized headless random
// tape.  Oracle information must never cross into a deployed policy.
namespace drop7::oracle_topology {

using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kDefaultSeedStart = 0x3d70'f000u;
constexpr std::uint32_t kTrainingStart = 0x3d00'0000u;
constexpr std::uint32_t kTrainingEnd = 0x3e00'0000u;
constexpr int kDefaultGames = 16;
constexpr int kDefaultMaximumMoves = 200;
constexpr int kDefaultOracleDepth = 4;
constexpr int kDefaultOracleBeam = 128;
constexpr int kMoveBandWidth = 20;
constexpr int kOccupancyBinWidth = 4;
constexpr int kHeightBinWidth = 2;
constexpr double kMinimumStableEffect = 0.10;
constexpr int kMinimumSeedsPerHalf = 6;
constexpr double kTerminalPenalty = -1'000'000'000.0;

std::mutex progress_mutex;

struct Config {
  std::uint32_t seed_start = kDefaultSeedStart;
  int games = kDefaultGames;
  int maximum_moves = kDefaultMaximumMoves;
  int oracle_depth = kDefaultOracleDepth;
  int oracle_beam = kDefaultOracleBeam;
  int parallelism = 4;
  std::string json = "/tmp/drop7-oracle-topology-audit.json";
  std::string readme = "/tmp/drop7-oracle-topology-audit-README.md";
};

struct OracleStats {
  std::uint64_t generated = 0;
  std::uint64_t deduplicated = 0;
  std::size_t peak_candidates = 0;
};

struct OraclePlan {
  int column = -1;
  OracleStats stats{};
};

struct BeamNode {
  State state{};
  int first_column = -1;
  std::string dynamic_key;
  double rank = -std::numeric_limits<double>::infinity();
};

double originalCombinedUtility(const State& state) {
  if (state.game_over) return -250'000.0;
  const cfpi::detail::PhaseFeatures f =
      cfpi::detail::extractPhaseFeatures(state);
  return 180.0 * f.open_columns - 10.0 * f.height_load -
         620.0 * f.solid_cells - 220.0 * f.cracked_cells -
         18.0 * f.numbered_cells - 90.0 * f.high_low_numbers +
         140.0 * f.direct_potential + 360.0 * f.latent_chain_potential +
         100.0 * f.cracked_exposure + 40.0 * f.solid_exposure -
         550.0 * f.adjacent_ones - 750.0 * f.triple_twos -
         120.0 * f.dead_low_numbers;
}

double rankState(const State& state) {
  return static_cast<double>(state.score) + originalCombinedUtility(state) +
         (state.game_over ? kTerminalPenalty : 0.0);
}

std::string oracleDynamicKey(const State& state) {
  std::string key = serializeBoard(state.board);
  key.push_back('|');
  key += std::to_string(state.next_disc);
  key.push_back('|');
  key += std::to_string(state.level);
  key.push_back('|');
  key += std::to_string(state.moves_remaining);
  key.push_back('|');
  key += std::to_string(state.moves_played);
  key.push_back('|');
  key.push_back(state.game_over ? '1' : '0');
  return key;
}

bool betterBeamNode(const BeamNode& left, const BeamNode& right) {
  if (left.rank != right.rank) return left.rank > right.rank;
  if (left.state.score != right.state.score) {
    return left.state.score > right.state.score;
  }
  if (left.first_column != right.first_column) {
    return left.first_column < right.first_column;
  }
  return left.dynamic_key < right.dynamic_key;
}

void insertCandidate(std::unordered_map<std::string, BeamNode>& candidates,
                     BeamNode candidate, OracleStats& stats) {
  const auto found = candidates.find(candidate.dynamic_key);
  if (found == candidates.end()) {
    candidates.emplace(candidate.dynamic_key, std::move(candidate));
    return;
  }
  ++stats.deduplicated;
  if (candidate.state.score > found->second.state.score ||
      (candidate.state.score == found->second.state.score &&
       candidate.first_column < found->second.first_column)) {
    found->second = std::move(candidate);
  }
}

OraclePlan planOracleMove(const State& root, std::uint32_t game_seed,
                          int depth, int beam_width) {
  if (root.game_over) return {};
  if (depth < 1 || depth > 12 || beam_width < 1 || beam_width > 2'048) {
    throw std::invalid_argument("oracle work bounds are invalid");
  }
  std::vector<BeamNode> beam{{root, -1, oracleDynamicKey(root),
                              rankState(root)}};
  OracleStats stats;
  for (int ply = 0; ply < depth; ++ply) {
    std::unordered_map<std::string, BeamNode> candidates;
    candidates.reserve(static_cast<std::size_t>(beam_width * kBoardSize));
    for (const BeamNode& node : beam) {
      if (node.state.game_over) {
        insertCandidate(candidates, node, stats);
        continue;
      }
      int legal_count = 0;
      const auto legal = legalColumns(node.state.board, legal_count);
      for (int offset = 0; offset < legal_count; ++offset) {
        const int column = legal[offset];
        State next = node.state;
        MoveResult move;
        if (!playHeadlessMove(next, game_seed, column, move)) continue;
        ++stats.generated;
        BeamNode candidate;
        candidate.state = std::move(next);
        candidate.first_column =
            node.first_column < 0 ? column : node.first_column;
        candidate.dynamic_key = oracleDynamicKey(candidate.state);
        insertCandidate(candidates, std::move(candidate), stats);
      }
    }
    if (candidates.empty()) break;
    stats.peak_candidates =
        std::max(stats.peak_candidates, candidates.size());
    std::vector<BeamNode> ranked;
    ranked.reserve(candidates.size());
    for (auto& entry : candidates) {
      entry.second.rank = rankState(entry.second.state);
      ranked.push_back(std::move(entry.second));
    }
    std::sort(ranked.begin(), ranked.end(), betterBeamNode);
    if (static_cast<int>(ranked.size()) > beam_width) {
      ranked.resize(static_cast<std::size_t>(beam_width));
    }
    beam = std::move(ranked);
  }
  std::sort(beam.begin(), beam.end(), betterBeamNode);
  for (const BeamNode& node : beam) {
    if (node.first_column >= 0) return {node.first_column, stats};
  }
  return {-1, stats};
}

enum Feature : std::size_t {
  kOccupied,
  kMaximumHeight,
  kHeightLoad,
  kSolidCells,
  kCrackedCells,
  kNumberedCells,
  kDirectPotential,
  kLatentChainPotential,
  kTriggerReadiness,
  kRiseTriggerReadiness,
  kQuietBuildOptions,
  kQuietDirectGain,
  kCrackedExposure,
  kSolidExposure,
  kCoverAltitudeDebt,
  kSolidAltitude,
  kCrackedAltitude,
  kCoveredCliffAccess,
  kEdgeCliffAccess,
  kHighNumberColumnCohesion,
  kHighNumberCliffCohesion,
  kStoredHighNumbers,
  kAdjacentOnes,
  kTripleTwos,
  kDeadLowNumbers,
  kLowCapLoad,
  kAdjacentLowCapLoad,
  kProjectedOccupancyDebt,
  kPeakHeightRisk,
  kNumberedCleared,
  kCoversRevealed,
  kChainWaves,
  kMaximumChainDepth,
  kMultiwaveMove,
  kCascadePoints,
  kBoardClear,
  kFeatureCount,
};

using Features = std::array<double, kFeatureCount>;

struct FeatureSpec {
  const char* name;
  const char* category;
  const char* kind;
  const char* interpretation;
};

constexpr std::array<FeatureSpec, kFeatureCount> kFeatureSpecs{{
    {"occupied", "balance", "state", "occupied board cells"},
    {"maximumHeight", "balance", "state", "highest column"},
    {"heightLoad", "coverAltitudeDamage", "state", "squared altitude load"},
    {"solidCells", "coverAltitudeDamage", "state", "undamaged covers"},
    {"crackedCells", "coverAltitudeDamage", "state", "partially damaged covers"},
    {"numberedCells", "balance", "state", "visible numbered discs"},
    {"directPotential", "storedEnergy", "state", "direct trigger readiness"},
    {"latentChainPotential", "storedEnergy", "state", "release readiness behind another trigger"},
    {"triggerReadiness", "storedEnergy", "state", "immediate legal-drop trigger inventory"},
    {"riseTriggerReadiness", "storedEnergy", "state", "stored trigger after the next rise"},
    {"quietBuildOptions", "storedEnergy", "state", "legal non-triggering build choices"},
    {"quietDirectGain", "storedEnergy", "state", "best quiet increase in direct readiness"},
    {"crackedExposure", "coverAccess", "state", "reachable cracked covers"},
    {"solidExposure", "coverAccess", "state", "two-hit solid-cover exposure"},
    {"coverAltitudeDebt", "coverAltitudeDamage", "state", "altitude-weighted cover burden"},
    {"solidAltitude", "coverAltitudeDamage", "state", "squared altitude of solid covers"},
    {"crackedAltitude", "coverAltitudeDamage", "state", "squared altitude of cracked covers"},
    {"coveredCliffAccess", "coverAccess", "state", "covered cliff faces reachable from a low channel"},
    {"edgeCliffAccess", "coverAccess", "state", "reachable covered cliff faces on board edges"},
    {"highNumberColumnCohesion", "highNumberTrench", "state", "repeated live 5/6/7 discs sharing a vertical trigger"},
    {"highNumberCliffCohesion", "highNumberTrench", "state", "high-number cohesion adjacent to covered cliffs"},
    {"storedHighNumbers", "highNumberTrench", "state", "unfired high-number readiness"},
    {"adjacentOnes", "lowNumberClog", "state", "paired ones with no easy escape"},
    {"tripleTwos", "lowNumberClog", "state", "runs of three or more twos"},
    {"deadLowNumbers", "lowNumberClog", "state", "oversized low discs with weak release paths"},
    {"lowCapLoad", "lowNumberClog", "state", "height-weighted columns capped by one or two"},
    {"adjacentLowCapLoad", "lowNumberClog", "state", "neighboring low-number column caps"},
    {"projectedOccupancyDebt", "balance", "state", "load projected through the next rise"},
    {"peakHeightRisk", "balance", "state", "phase-adjusted cubic height risk"},
    {"numberedCleared", "clearRevealThroughput", "transitionOutcome", "numbered discs cleared on the following move"},
    {"coversRevealed", "clearRevealThroughput", "transitionOutcome", "covers revealed on the following move"},
    {"chainWaves", "clearRevealThroughput", "transitionOutcome", "cascade waves on the following move"},
    {"maximumChainDepth", "clearRevealThroughput", "transitionOutcome", "deepest following cascade wave"},
    {"multiwaveMove", "clearRevealThroughput", "transitionOutcome", "following move has multiple waves"},
    {"cascadePoints", "clearRevealThroughput", "transitionOutcome", "following cascade score excluding bonuses"},
    {"boardClear", "clearRevealThroughput", "transitionOutcome", "following move clears the board"},
}};

double readiness(int required) {
  return required >= 1 ? std::ldexp(1.0, 1 - required) : 0.0;
}

double unionReadiness(double first, double second) {
  return 1.0 - (1.0 - first) * (1.0 - second);
}

std::array<int, kBoardSize> heights(const Board& board) {
  std::array<int, kBoardSize> result{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      result[column] += board[indexOf(row, column)] != kEmpty;
    }
  }
  return result;
}

int adjacentCoveredCliffDepth(
    const Board& board, int column, int height,
    const std::array<int, kBoardSize>& column_heights) {
  int result = 0;
  for (const int neighbor : {column - 1, column + 1}) {
    if (neighbor < 0 || neighbor >= kBoardSize) continue;
    for (int elevation = height + 1;
         elevation <= column_heights[neighbor]; ++elevation) {
      const std::uint8_t cell =
          board[indexOf(kBoardSize - elevation, neighbor)];
      result += cell == kSolid || cell == kCracked;
    }
  }
  return result;
}

Features extractFeatures(const State& state, const MoveResult& move) {
  Features result{};
  const cfpi::detail::PhaseFeatures phase =
      cfpi::detail::extractPhaseFeatures(state);
  const auto column_heights = heights(state.board);
  int occupied = 0;
  int maximum_height = 0;
  for (const int height : column_heights) maximum_height = std::max(maximum_height, height);
  for (const std::uint8_t cell : state.board) occupied += cell != kEmpty;
  result[kOccupied] = occupied;
  result[kMaximumHeight] = maximum_height;
  result[kHeightLoad] = phase.height_load;
  result[kSolidCells] = phase.solid_cells;
  result[kCrackedCells] = phase.cracked_cells;
  result[kNumberedCells] = phase.numbered_cells;
  result[kDirectPotential] = phase.direct_potential;
  result[kLatentChainPotential] = phase.latent_chain_potential;
  result[kTriggerReadiness] = phase.trigger_readiness;
  result[kRiseTriggerReadiness] = phase.rise_trigger_readiness;
  result[kQuietBuildOptions] = phase.quiet_build_options;
  result[kQuietDirectGain] = phase.quiet_direct_gain;
  result[kCrackedExposure] = phase.cracked_exposure;
  result[kSolidExposure] = phase.solid_exposure;
  result[kCoverAltitudeDebt] = phase.cover_altitude_debt;
  result[kAdjacentOnes] = phase.adjacent_ones;
  result[kTripleTwos] = phase.triple_twos;
  result[kDeadLowNumbers] = phase.dead_low_numbers;
  result[kLowCapLoad] = phase.low_cap_load;
  result[kAdjacentLowCapLoad] = phase.adjacent_low_cap_load;
  result[kProjectedOccupancyDebt] = phase.projected_occupancy_debt;
  result[kPeakHeightRisk] = phase.peak_height_risk;

  for (int row = 0; row < kBoardSize; ++row) {
    const int elevation = kBoardSize - row;
    for (int column = 0; column < kBoardSize; ++column) {
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell != kSolid && cell != kCracked) continue;
      const double altitude = static_cast<double>(elevation * elevation);
      if (cell == kSolid) result[kSolidAltitude] += altitude;
      else result[kCrackedAltitude] += altitude;
      const double cover_factor = cell == kSolid ? 1.0 : 0.68;
      const auto access_from = [&](int neighbor) {
        if (neighbor < 0 || neighbor >= kBoardSize ||
            column_heights[neighbor] >= elevation) {
          return 0.0;
        }
        return readiness(elevation - column_heights[neighbor]);
      };
      const double access = unionReadiness(access_from(column - 1),
                                           access_from(column + 1)) *
                            altitude * cover_factor;
      result[kCoveredCliffAccess] += access;
      if (column == 0 || column == kBoardSize - 1) {
        result[kEdgeCliffAccess] += access;
      }
    }
  }

  for (int column = 0; column < kBoardSize; ++column) {
    const int height = column_heights[column];
    std::array<int, 3> high_counts{};
    for (int row = kBoardSize - height; row < kBoardSize; ++row) {
      if (row < 0) continue;
      const std::uint8_t cell = state.board[indexOf(row, column)];
      if (cell >= 5 && cell <= 7 && cell > height) {
        ++high_counts[cell - 5];
      }
      if (cell >= 5 && cell <= 7) {
        const int horizontal = lineLength(state.board, row, column, false);
        const int vertical = lineLength(state.board, row, column, true);
        double ready = 0.0;
        if (horizontal < cell) ready = readiness(cell - horizontal);
        if (vertical < cell) {
          ready = unionReadiness(ready, readiness(cell - vertical));
        }
        if (ready > 0 && horizontal != cell && vertical != cell) {
          result[kStoredHighNumbers] += ready * (cell - 3) / 4.0;
        }
      }
    }
    const int cliff_depth = adjacentCoveredCliffDepth(
        state.board, column, height, column_heights);
    for (int offset = 0; offset < 3; ++offset) {
      const int count = high_counts[offset];
      if (count < 2) continue;
      const int value = offset + 5;
      const double cohesion =
          (count * (count - 1) / 2.0) * readiness(value - height);
      result[kHighNumberColumnCohesion] += cohesion;
      result[kHighNumberCliffCohesion] +=
          cohesion * std::min(3, cliff_depth);
    }
  }

  std::int64_t cascade_points = 0;
  int maximum_chain_depth = 0;
  for (const Wave& wave : move.waves) {
    result[kNumberedCleared] += wave.cleared;
    result[kCoversRevealed] += wave.revealed;
    cascade_points += wave.points;
    maximum_chain_depth = std::max(maximum_chain_depth, wave.depth);
  }
  result[kChainWaves] = move.waves.size();
  result[kMaximumChainDepth] = maximum_chain_depth;
  result[kMultiwaveMove] = move.waves.size() > 1;
  result[kCascadePoints] = static_cast<double>(cascade_points);
  result[kBoardClear] = move.cleared_board;
  for (const double value : result) {
    if (!std::isfinite(value)) {
      throw std::runtime_error("topology feature is non-finite");
    }
  }
  return result;
}

enum class Policy { kExact, kOracle };

struct StateRecord {
  std::uint32_t seed = 0;
  Policy policy = Policy::kExact;
  int move_band = 0;
  int rise_phase = 0;
  int occupancy_bin = 0;
  int height_bin = 0;
  Features features{};
};

struct GameResult {
  std::uint32_t seed = 0;
  Policy policy = Policy::kExact;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t work = 0;
  std::uint64_t generated = 0;
  std::uint64_t deduplicated = 0;
  std::size_t peak_candidates = 0;
  std::uint64_t completed_depth_sum = 0;
  std::uint64_t completed_depth_three = 0;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  std::uint64_t waves = 0;
  int maximum_chain_depth = 0;
  double elapsed_seconds = 0.0;
  std::vector<StateRecord> records;
};

State publicState(const State& source) {
  State result;
  result.board = source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.score = 0;
  result.level = 1;
  result.moves_played = 0;
  result.game_over = false;
  return result;
}

StateRecord makeRecord(std::uint32_t seed, Policy policy,
                       const State& state, const MoveResult& move) {
  const cfpi::PhaseMetrics metrics = cfpi::evaluatePhaseMetrics(state);
  StateRecord result;
  result.seed = seed;
  result.policy = policy;
  result.move_band = state.moves_played / kMoveBandWidth;
  result.rise_phase = state.moves_remaining;
  result.occupancy_bin = metrics.occupied / kOccupancyBinWidth;
  result.height_bin = metrics.maximum_height / kHeightBinWidth;
  result.features = extractFeatures(publicState(state), move);
  return result;
}

void addThroughput(GameResult& result, const MoveResult& move) {
  for (const Wave& wave : move.waves) {
    result.cleared += wave.cleared;
    result.revealed += wave.revealed;
    result.maximum_chain_depth =
        std::max(result.maximum_chain_depth, wave.depth);
  }
  result.waves += move.waves.size();
}

GameResult runExactGame(std::uint32_t seed, const Config& config) {
  const auto started = Clock::now();
  GameResult result;
  result.seed = seed;
  result.policy = Policy::kExact;
  result.records.reserve(config.maximum_moves);
  State state = initialHeadlessState(seed);
  cfpi::BehaviorOptions options;
  options.max_depth = 3;
  options.chance_samples = 5;
  options.max_work = 1'000'000;
  options.max_cache_entries = 40'000;
  options.terminal_utility = -1'000'000.0;
  while (!state.game_over && state.moves_played < config.maximum_moves) {
    cfpi::BehaviorMetrics metrics;
    const int action =
        cfpi::chooseBehaviorAction(publicState(state), options, &metrics);
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("exact audit policy chose an illegal action");
    }
    const State before = state;
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("exact audit transition failed");
    }
    result.records.push_back(makeRecord(seed, Policy::kExact, before, move));
    result.work += metrics.work;
    result.completed_depth_sum += metrics.completed_depth;
    result.completed_depth_three += metrics.completed_depth == 3;
    addThroughput(result, move);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

GameResult runOracleGame(std::uint32_t seed, const Config& config) {
  const auto started = Clock::now();
  GameResult result;
  result.seed = seed;
  result.policy = Policy::kOracle;
  result.records.reserve(config.maximum_moves);
  State state = initialHeadlessState(seed);
  while (!state.game_over && state.moves_played < config.maximum_moves) {
    const OraclePlan plan = planOracleMove(
        state, seed, config.oracle_depth, config.oracle_beam);
    if (!isLegal(state.board, plan.column)) {
      throw std::runtime_error("privileged oracle chose an illegal action");
    }
    const State before = state;
    MoveResult move;
    if (!playHeadlessMove(state, seed, plan.column, move)) {
      throw std::runtime_error("oracle audit transition failed");
    }
    result.records.push_back(makeRecord(seed, Policy::kOracle, before, move));
    result.generated += plan.stats.generated;
    result.deduplicated += plan.stats.deduplicated;
    result.peak_candidates =
        std::max(result.peak_candidates, plan.stats.peak_candidates);
    addThroughput(result, move);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Cohort {
  std::vector<GameResult> exact;
  std::vector<GameResult> oracle;
  double wall_seconds = 0.0;
};

Cohort runCohort(const Config& config) {
  const auto started = Clock::now();
  Cohort result;
  result.exact.resize(config.games);
  result.oracle.resize(config.games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  const int worker_count = std::min(config.parallelism, config.games);
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= config.games) return;
        const std::uint32_t seed =
            config.seed_start + static_cast<std::uint32_t>(game);
        result.exact[game] = runExactGame(seed, config);
        result.oracle[game] = runOracleGame(seed, config);
        const std::lock_guard<std::mutex> lock(progress_mutex);
        std::cerr << "oracle-topology " << game + 1 << '/' << config.games
                  << " seed 0x" << std::hex << seed << std::dec
                  << " exact " << result.exact[game].score << '/'
                  << result.exact[game].moves << " oracle "
                  << result.oracle[game].score << '/'
                  << result.oracle[game].moves << '\n';
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct MatchKey {
  std::uint32_t seed = 0;
  int move_band = 0;
  int rise_phase = 0;
  int occupancy_bin = 0;
  int height_bin = 0;

  auto operator<=>(const MatchKey&) const = default;
};

struct Accumulator {
  Features sum{};
  int count = 0;

  void add(const Features& values) {
    for (std::size_t feature = 0; feature < values.size(); ++feature) {
      sum[feature] += values[feature];
    }
    ++count;
  }

  Features mean() const {
    if (count == 0) throw std::logic_error("empty matched accumulator");
    Features result{};
    for (std::size_t feature = 0; feature < result.size(); ++feature) {
      result[feature] = sum[feature] / count;
    }
    return result;
  }
};

struct SeedPairAccumulator {
  Features exact_sum{};
  Features oracle_sum{};
  int strata = 0;
};

struct SeedPair {
  std::uint32_t seed = 0;
  Features exact{};
  Features oracle{};
  int matched_strata = 0;
};

struct MatchedData {
  std::vector<SeedPair> seeds;
  int matched_strata = 0;
  int exact_records = 0;
  int oracle_records = 0;
};

std::map<MatchKey, Accumulator> aggregateRecords(
    const std::vector<GameResult>& games, int& record_count) {
  std::map<MatchKey, Accumulator> result;
  record_count = 0;
  for (const GameResult& game : games) {
    for (const StateRecord& record : game.records) {
      const MatchKey key{record.seed, record.move_band, record.rise_phase,
                         record.occupancy_bin, record.height_bin};
      result[key].add(record.features);
      ++record_count;
    }
  }
  return result;
}

MatchedData matchTopology(const Cohort& cohort) {
  MatchedData result;
  const auto exact = aggregateRecords(cohort.exact, result.exact_records);
  const auto oracle = aggregateRecords(cohort.oracle, result.oracle_records);
  std::map<std::uint32_t, SeedPairAccumulator> by_seed;
  for (const auto& [key, exact_values] : exact) {
    const auto found = oracle.find(key);
    if (found == oracle.end()) continue;
    const Features exact_mean = exact_values.mean();
    const Features oracle_mean = found->second.mean();
    SeedPairAccumulator& seed = by_seed[key.seed];
    for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
      seed.exact_sum[feature] += exact_mean[feature];
      seed.oracle_sum[feature] += oracle_mean[feature];
    }
    ++seed.strata;
    ++result.matched_strata;
  }
  result.seeds.reserve(by_seed.size());
  for (const auto& [seed, values] : by_seed) {
    SeedPair pair;
    pair.seed = seed;
    pair.matched_strata = values.strata;
    for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
      pair.exact[feature] = values.exact_sum[feature] / values.strata;
      pair.oracle[feature] = values.oracle_sum[feature] / values.strata;
    }
    result.seeds.push_back(pair);
  }
  return result;
}

struct FeatureStats {
  int seeds = 0;
  double exact_mean = 0.0;
  double oracle_mean = 0.0;
  double delta = 0.0;
  double standard_deviation = 0.0;
  double effect = 0.0;
  bool effect_defined = false;
};

FeatureStats featureStats(const std::vector<SeedPair>& pairs,
                          std::size_t feature, std::uint32_t begin,
                          std::uint32_t end) {
  FeatureStats result;
  std::vector<double> deltas;
  for (const SeedPair& pair : pairs) {
    if (pair.seed < begin || pair.seed >= end) continue;
    result.exact_mean += pair.exact[feature];
    result.oracle_mean += pair.oracle[feature];
    deltas.push_back(pair.oracle[feature] - pair.exact[feature]);
  }
  result.seeds = static_cast<int>(deltas.size());
  if (deltas.empty()) return result;
  result.exact_mean /= deltas.size();
  result.oracle_mean /= deltas.size();
  result.delta =
      std::accumulate(deltas.begin(), deltas.end(), 0.0) / deltas.size();
  if (deltas.size() > 1) {
    double squared = 0.0;
    for (const double delta : deltas) {
      squared += (delta - result.delta) * (delta - result.delta);
    }
    result.standard_deviation =
        std::sqrt(squared / static_cast<double>(deltas.size() - 1));
    if (result.standard_deviation > 1.0e-12) {
      result.effect = result.delta / result.standard_deviation;
      result.effect_defined = true;
    }
  }
  return result;
}

struct FeatureResult {
  std::size_t feature = 0;
  FeatureStats overall;
  FeatureStats first_half;
  FeatureStats second_half;
  bool direction_stable = false;
  bool stable_hypothesis = false;
  double stability_rank = 0.0;
};

std::vector<FeatureResult> analyzeFeatures(const MatchedData& matched,
                                           const Config& config) {
  const std::uint32_t split =
      config.seed_start + static_cast<std::uint32_t>(config.games / 2);
  const std::uint32_t end =
      config.seed_start + static_cast<std::uint32_t>(config.games);
  std::vector<FeatureResult> result;
  result.reserve(kFeatureCount);
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    FeatureResult item;
    item.feature = feature;
    item.overall =
        featureStats(matched.seeds, feature, config.seed_start, end);
    item.first_half =
        featureStats(matched.seeds, feature, config.seed_start, split);
    item.second_half = featureStats(matched.seeds, feature, split, end);
    item.direction_stable =
        item.first_half.delta * item.second_half.delta > 0.0;
    const auto strong_enough = [](const FeatureStats& stats) {
      return !stats.effect_defined ||
             std::abs(stats.effect) >= kMinimumStableEffect;
    };
    item.stable_hypothesis =
        item.direction_stable &&
        item.first_half.seeds >= kMinimumSeedsPerHalf &&
        item.second_half.seeds >= kMinimumSeedsPerHalf &&
        strong_enough(item.first_half) && strong_enough(item.second_half);
    const auto magnitude = [](const FeatureStats& stats) {
      return stats.effect_defined ? std::abs(stats.effect) :
                                    (std::abs(stats.delta) > 1.0e-12 ? 99.0
                                                                    : 0.0);
    };
    item.stability_rank =
        std::min(magnitude(item.first_half), magnitude(item.second_half));
    result.push_back(item);
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const FeatureResult& left,
                      const FeatureResult& right) {
                     if (left.stable_hypothesis != right.stable_hypothesis) {
                       return left.stable_hypothesis;
                     }
                     return left.stability_rank > right.stability_rank;
                   });
  return result;
}

struct PolicySummary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double waves_per_move = 0.0;
  double mean_maximum_chain_depth = 0.0;
  double work_per_move = 0.0;
  double generated_per_move = 0.0;
  double completed_depth = 0.0;
  double depth_three_rate = 0.0;
  std::size_t peak_candidates = 0;
  double aggregate_game_seconds = 0.0;
};

PolicySummary summarizePolicy(const std::vector<GameResult>& games,
                              std::uint32_t begin, std::uint32_t end) {
  PolicySummary result;
  std::uint64_t moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::uint64_t waves = 0;
  std::uint64_t work = 0;
  std::uint64_t generated = 0;
  std::uint64_t depth_sum = 0;
  std::uint64_t depth_three = 0;
  for (const GameResult& game : games) {
    if (game.seed < begin || game.seed >= end) continue;
    ++result.games;
    result.mean_score += game.score;
    result.mean_moves += game.moves;
    result.censored += game.censored;
    result.mean_maximum_chain_depth += game.maximum_chain_depth;
    result.aggregate_game_seconds += game.elapsed_seconds;
    result.peak_candidates =
        std::max(result.peak_candidates, game.peak_candidates);
    moves += game.moves;
    clears += game.cleared;
    reveals += game.revealed;
    waves += game.waves;
    work += game.work;
    generated += game.generated;
    depth_sum += game.completed_depth_sum;
    depth_three += game.completed_depth_three;
  }
  if (result.games == 0) return result;
  result.mean_score /= result.games;
  result.mean_moves /= result.games;
  result.mean_maximum_chain_depth /= result.games;
  const double move_count = static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.clears_per_move = clears / move_count;
  result.reveals_per_move = reveals / move_count;
  result.waves_per_move = waves / move_count;
  result.work_per_move = work / move_count;
  result.generated_per_move = generated / move_count;
  result.completed_depth = depth_sum / move_count;
  result.depth_three_rate = depth_three / move_count;
  return result;
}

struct PairedOutcome {
  int games = 0;
  double score_delta = 0.0;
  double move_delta = 0.0;
  int score_wins = 0;
  int move_wins = 0;
  int score_ties = 0;
  int move_ties = 0;
};

PairedOutcome pairedOutcome(const Cohort& cohort, std::uint32_t begin,
                            std::uint32_t end) {
  if (cohort.exact.size() != cohort.oracle.size()) {
    throw std::logic_error("oracle audit cohort is not paired");
  }
  PairedOutcome result;
  for (std::size_t game = 0; game < cohort.exact.size(); ++game) {
    const GameResult& exact = cohort.exact[game];
    const GameResult& oracle = cohort.oracle[game];
    if (exact.seed != oracle.seed) {
      throw std::logic_error("oracle audit seed pairing failed");
    }
    if (exact.seed < begin || exact.seed >= end) continue;
    ++result.games;
    result.score_delta += oracle.score - exact.score;
    result.move_delta += oracle.moves - exact.moves;
    result.score_wins += oracle.score > exact.score;
    result.move_wins += oracle.moves > exact.moves;
    result.score_ties += oracle.score == exact.score;
    result.move_ties += oracle.moves == exact.moves;
  }
  if (result.games > 0) {
    result.score_delta /= result.games;
    result.move_delta /= result.games;
  }
  return result;
}

void writeOptionalNumber(std::ostream& output, double value, bool defined) {
  if (defined && std::isfinite(value)) output << value;
  else output << "null";
}

void writePolicySummary(std::ostream& output, const PolicySummary& summary) {
  output << "{\"games\":" << summary.games
         << ",\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"censored\":" << summary.censored
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"wavesPerMove\":" << summary.waves_per_move
         << ",\"meanMaximumChainDepth\":"
         << summary.mean_maximum_chain_depth
         << ",\"workPerMove\":" << summary.work_per_move
         << ",\"generatedPerMove\":" << summary.generated_per_move
         << ",\"meanCompletedDepth\":" << summary.completed_depth
         << ",\"depthThreeRate\":" << summary.depth_three_rate
         << ",\"peakCandidates\":" << summary.peak_candidates
         << ",\"aggregateGameSeconds\":"
         << summary.aggregate_game_seconds << '}';
}

void writePairedOutcome(std::ostream& output, const PairedOutcome& outcome) {
  output << "{\"games\":" << outcome.games
         << ",\"meanScoreDelta\":" << outcome.score_delta
         << ",\"meanMoveDelta\":" << outcome.move_delta
         << ",\"scoreWins\":" << outcome.score_wins
         << ",\"moveWins\":" << outcome.move_wins
         << ",\"scoreTies\":" << outcome.score_ties
         << ",\"moveTies\":" << outcome.move_ties << '}';
}

void writeFeatureStats(std::ostream& output, const FeatureStats& stats) {
  output << "{\"seeds\":" << stats.seeds
         << ",\"exactMean\":" << stats.exact_mean
         << ",\"oracleMean\":" << stats.oracle_mean
         << ",\"oracleMinusExact\":" << stats.delta
         << ",\"seedDeltaSd\":" << stats.standard_deviation
         << ",\"pairedEffect\":";
  writeOptionalNumber(output, stats.effect, stats.effect_defined);
  output << '}';
}

std::string hexSeed(std::uint32_t seed) {
  std::ostringstream output;
  output << "0x" << std::hex << std::setw(8) << std::setfill('0') << seed;
  return output.str();
}

void writeGames(std::ostream& output, const Cohort& cohort) {
  output << '[';
  for (std::size_t game = 0; game < cohort.exact.size(); ++game) {
    if (game > 0) output << ',';
    const GameResult& exact = cohort.exact[game];
    const GameResult& oracle = cohort.oracle[game];
    output << "{\"seed\":\"" << hexSeed(exact.seed)
           << "\",\"half\":"
           << (game < cohort.exact.size() / 2 ? 1 : 2)
           << ",\"exact\":{\"score\":" << exact.score
           << ",\"moves\":" << exact.moves
           << ",\"censored\":" << (exact.censored ? "true" : "false")
           << ",\"work\":" << exact.work << "},\"oracle\":{\"score\":"
           << oracle.score << ",\"moves\":" << oracle.moves
           << ",\"censored\":" << (oracle.censored ? "true" : "false")
           << ",\"generated\":" << oracle.generated
           << ",\"deduplicated\":" << oracle.deduplicated << "}}";
  }
  output << ']';
}

void writeJson(const Config& config, const Cohort& cohort,
               const MatchedData& matched,
               const std::vector<FeatureResult>& features) {
  const std::uint32_t split =
      config.seed_start + static_cast<std::uint32_t>(config.games / 2);
  const std::uint32_t end =
      config.seed_start + static_cast<std::uint32_t>(config.games);
  const PolicySummary exact_all =
      summarizePolicy(cohort.exact, config.seed_start, end);
  const PolicySummary oracle_all =
      summarizePolicy(cohort.oracle, config.seed_start, end);
  const PolicySummary exact_first =
      summarizePolicy(cohort.exact, config.seed_start, split);
  const PolicySummary oracle_first =
      summarizePolicy(cohort.oracle, config.seed_start, split);
  const PolicySummary exact_second =
      summarizePolicy(cohort.exact, split, end);
  const PolicySummary oracle_second =
      summarizePolicy(cohort.oracle, split, end);
  std::ofstream output(config.json);
  if (!output) throw std::runtime_error("could not open oracle audit JSON");
  output << std::setprecision(10)
         << "{\n  \"format\":\"drop7-oracle-topology-audit-v1\",\n"
         << "  \"warning\":\"analysis-only privileged oracle; never deploy oracle-dependent inputs\",\n"
         << "  \"mechanics\":{\"levelBonus\":7000},\n"
         << "  \"seedPolicy\":{\"trainingOnly\":true,\"forbiddenFamiliesInspected\":false,"
            "\"seedStart\":\""
         << hexSeed(config.seed_start) << "\",\"seedEndExclusive\":\""
         << hexSeed(end) << "\",\"sameSeedsAcrossPolicies\":true},\n"
         << "  \"configuration\":{\"gamesPerPolicy\":" << config.games
         << ",\"maximumMoves\":" << config.maximum_moves
         << ",\"parallelism\":" << config.parallelism
         << ",\"exact\":{\"depth\":3,\"chanceSamples\":5,"
            "\"maximumWork\":1000000,\"maximumCacheEntries\":40000,"
            "\"publicStateOnly\":true},\"oracle\":{\"privilegedFutureAware\":true,"
            "\"depth\":"
         << config.oracle_depth << ",\"beamWidth\":" << config.oracle_beam
         << "}},\n"
         << "  \"matching\":{\"unit\":\"same-seed topology stratum then one mean delta per seed\","
            "\"moveBandWidth\":"
         << kMoveBandWidth << ",\"risePhase\":\"exact movesRemaining 1..5\","
            "\"occupancyBinWidth\":"
         << kOccupancyBinWidth << ",\"maximumHeightBinWidth\":"
         << kHeightBinWidth << ",\"exactRecords\":"
         << matched.exact_records << ",\"oracleRecords\":"
         << matched.oracle_records << ",\"matchedStrata\":"
         << matched.matched_strata << ",\"matchedSeeds\":"
         << matched.seeds.size() << "},\n"
         << "  \"stabilityRule\":{\"seedHalves\":[\""
         << hexSeed(config.seed_start) << ".." << hexSeed(split - 1)
         << "\",\"" << hexSeed(split) << ".." << hexSeed(end - 1)
         << "\"],\"minimumSeedsPerHalf\":" << kMinimumSeedsPerHalf
         << ",\"sameNonzeroDirectionRequired\":true,"
            "\"minimumAbsoluteEffectEachHalf\":"
         << kMinimumStableEffect << "},\n"
         << "  \"outcomes\":{\"overall\":{\"exact\":";
  writePolicySummary(output, exact_all);
  output << ",\"oracle\":";
  writePolicySummary(output, oracle_all);
  output << ",\"pairedOracleMinusExact\":";
  writePairedOutcome(output, pairedOutcome(cohort, config.seed_start, end));
  output << "},\"firstHalf\":{\"exact\":";
  writePolicySummary(output, exact_first);
  output << ",\"oracle\":";
  writePolicySummary(output, oracle_first);
  output << ",\"pairedOracleMinusExact\":";
  writePairedOutcome(output,
                     pairedOutcome(cohort, config.seed_start, split));
  output << "},\"secondHalf\":{\"exact\":";
  writePolicySummary(output, exact_second);
  output << ",\"oracle\":";
  writePolicySummary(output, oracle_second);
  output << ",\"pairedOracleMinusExact\":";
  writePairedOutcome(output, pairedOutcome(cohort, split, end));
  output << "}},\n  \"games\":";
  writeGames(output, cohort);
  output << ",\n  \"features\":[";
  for (std::size_t index = 0; index < features.size(); ++index) {
    if (index > 0) output << ',';
    const FeatureResult& feature = features[index];
    const FeatureSpec& spec = kFeatureSpecs[feature.feature];
    output << "{\"name\":\"" << spec.name << "\",\"category\":\""
           << spec.category << "\",\"kind\":\"" << spec.kind
           << "\",\"interpretation\":\"" << spec.interpretation
           << "\",\"overall\":";
    writeFeatureStats(output, feature.overall);
    output << ",\"firstHalf\":";
    writeFeatureStats(output, feature.first_half);
    output << ",\"secondHalf\":";
    writeFeatureStats(output, feature.second_half);
    output << ",\"directionStable\":"
           << (feature.direction_stable ? "true" : "false")
           << ",\"stableHypothesis\":"
           << (feature.stable_hypothesis ? "true" : "false")
           << ",\"candidateDirection\":\""
           << (feature.overall.delta > 0 ? "higher" : "lower")
           << "\",\"stabilityRank\":" << feature.stability_rank << '}';
  }
  output << "],\n  \"limits\":["
            "\"oracle sees realized future random streams and is not deployable\","
            "\"beam pruning makes the oracle an approximate privileged upper bound\","
            "\"matched observational differences are hypotheses, not causal feature weights\","
            "\"transition-outcome features require a separate public-state predictor before policy use\","
            "\"correlated features and survivor selection remain after coarse topology matching\"],\n"
         << "  \"wallSeconds\":" << cohort.wall_seconds << "\n}\n";
}

std::string formatNumber(double value, int precision = 3) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(precision) << value;
  return output.str();
}

std::string formatEffect(const FeatureStats& stats) {
  return stats.effect_defined ? formatNumber(stats.effect, 2) : "n/a";
}

void writeReadme(const Config& config, const Cohort& cohort,
                 const MatchedData& matched,
                 const std::vector<FeatureResult>& features) {
  const std::uint32_t end =
      config.seed_start + static_cast<std::uint32_t>(config.games);
  const PolicySummary exact =
      summarizePolicy(cohort.exact, config.seed_start, end);
  const PolicySummary oracle =
      summarizePolicy(cohort.oracle, config.seed_start, end);
  const PairedOutcome paired =
      pairedOutcome(cohort, config.seed_start, end);
  std::ofstream output(config.readme);
  if (!output) throw std::runtime_error("could not open oracle audit README");
  output << "# Drop7 oracle-topology audit\n\n"
         << "> Analysis only. The oracle sees the realized future disc and reveal streams. "
            "It is neither fair nor deployable, and none of its privileged inputs may enter a policy.\n\n"
         << "## Scope and method\n\n"
         << "The audit ran " << config.games << " exact-depth-3 games and "
         << config.games << " privileged depth-" << config.oracle_depth
         << ", beam-" << config.oracle_beam << " games on the same fresh "
         << hexSeed(config.seed_start) << "–" << hexSeed(end - 1)
         << " training seeds. Mechanics use the corrected 7,000-point level bonus. "
            "No `0x7d` or `0xd7` seed was inspected.\n\n"
         << "States were matched within the same seed, 20-move band, exact rise phase, "
            "four-cell occupancy bin, and two-row maximum-height bin. Each matched topology "
            "stratum was averaged first, then each seed contributed one mean delta so repeated "
            "states do not masquerade as independent games. There were "
         << matched.matched_strata << " matched strata across "
         << matched.seeds.size() << " seeds (" << matched.exact_records
         << " exact states and " << matched.oracle_records
         << " oracle states before matching).\n\n"
         << "A feature is called direction-stable only when both eight-seed halves have "
            "the same nonzero oracle-minus-exact direction, at least six matched seeds, and "
            "an absolute paired effect of at least 0.10 in each half. This is a hypothesis "
            "filter, not a significance or causality claim.\n\n"
         << "## Outcomes\n\n"
         << "| Policy | Games | Mean score | Mean moves | Clears/move | Reveals/move | Waves/move | Capped |\n"
            "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n"
         << "| Exact d3 | " << exact.games << " | "
         << formatNumber(exact.mean_score, 0) << " | "
         << formatNumber(exact.mean_moves, 2) << " | "
         << formatNumber(exact.clears_per_move) << " | "
         << formatNumber(exact.reveals_per_move) << " | "
         << formatNumber(exact.waves_per_move) << " | " << exact.censored
         << " |\n"
         << "| Privileged oracle | " << oracle.games << " | "
         << formatNumber(oracle.mean_score, 0) << " | "
         << formatNumber(oracle.mean_moves, 2) << " | "
         << formatNumber(oracle.clears_per_move) << " | "
         << formatNumber(oracle.reveals_per_move) << " | "
         << formatNumber(oracle.waves_per_move) << " | " << oracle.censored
         << " |\n\n"
         << "Paired oracle-minus-exact outcome: "
         << (paired.score_delta >= 0 ? "+" : "")
         << formatNumber(paired.score_delta, 0) << " points and "
         << (paired.move_delta >= 0 ? "+" : "")
         << formatNumber(paired.move_delta, 2) << " moves; score wins "
         << paired.score_wins << '/' << paired.games << ", move wins "
         << paired.move_wins << '/' << paired.games << ". Exact search completed "
         << formatNumber(100.0 * exact.depth_three_rate, 2)
         << "% of depth-3 iterations.\n\n"
         << "## Direction-stable topology signals\n\n"
         << "Positive deltas mean the matched oracle states/outcomes are higher. "
            "Transition outcomes describe the following realized move and cannot be used "
            "directly by a fair state heuristic.\n\n"
         << "| Feature | Category | Kind | Exact | Oracle | Delta | Effect H1 | Effect H2 | Candidate direction |\n"
            "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- |\n";
  int stable_count = 0;
  for (const FeatureResult& feature : features) {
    if (!feature.stable_hypothesis) continue;
    ++stable_count;
    const FeatureSpec& spec = kFeatureSpecs[feature.feature];
    output << "| " << spec.name << " | " << spec.category << " | "
           << spec.kind << " | " << formatNumber(feature.overall.exact_mean)
           << " | " << formatNumber(feature.overall.oracle_mean) << " | "
           << (feature.overall.delta >= 0 ? "+" : "")
           << formatNumber(feature.overall.delta) << " | "
           << formatEffect(feature.first_half) << " | "
           << formatEffect(feature.second_half) << " | "
           << (feature.overall.delta > 0 ? "higher" : "lower") << " |\n";
  }
  if (stable_count == 0) {
    output << "| _None passed the split-half rule_ | | | | | | | | |\n";
  }
  output << "\n## Next heuristic hypothesis\n\n";
  for (const std::string_view category : {
           "highNumberTrench", "coverAccess", "storedEnergy",
           "coverAltitudeDamage", "lowNumberClog",
           "clearRevealThroughput"}) {
    int emitted = 0;
    for (const FeatureResult& feature : features) {
      const FeatureSpec& spec = kFeatureSpecs[feature.feature];
      if (!feature.stable_hypothesis || spec.category != category) continue;
      if (emitted == 0) output << "- **" << category << ":** ";
      else output << "; ";
      output << (feature.overall.delta > 0 ? "retain/reward higher "
                                          : "penalize/reduce ")
             << spec.name;
      ++emitted;
      if (emitted == 3) break;
    }
    if (emitted > 0) output << ".\n";
  }
  output << "\nThe safest next experiment is a fair exact-d3 leaf ablation containing only "
            "the split-stable **state** signals above, with signs fixed by this audit. "
            "Keep transition-throughput signals as supervised prediction targets rather "
            "than rewards until a public-state predictor validates out of sample.\n\n"
         << "## Limits\n\n"
         << "- The oracle is an approximate privileged upper bound: beam pruning can discard "
            "the globally best future path.\n"
         << "- Matching reduces obvious phase/load confounding but does not remove survivor "
            "selection, feature correlation, or policy-induced state-distribution shift.\n"
         << "- Sixteen paired seeds are adequate for hypothesis generation, not promotion. "
            "Every proposed feature still needs a fresh fair-policy ablation.\n";
}

int positiveInteger(std::string_view text, std::string_view flag) {
  std::size_t consumed = 0;
  const unsigned long long value =
      std::stoull(std::string(text), &consumed, 0);
  if (consumed != text.size() || value == 0 ||
      value > static_cast<unsigned long long>(
                  std::numeric_limits<int>::max())) {
    throw std::invalid_argument(std::string(flag) +
                                " must be a positive integer");
  }
  return static_cast<int>(value);
}

std::uint32_t parseSeed(std::string_view text) {
  std::size_t consumed = 0;
  const unsigned long long value =
      std::stoull(std::string(text), &consumed, 0);
  if (consumed != text.size() ||
      value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("seed must be a uint32 integer");
  }
  return static_cast<std::uint32_t>(value);
}

void validateConfig(Config& config) {
  if (config.games < 12 || config.games % 2 != 0) {
    throw std::invalid_argument(
        "oracle audit games must be an even number of at least twelve");
  }
  if (config.maximum_moves < 20 || config.maximum_moves > 1'000) {
    throw std::invalid_argument("maximum moves must be from 20 to 1000");
  }
  if (config.oracle_depth < 1 || config.oracle_depth > 12 ||
      config.oracle_beam < 1 || config.oracle_beam > 2'048) {
    throw std::invalid_argument("oracle bounds are invalid");
  }
  config.parallelism = std::min(config.parallelism, 16);
  const std::uint64_t end =
      static_cast<std::uint64_t>(config.seed_start) + config.games;
  if (config.seed_start < kTrainingStart || end > kTrainingEnd ||
      (config.seed_start & 0xff00'0000u) != 0x3d00'0000u) {
    throw std::invalid_argument(
        "oracle audit is restricted to the 0x3d training family");
  }
}

Config parseConfig(int argc, char** argv) {
  Config config;
  for (int index = 2; index < argc; ++index) {
    const std::string flag = argv[index];
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + flag);
    }
    const std::string_view value = argv[++index];
    if (flag == "--seed-start") {
      config.seed_start = parseSeed(value);
    } else if (flag == "--games") {
      config.games = positiveInteger(value, flag);
    } else if (flag == "--max-moves") {
      config.maximum_moves = positiveInteger(value, flag);
    } else if (flag == "--oracle-depth") {
      config.oracle_depth = positiveInteger(value, flag);
    } else if (flag == "--oracle-beam") {
      config.oracle_beam = positiveInteger(value, flag);
    } else if (flag == "--parallel") {
      config.parallelism = positiveInteger(value, flag);
    } else if (flag == "--json") {
      config.json = value;
    } else if (flag == "--readme") {
      config.readme = value;
    } else {
      throw std::invalid_argument("unknown oracle audit argument " + flag);
    }
  }
  validateConfig(config);
  return config;
}

bool selfTest(std::ostream& output) {
  constexpr std::uint32_t seed = 0x3d70'ff00u;
  State state = initialHeadlessState(seed);
  const OraclePlan first = planOracleMove(state, seed, 3, 16);
  const OraclePlan repeat = planOracleMove(state, seed, 3, 16);
  const bool oracle_deterministic =
      first.column == repeat.column &&
      first.stats.generated == repeat.stats.generated &&
      first.stats.deduplicated == repeat.stats.deduplicated;
  const bool oracle_legal = isLegal(state.board, first.column);

  cfpi::BehaviorOptions exact;
  exact.max_depth = 3;
  exact.chance_samples = 5;
  exact.max_work = 1'000'000;
  exact.max_cache_entries = 40'000;
  cfpi::BehaviorMetrics exact_metrics;
  const int exact_action =
      cfpi::chooseBehaviorAction(publicState(state), exact, &exact_metrics);
  State metadata_changed = state;
  metadata_changed.score = 999'999;
  metadata_changed.level = 77;
  metadata_changed.moves_played = 444;
  const int metadata_action =
      cfpi::chooseBehaviorAction(publicState(metadata_changed), exact);
  const bool public_exact = exact_action == metadata_action &&
                            exact_metrics.completed_depth == 3 &&
                            isLegal(state.board, exact_action);

  const State before = state;
  MoveResult move;
  const bool moved = playHeadlessMove(state, seed, first.column, move);
  const Features features = extractFeatures(before, move);
  State mirrored = before;
  mirrored.board = cfpi::detail::mirrorBoard(before.board);
  const Features reflected = extractFeatures(mirrored, move);
  bool reflection_safe = true;
  bool finite = true;
  for (std::size_t feature = 0; feature < kFeatureCount; ++feature) {
    reflection_safe =
        reflection_safe && std::abs(features[feature] - reflected[feature]) <
                               1.0e-9;
    finite = finite && std::isfinite(features[feature]);
  }
  const bool passed = kLevelBonus == 7'000 && oracle_deterministic &&
                      oracle_legal && public_exact && moved &&
                      reflection_safe && finite &&
                      kFeatureSpecs.size() == kFeatureCount;
  output << "ORACLE_TOPOLOGY_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"levelBonus\":" << kLevelBonus
         << ",\"oracleDeterministic\":"
         << (oracle_deterministic ? "true" : "false")
         << ",\"oracleLegal\":" << (oracle_legal ? "true" : "false")
         << ",\"publicExactD3\":"
         << (public_exact ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"finite\":" << (finite ? "true" : "false")
         << ",\"trainingSeedOnly\":true}\n";
  return passed;
}

int run(const Config& config, std::ostream& output) {
  const Cohort cohort = runCohort(config);
  const MatchedData matched = matchTopology(cohort);
  const std::vector<FeatureResult> features =
      analyzeFeatures(matched, config);
  writeJson(config, cohort, matched, features);
  writeReadme(config, cohort, matched, features);
  int stable = 0;
  for (const FeatureResult& feature : features) {
    stable += feature.stable_hypothesis;
  }
  const std::uint32_t end =
      config.seed_start + static_cast<std::uint32_t>(config.games);
  const PolicySummary exact =
      summarizePolicy(cohort.exact, config.seed_start, end);
  const PolicySummary oracle =
      summarizePolicy(cohort.oracle, config.seed_start, end);
  const PairedOutcome paired =
      pairedOutcome(cohort, config.seed_start, end);
  output << std::fixed << std::setprecision(3)
         << "ORACLE_TOPOLOGY_RESULT {\"trainingSeedOnly\":true"
         << ",\"gamesPerPolicy\":" << config.games
         << ",\"exactScore\":" << exact.mean_score
         << ",\"exactMoves\":" << exact.mean_moves
         << ",\"oracleScore\":" << oracle.mean_score
         << ",\"oracleMoves\":" << oracle.mean_moves
         << ",\"scoreDelta\":" << paired.score_delta
         << ",\"moveDelta\":" << paired.move_delta
         << ",\"matchedStrata\":" << matched.matched_strata
         << ",\"matchedSeeds\":" << matched.seeds.size()
         << ",\"stableFeatures\":" << stable
         << ",\"json\":\"" << config.json << "\",\"readme\":\""
         << config.readme << "\"}\n";
  return 0;
}

}  // namespace drop7::oracle_topology

#ifndef DROP7_ORACLE_TOPOLOGY_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return drop7::oracle_topology::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--run") {
      return drop7::oracle_topology::run(
          drop7::oracle_topology::parseConfig(argc, argv), std::cout);
    }
    std::cerr
        << "usage: drop7_oracle_topology_audit --self-test | --run "
           "[--seed-start 0x3d...] [--games EVEN>=12] [--max-moves N] "
           "[--oracle-depth N] [--oracle-beam N] [--parallel N] "
           "[--json PATH] [--readme PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_oracle_topology_audit: " << error.what() << '\n';
    return 1;
  }
}
#endif
