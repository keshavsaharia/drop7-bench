#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#define DROP7_ORACLE_TOPOLOGY_LIBRARY
#include "oracle-topology-audit.cpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/resource.h>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

// A leakage-controlled test of whether the observable topology of the
// privileged oracle can provide a useful residual for fair-only depth four.
// Oracle access is confined to the 0x3d fitting families.  The learned model
// receives only a reflection-canonical board; nuisance variables are used to
// balance examples but never enter its input.  Fresh gameplay is sealed behind
// both a prediction gate and a public-state policy-diagnostic gate.
namespace drop7::oracle_topology_residual {

namespace fair = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;
namespace oracle = drop7::oracle_topology;
using Clock = std::chrono::steady_clock;

constexpr std::uint32_t kTrainingSeedStart = 0x3d9c'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3d9d'0000u;
constexpr std::uint32_t kScreenSeedStart = 0x3ea9'0000u;
constexpr std::uint32_t kConfirmationSeedStart = 0x3eaa'0000u;
constexpr int kTrainingGames = 16;
constexpr int kHeldoutGames = 8;
constexpr int kTrainingMaximumMoves = 160;
constexpr int kGameplayMaximumMoves = 1'000;
constexpr int kScreenGames = 8;
constexpr int kConfirmationGames = 16;
constexpr int kOracleDepth = 4;
constexpr int kOracleBeam = 128;
constexpr int kParallelism = 4;
constexpr int kMoveBandWidth = 20;
constexpr int kMaximumPairsPerStratum = 16;
constexpr int kDiagnosticStates = 24;

constexpr int kCellKinds = 10;
constexpr int kInputCount = kCellCount * kCellKinds;
constexpr int kHidden = 8;
constexpr int kParameterCount =
    kInputCount * kHidden + kHidden + kHidden + 1;
constexpr int kTrainingEpochs = 240;
constexpr int kBatchSize = 64;
constexpr double kLearningRate = 0.004;
constexpr double kL2 = 0.0002;
constexpr std::array<double, 6> kCoefficientGrid{{
    250.0, 500.0, 1'000.0, 2'000.0, 4'000.0, 8'000.0,
}};
constexpr double kMinimumHeldoutAuc = 0.58;
constexpr double kMinimumHeldoutPairAccuracy = 0.56;
constexpr double kMinimumHalfPairAccuracy = 0.53;
constexpr double kMinimumDiagnosticSwitchRate = 0.04;
constexpr double kMaximumDiagnosticSwitchRate = 0.35;

static_assert(kLevelBonus == 7'000);
static_assert(kParameterCount == 3'937);
static_assert(kParameterCount < 4'000);
static_assert(kTrainingSeedStart + kTrainingGames < kHeldoutSeedStart);
static_assert(kHeldoutSeedStart + kHeldoutGames < 0x3e00'0000u);
static_assert(kScreenSeedStart + kScreenGames < kConfirmationSeedStart);
static_assert(kConfirmationSeedStart + kConfirmationGames < 0x3eab'0000u);
static_assert(fair::kCandidateDepth == 4);
static_assert(fair::kMaximumWork == 3'200'000);
static_assert(fair::kMaximumCacheEntries == 60'000);

std::mutex progress_mutex;

std::uint64_t peakRssBytes() {
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;
#endif
}

struct Stratum {
  int move_band = 0;
  int rise_phase = 0;
  int occupied = 0;
  int maximum_height = 0;

  auto operator<=>(const Stratum&) const = default;
};

enum class Label : std::uint8_t { kFair = 0, kOracle = 1 };

struct RawRecord {
  std::uint32_t seed = 0;
  Stratum stratum{};
  Board board{};
  Label label = Label::kFair;
};

// This is the complete optimizer-facing example type.  It intentionally has
// no seed, score, level, move index, history, next disc, or random-tape field.
struct LearningExample {
  std::array<std::uint16_t, kCellCount> active{};
  double label = 0.0;
};

static_assert(std::tuple_size_v<decltype(LearningExample::active)> ==
              kCellCount);

struct MatchedPair {
  LearningExample fair{};
  LearningExample oracle{};
  std::uint32_t diagnostic_seed = 0;
  Stratum diagnostic_stratum{};
};

struct MatchedDataset {
  std::vector<LearningExample> examples;
  std::vector<MatchedPair> pairs;
  int raw_fair = 0;
  int raw_oracle = 0;
  int strata = 0;
};

struct PublicDiagnosticState {
  State state{};
  Stratum stratum{};
};

// Independent training-only behavior-clone material.  It is serialized after
// collection but never read by matching, fitting, prediction, or policy gates.
struct BehaviorLabel {
  Board board{};
  std::uint8_t next_disc = 1;
  int moves_remaining = kMovesPerLevel;
  int action = -1;
  std::array<double, kBoardSize> root_values{};
};

struct CollectedSplit {
  std::vector<RawRecord> fair;
  std::vector<RawRecord> oracle;
  std::vector<PublicDiagnosticState> fair_diagnostics;
  std::vector<BehaviorLabel> behavior_labels;
  double wall_seconds = 0.0;
};

int occupiedCells(const Board& board) {
  return static_cast<int>(std::count_if(
      board.begin(), board.end(), [](std::uint8_t cell) {
        return cell != kEmpty;
      }));
}

int maximumHeight(const Board& board) {
  int result = 0;
  for (const int height : cfpi::detail::columnHeights(board)) {
    result = std::max(result, height);
  }
  return result;
}

Stratum stratumFor(const State& state) {
  return {state.moves_played / kMoveBandWidth, state.moves_remaining,
          occupiedCells(state.board), maximumHeight(state.board)};
}

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

Board canonicalBoard(const Board& board) {
  const Board reflected = cfpi::detail::mirrorBoard(board);
  return std::lexicographical_compare(reflected.begin(), reflected.end(),
                                      board.begin(), board.end())
             ? reflected
             : board;
}

LearningExample makeLearningExample(const Board& source, Label label) {
  const Board board = canonicalBoard(source);
  LearningExample result;
  result.label = label == Label::kOracle ? 1.0 : 0.0;
  for (int cell = 0; cell < kCellCount; ++cell) {
    const int kind = static_cast<int>(board[cell]);
    if (kind < 0 || kind >= kCellKinds) {
      throw std::logic_error("invalid Drop7 cell in NNUE input");
    }
    result.active[cell] = static_cast<std::uint16_t>(
        cell * kCellKinds + kind);
  }
  return result;
}

RawRecord makeRawRecord(std::uint32_t seed, Label label,
                        const State& state) {
  return {seed, stratumFor(state), state.board, label};
}

BehaviorLabel makeBehaviorLabel(const State& source,
                                const fair::SearchDecision& decision) {
  const bool mirrored =
      cfpi::detail::mirroredRepresentationIsSmaller(source.board);
  BehaviorLabel result;
  result.board = mirrored ? cfpi::detail::mirrorBoard(source.board)
                          : source.board;
  result.next_disc = source.next_disc;
  result.moves_remaining = source.moves_remaining;
  result.action = mirrored ? kBoardSize - 1 - decision.action
                           : decision.action;
  for (int canonical_column = 0; canonical_column < kBoardSize;
       ++canonical_column) {
    const int source_column = mirrored
                                  ? kBoardSize - 1 - canonical_column
                                  : canonical_column;
    result.root_values[canonical_column] =
        decision.root_values[source_column];
  }
  return result;
}

void reportCollection(std::string_view split, std::string_view policy,
                      std::uint32_t seed, int moves, std::int64_t score) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << "topology-residual " << split << '-' << policy << " seed 0x"
            << std::hex << seed << std::dec << ' ' << score << " (" << moves
            << " moves)\n";
}

std::vector<RawRecord> collectFairGame(std::uint32_t seed, int maximum_moves,
                                       std::string_view split,
                                       std::vector<PublicDiagnosticState>*
                                           diagnostics,
                                       std::vector<BehaviorLabel>* labels) {
  State state = initialHeadlessState(seed);
  std::vector<RawRecord> records;
  records.reserve(maximum_moves);
  while (!state.game_over && state.moves_played < maximum_moves) {
    records.push_back(makeRawRecord(seed, Label::kFair, state));
    if (diagnostics != nullptr) {
      diagnostics->push_back({publicState(state), stratumFor(state)});
    }
    const fair::SearchDecision decision = fair::chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != 4 ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("fair D4 collection decision failed");
    }
    if (labels != nullptr) labels->push_back(makeBehaviorLabel(state, decision));
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("fair D4 collection transition failed");
    }
  }
  reportCollection(split, "fair-d4", seed, state.moves_played, state.score);
  return records;
}

std::vector<RawRecord> collectOracleGame(std::uint32_t seed,
                                         int maximum_moves,
                                         std::string_view split) {
  State state = initialHeadlessState(seed);
  std::vector<RawRecord> records;
  records.reserve(maximum_moves);
  while (!state.game_over && state.moves_played < maximum_moves) {
    records.push_back(makeRawRecord(seed, Label::kOracle, state));
    const oracle::OraclePlan plan =
        oracle::planOracleMove(state, seed, kOracleDepth, kOracleBeam);
    if (!isLegal(state.board, plan.column)) {
      throw std::runtime_error("oracle collection chose an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, plan.column, move)) {
      throw std::runtime_error("oracle collection transition failed");
    }
  }
  reportCollection(split, "oracle", seed, state.moves_played, state.score);
  return records;
}

CollectedSplit collectSplit(std::uint32_t seed_start, int games,
                            std::string_view split) {
  const auto started = Clock::now();
  std::vector<std::vector<RawRecord>> fair_by_game(games);
  std::vector<std::vector<RawRecord>> oracle_by_game(games);
  std::vector<std::vector<PublicDiagnosticState>> diagnostics_by_game(games);
  std::vector<std::vector<BehaviorLabel>> labels_by_game(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        fair_by_game[game] = collectFairGame(
            seed, kTrainingMaximumMoves, split, &diagnostics_by_game[game],
            &labels_by_game[game]);
        oracle_by_game[game] =
            collectOracleGame(seed, kTrainingMaximumMoves, split);
      }
    }));
  }
  for (auto& worker : workers) worker.get();

  CollectedSplit result;
  for (int game = 0; game < games; ++game) {
    result.fair.insert(result.fair.end(), fair_by_game[game].begin(),
                       fair_by_game[game].end());
    result.oracle.insert(result.oracle.end(), oracle_by_game[game].begin(),
                         oracle_by_game[game].end());
    result.fair_diagnostics.insert(result.fair_diagnostics.end(),
                                   diagnostics_by_game[game].begin(),
                                   diagnostics_by_game[game].end());
    result.behavior_labels.insert(result.behavior_labels.end(),
                                  labels_by_game[game].begin(),
                                  labels_by_game[game].end());
  }
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

std::uint32_t stratumHash(const Stratum& value) {
  std::uint32_t hash = 0x811c'9dc5u;
  for (const int item : {value.move_band, value.rise_phase, value.occupied,
                         value.maximum_height}) {
    hash ^= static_cast<std::uint32_t>(item + 1);
    hash *= 0x0100'0193u;
  }
  return mix32(hash);
}

template <typename Value>
void deterministicShuffle(std::vector<Value>& values, std::uint32_t seed) {
  for (std::size_t remaining = values.size(); remaining > 1; --remaining) {
    seed = mix32(seed + static_cast<std::uint32_t>(remaining));
    const std::size_t selected = seed % remaining;
    std::swap(values[remaining - 1], values[selected]);
  }
}

MatchedDataset matchRecords(const std::vector<RawRecord>& fair_records,
                            const std::vector<RawRecord>& oracle_records) {
  std::map<Stratum, std::vector<const RawRecord*>> fair;
  std::map<Stratum, std::vector<const RawRecord*>> privileged;
  for (const RawRecord& record : fair_records) fair[record.stratum].push_back(&record);
  for (const RawRecord& record : oracle_records) {
    privileged[record.stratum].push_back(&record);
  }

  MatchedDataset result;
  result.raw_fair = static_cast<int>(fair_records.size());
  result.raw_oracle = static_cast<int>(oracle_records.size());
  for (auto& [stratum, fair_values] : fair) {
    const auto found = privileged.find(stratum);
    if (found == privileged.end()) continue;
    std::vector<const RawRecord*> oracle_values = found->second;
    deterministicShuffle(fair_values, stratumHash(stratum) ^ 0xfa17'0001u);
    deterministicShuffle(oracle_values,
                         stratumHash(stratum) ^ 0x0ace'0002u);
    const std::size_t count = std::min<std::size_t>(
        {fair_values.size(), oracle_values.size(),
         static_cast<std::size_t>(kMaximumPairsPerStratum)});
    if (count == 0) continue;
    ++result.strata;
    for (std::size_t index = 0; index < count; ++index) {
      MatchedPair pair;
      pair.fair = makeLearningExample(fair_values[index]->board, Label::kFair);
      pair.oracle =
          makeLearningExample(oracle_values[index]->board, Label::kOracle);
      pair.diagnostic_seed = fair_values[index]->seed;
      pair.diagnostic_stratum = stratum;
      result.examples.push_back(pair.fair);
      result.examples.push_back(pair.oracle);
      result.pairs.push_back(std::move(pair));
    }
  }
  deterministicShuffle(result.examples, 0x71a1'5eedu);
  return result;
}

struct NnueModel {
  std::array<double, kInputCount * kHidden> input{};
  std::array<double, kHidden> hidden_bias{};
  std::array<double, kHidden> output{};
  double output_bias = 0.0;
};

struct Forward {
  std::array<double, kHidden> preactivation{};
  std::array<double, kHidden> hidden{};
  double logit = 0.0;
};

Forward forward(const NnueModel& model, const LearningExample& example) {
  Forward result;
  result.preactivation = model.hidden_bias;
  for (const std::uint16_t active : example.active) {
    const std::size_t base = static_cast<std::size_t>(active) * kHidden;
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.preactivation[hidden] += model.input[base + hidden];
    }
  }
  result.logit = model.output_bias;
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    result.hidden[hidden] = std::max(0.0, result.preactivation[hidden]);
    result.logit += model.output[hidden] * result.hidden[hidden];
  }
  return result;
}

double boardLogit(const NnueModel& model, const Board& board) {
  return forward(model, makeLearningExample(board, Label::kFair)).logit;
}

double sigmoid(double value) {
  if (value >= 0.0) {
    const double inverse = std::exp(-value);
    return 1.0 / (1.0 + inverse);
  }
  const double exponential = std::exp(value);
  return exponential / (1.0 + exponential);
}

struct PackedParameters {
  std::array<double, kParameterCount> values{};
};

PackedParameters pack(const NnueModel& model) {
  PackedParameters result;
  std::size_t cursor = 0;
  for (double value : model.input) result.values[cursor++] = value;
  for (double value : model.hidden_bias) result.values[cursor++] = value;
  for (double value : model.output) result.values[cursor++] = value;
  result.values[cursor++] = model.output_bias;
  if (cursor != result.values.size()) throw std::logic_error("NNUE pack failed");
  return result;
}

NnueModel unpack(const PackedParameters& packed) {
  NnueModel result;
  std::size_t cursor = 0;
  for (double& value : result.input) value = packed.values[cursor++];
  for (double& value : result.hidden_bias) value = packed.values[cursor++];
  for (double& value : result.output) value = packed.values[cursor++];
  result.output_bias = packed.values[cursor++];
  if (cursor != packed.values.size()) throw std::logic_error("NNUE unpack failed");
  return result;
}

NnueModel initializedModel() {
  NnueModel result;
  std::uint32_t random = 0x4e4e'5545u;
  for (double& value : result.input) {
    random = mix32(random + 0x9e37'79b9u);
    value = (static_cast<double>(random) / 4'294'967'296.0 - 0.5) * 0.02;
  }
  for (double& value : result.output) {
    random = mix32(random + 0x9e37'79b9u);
    value = (static_cast<double>(random) / 4'294'967'296.0 - 0.5) * 0.04;
  }
  return result;
}

struct TrainingResult {
  NnueModel model{};
  double initial_loss = 0.0;
  double final_loss = 0.0;
};

double datasetLoss(const NnueModel& model,
                   const std::vector<LearningExample>& examples) {
  if (examples.empty()) throw std::invalid_argument("empty NNUE dataset");
  double result = 0.0;
  for (const LearningExample& example : examples) {
    const double logit = forward(model, example).logit;
    result += std::max(logit, 0.0) - logit * example.label +
              std::log1p(std::exp(-std::abs(logit)));
  }
  return result / examples.size();
}

TrainingResult trainModel(const std::vector<LearningExample>& source) {
  if (source.size() < 64) {
    throw std::invalid_argument("too few matched NNUE examples");
  }
  std::vector<LearningExample> examples = source;
  NnueModel model = initializedModel();
  TrainingResult result;
  result.initial_loss = datasetLoss(model, examples);
  PackedParameters packed = pack(model);
  PackedParameters first_moment;
  PackedParameters second_moment;
  std::uint64_t step = 0;

  for (int epoch = 0; epoch < kTrainingEpochs; ++epoch) {
    deterministicShuffle(examples, 0xada0'0000u + static_cast<std::uint32_t>(epoch));
    for (std::size_t begin = 0; begin < examples.size(); begin += kBatchSize) {
      const std::size_t end = std::min(examples.size(), begin + kBatchSize);
      PackedParameters gradient;
      model = unpack(packed);
      for (std::size_t index = begin; index < end; ++index) {
        const LearningExample& example = examples[index];
        const Forward pass = forward(model, example);
        const double error = sigmoid(pass.logit) - example.label;
        const std::size_t output_offset = kInputCount * kHidden + kHidden;
        for (int hidden = 0; hidden < kHidden; ++hidden) {
          gradient.values[output_offset + hidden] +=
              error * pass.hidden[hidden];
          if (pass.preactivation[hidden] <= 0.0) continue;
          const double hidden_error = error * model.output[hidden];
          gradient.values[kInputCount * kHidden + hidden] += hidden_error;
          for (const std::uint16_t active : example.active) {
            gradient.values[static_cast<std::size_t>(active) * kHidden + hidden] +=
                hidden_error;
          }
        }
        gradient.values.back() += error;
      }
      const double inverse_batch = 1.0 / static_cast<double>(end - begin);
      ++step;
      const double first_correction = 1.0 - std::pow(0.9, static_cast<double>(step));
      const double second_correction = 1.0 - std::pow(0.999, static_cast<double>(step));
      for (std::size_t parameter = 0; parameter < packed.values.size(); ++parameter) {
        double value = gradient.values[parameter] * inverse_batch;
        const bool regularized = parameter < kInputCount * kHidden ||
                                 (parameter >= kInputCount * kHidden + kHidden &&
                                  parameter < kParameterCount - 1);
        if (regularized) value += kL2 * packed.values[parameter];
        first_moment.values[parameter] =
            0.9 * first_moment.values[parameter] + 0.1 * value;
        second_moment.values[parameter] =
            0.999 * second_moment.values[parameter] + 0.001 * value * value;
        const double corrected_first =
            first_moment.values[parameter] / first_correction;
        const double corrected_second =
            second_moment.values[parameter] / second_correction;
        packed.values[parameter] -=
            kLearningRate * corrected_first / (std::sqrt(corrected_second) + 1.0e-8);
      }
    }
  }
  result.model = unpack(packed);
  result.final_loss = datasetLoss(result.model, source);
  return result;
}

struct PredictionMetrics {
  int examples = 0;
  int pairs = 0;
  double loss = 0.0;
  double auc = 0.0;
  double pair_accuracy = 0.0;
  double first_half_accuracy = 0.0;
  double second_half_accuracy = 0.0;
};

double pairCredit(double positive, double negative) {
  if (positive > negative) return 1.0;
  if (positive == negative) return 0.5;
  return 0.0;
}

PredictionMetrics predictionMetrics(const NnueModel& model,
                                    const MatchedDataset& dataset,
                                    std::uint32_t split_seed) {
  PredictionMetrics result;
  result.examples = static_cast<int>(dataset.examples.size());
  result.pairs = static_cast<int>(dataset.pairs.size());
  result.loss = datasetLoss(model, dataset.examples);
  std::vector<double> positive;
  std::vector<double> negative;
  for (const LearningExample& example : dataset.examples) {
    const double score = forward(model, example).logit;
    (example.label > 0.5 ? positive : negative).push_back(score);
  }
  double auc_credit = 0.0;
  for (const double oracle_score : positive) {
    for (const double fair_score : negative) {
      auc_credit += pairCredit(oracle_score, fair_score);
    }
  }
  result.auc = auc_credit /
               static_cast<double>(positive.size() * negative.size());

  double all_credit = 0.0;
  double first_credit = 0.0;
  double second_credit = 0.0;
  int first_count = 0;
  int second_count = 0;
  for (const MatchedPair& pair : dataset.pairs) {
    const double credit = pairCredit(forward(model, pair.oracle).logit,
                                     forward(model, pair.fair).logit);
    all_credit += credit;
    if (pair.diagnostic_seed < split_seed) {
      first_credit += credit;
      ++first_count;
    } else {
      second_credit += credit;
      ++second_count;
    }
  }
  result.pair_accuracy = all_credit / std::max(1, result.pairs);
  result.first_half_accuracy = first_credit / std::max(1, first_count);
  result.second_half_accuracy = second_credit / std::max(1, second_count);
  return result;
}

std::uint64_t modelFingerprint(const NnueModel& model) {
  const PackedParameters packed = pack(model);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const double value : packed.values) {
    std::uint64_t bits = std::bit_cast<std::uint64_t>(value);
    for (int byte = 0; byte < 8; ++byte) {
      hash ^= bits & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
      bits >>= 8;
    }
  }
  return hash;
}

namespace residual_search {

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct Context {
  Context(const NnueModel& source_model, double source_coefficient)
      : model(source_model), coefficient(source_coefficient) {}

  const NnueModel& model;
  double coefficient = 0.0;
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
};

void checkBudget(const Context& context) {
  if (context.work >= fair::kMaximumWork) throw WorkLimitReached{};
}

void cacheValue(Context& context, std::string key, double value) {
  const auto prior = context.cache.find(key);
  if (prior != context.cache.end()) {
    context.order.erase(prior->second.order);
    context.cache.erase(prior);
  }
  while (context.cache.size() >= fair::kMaximumCacheEntries) {
    const std::string& oldest = context.order.front();
    context.cache.erase(oldest);
    context.order.pop_front();
  }
  context.order.push_back(key);
  const auto order = std::prev(context.order.end());
  context.cache.emplace(std::move(key), CacheEntry{value, order});
}

double bestFutureValue(const State& state, int depth, Context& context);

struct ActionValue {
  double value = 0.0;
  double expected_score = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           Context& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, frozen::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < fair::kChanceSamples; ++sample) {
    checkBudget(context);
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, fair::kChanceSamples, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, column, random, move);
    ++context.work;
    if (!played) {
      result.value += frozen::kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    result.expected_score += score_delta;
    if (move.state.game_over) {
      result.value += score_delta + frozen::kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, fair::kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value +=
        score_delta + bestFutureValue(next, depth - 1, context);
  }
  result.value /= fair::kChanceSamples;
  result.expected_score /= fair::kChanceSamples;
  return result;
}

double evaluateLeaf(const State& state, Context& context) {
  checkBudget(context);
  ++context.work;
  const double value = frozen::fairLeaf(state) +
                       context.coefficient * boardLogit(context.model,
                                                        state.board);
  if (!std::isfinite(value)) {
    throw std::runtime_error("topology residual leaf is non-finite");
  }
  return value;
}

double bestFutureValue(const State& state, int depth, Context& context) {
  ++context.nodes;
  checkBudget(context);
  if (state.game_over) return frozen::kTerminalUtility;
  if (depth == 0) return evaluateLeaf(state, context);
  const std::string key = cfpi::detail::dynamicStateKey(state, depth);
  const auto cached = context.cache.find(key);
  if (cached != context.cache.end()) {
    ++context.cache_hits;
    const double value = cached->second.value;
    context.order.splice(context.order.end(), context.order,
                         cached->second.order);
    return value;
  }
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(best, evaluateAction(state, column, depth, context).value);
  }
  if (!std::isfinite(best)) best = frozen::kTerminalUtility;
  cacheValue(context, key, best);
  return best;
}

struct RootEvaluation {
  int action = -1;
  double value = -std::numeric_limits<double>::infinity();
  std::array<double, kBoardSize> values{};
};

RootEvaluation rootDecision(const State& state, int depth, Context& context) {
  RootEvaluation result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    const ActionValue candidate = evaluateAction(state, column, depth, context);
    result.values[column] = candidate.value;
    if (candidate.value > result.value) {
      result.value = candidate.value;
      result.action = column;
    }
  }
  return result;
}

struct Decision {
  int action = -1;
  int completed_depth = 0;
  bool complete = false;
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
  std::array<double, kBoardSize> root_values{};
};

Decision chooseAction(const State& source, const NnueModel& model,
                      double coefficient) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State state = cfpi::detail::canonicalState(source, mirrored);
  Context context{model, coefficient};
  RootEvaluation completed;
  int completed_depth = 0;
  for (int depth = 1; depth <= fair::kCandidateDepth; ++depth) {
    try {
      completed = rootDecision(state, depth, context);
      if (completed.action < 0) break;
      completed_depth = depth;
    } catch (const WorkLimitReached&) {
      break;
    }
  }
  int action = completed.action;
  if (action < 0) action = centerFirstMove(state.board);
  Decision result;
  result.action = mirrored ? kBoardSize - 1 - action : action;
  result.completed_depth = completed_depth;
  result.complete = completed_depth == fair::kCandidateDepth;
  result.work = context.work;
  result.nodes = context.nodes;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  result.root_values.fill(-std::numeric_limits<double>::infinity());
  if (completed_depth > 0) {
    for (int canonical_column = 0; canonical_column < kBoardSize;
         ++canonical_column) {
      const int source_column = mirrored
                                    ? kBoardSize - 1 - canonical_column
                                    : canonical_column;
      result.root_values[source_column] = completed.values[canonical_column];
    }
  }
  return result;
}

}  // namespace residual_search

struct CoefficientDiagnostic {
  double coefficient = 0.0;
  int states = 0;
  int switches = 0;
  double switch_rate = 0.0;
  bool all_complete = true;
  bool all_legal = true;
  std::uint64_t work = 0;
};

std::vector<State> selectDiagnosticStates(
    const std::vector<PublicDiagnosticState>& source) {
  if (source.empty()) return {};
  std::map<std::pair<int, int>, std::vector<State>> by_phase;
  for (const PublicDiagnosticState& item : source) {
    by_phase[{item.stratum.move_band, item.stratum.rise_phase}].push_back(
        item.state);
  }
  std::vector<State> result;
  std::size_t round = 0;
  while (result.size() < kDiagnosticStates) {
    bool added = false;
    for (const auto& [key, values] : by_phase) {
      static_cast<void>(key);
      if (round >= values.size()) continue;
      result.push_back(values[round]);
      added = true;
      if (result.size() == kDiagnosticStates) break;
    }
    if (!added) break;
    ++round;
  }
  return result;
}

struct PolicyDiagnostic {
  int states = 0;
  std::vector<CoefficientDiagnostic> grid;
  std::optional<double> selected_coefficient;
  bool passed = false;
};

PolicyDiagnostic diagnosePolicy(const NnueModel& model,
                                const std::vector<State>& states) {
  PolicyDiagnostic result;
  result.states = static_cast<int>(states.size());
  if (states.size() < static_cast<std::size_t>(kDiagnosticStates)) return result;
  std::vector<fair::SearchDecision> baseline;
  baseline.reserve(states.size());
  for (const State& state : states) {
    baseline.push_back(fair::chooseDepth4Action(state));
  }
  for (const double coefficient : kCoefficientGrid) {
    CoefficientDiagnostic item;
    item.coefficient = coefficient;
    item.states = static_cast<int>(states.size());
    for (std::size_t index = 0; index < states.size(); ++index) {
      const residual_search::Decision candidate =
          residual_search::chooseAction(states[index], model, coefficient);
      item.all_complete = item.all_complete && candidate.complete &&
                          candidate.completed_depth == 4;
      item.all_legal = item.all_legal &&
                       isLegal(states[index].board, candidate.action);
      item.switches += candidate.action != baseline[index].action;
      item.work += candidate.work;
    }
    item.switch_rate =
        static_cast<double>(item.switches) / std::max(1, item.states);
    result.grid.push_back(item);
  }
  double best_distance = std::numeric_limits<double>::infinity();
  for (const CoefficientDiagnostic& item : result.grid) {
    if (!item.all_complete || !item.all_legal ||
        item.switch_rate < kMinimumDiagnosticSwitchRate ||
        item.switch_rate > kMaximumDiagnosticSwitchRate) {
      continue;
    }
    const double distance = std::abs(item.switch_rate - 0.12);
    if (distance < best_distance) {
      best_distance = distance;
      result.selected_coefficient = item.coefficient;
    }
  }
  result.passed = result.selected_coefficient.has_value();
  return result;
}

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  int maximum_chain = 0;
  std::uint64_t work = 0;
  std::size_t peak_cache_entries = 0;
  double elapsed_seconds = 0.0;
};

void observeMove(const MoveResult& move, GameResult& result) {
  result.maximum_chain =
      std::max(result.maximum_chain, static_cast<int>(move.waves.size()));
  for (const Wave& wave : move.waves) {
    result.cleared += wave.cleared;
    result.revealed += wave.revealed;
  }
}

void reportGame(std::string_view phase, std::string_view policy,
                const GameResult& result) {
  const std::lock_guard<std::mutex> lock(progress_mutex);
  std::cerr << "topology-residual " << phase << '-' << policy << " seed 0x"
            << std::hex << result.seed << std::dec << ' ' << result.score
            << " (" << result.moves << " moves"
            << (result.censored ? ", capped" : "") << ")\n";
}

GameResult runBaselineGame(std::uint32_t seed, std::string_view phase) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kGameplayMaximumMoves) {
    const fair::SearchDecision decision = fair::chooseDepth4Action(state);
    if (!decision.complete || decision.completed_depth != 4 ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("gameplay fair D4 decision failed");
    }
    result.work += decision.work;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("gameplay fair D4 transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  reportGame(phase, "fair-d4", result);
  return result;
}

GameResult runResidualGame(std::uint32_t seed, const NnueModel& model,
                           double coefficient, std::string_view phase) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kGameplayMaximumMoves) {
    const residual_search::Decision decision =
        residual_search::chooseAction(state, model, coefficient);
    if (!decision.complete || decision.completed_depth != 4 ||
        !isLegal(state.board, decision.action)) {
      throw std::runtime_error("gameplay residual D4 decision failed");
    }
    result.work += decision.work;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, decision.cache_entries);
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("gameplay residual D4 transition failed");
    }
    observeMove(move, result);
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.elapsed_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  reportGame(phase, "residual-d4", result);
  return result;
}

struct GameplayCohort {
  std::vector<GameResult> baseline;
  std::vector<GameResult> candidate;
  double wall_seconds = 0.0;
};

GameplayCohort runGameplayCohort(std::uint32_t seed_start, int games,
                                 const NnueModel& model, double coefficient,
                                 std::string_view phase) {
  const auto started = Clock::now();
  GameplayCohort result;
  result.baseline.resize(games);
  result.candidate.resize(games);
  std::atomic<int> next_game{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(kParallelism, games); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next_game.fetch_add(1);
        if (game >= games) return;
        const std::uint32_t seed =
            seed_start + static_cast<std::uint32_t>(game);
        result.baseline[game] = runBaselineGame(seed, phase);
        result.candidate[game] =
            runResidualGame(seed, model, coefficient, phase);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct GameSummary {
  int games = 0;
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double mean_maximum_chain = 0.0;
  double work_per_move = 0.0;
  double moves_per_second = 0.0;
  std::size_t peak_cache_entries = 0;
};

GameSummary summarizeGames(const std::vector<GameResult>& games) {
  if (games.empty()) throw std::invalid_argument("empty gameplay cohort");
  GameSummary result;
  result.games = static_cast<int>(games.size());
  std::uint64_t moves = 0;
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  std::uint64_t work = 0;
  double seconds = 0.0;
  for (const GameResult& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.mean_maximum_chain +=
        static_cast<double>(game.maximum_chain) / games.size();
    moves += game.moves;
    cleared += game.cleared;
    revealed += game.revealed;
    work += game.work;
    seconds += game.elapsed_seconds;
    result.peak_cache_entries =
        std::max(result.peak_cache_entries, game.peak_cache_entries);
  }
  const double move_count = static_cast<double>(std::max<std::uint64_t>(1, moves));
  result.clears_per_move = cleared / move_count;
  result.reveals_per_move = revealed / move_count;
  result.work_per_move = work / move_count;
  result.moves_per_second = move_count / std::max(1.0e-9, seconds);
  return result;
}

struct Difference {
  double mean = 0.0;
  double lower_95 = 0.0;
  int wins = 0;
  int ties = 0;
  int losses = 0;
};

Difference difference(const std::vector<double>& values) {
  if (values.empty()) throw std::invalid_argument("empty paired difference");
  Difference result;
  result.mean = std::accumulate(values.begin(), values.end(), 0.0) /
                values.size();
  double squared = 0.0;
  for (const double value : values) {
    squared += (value - result.mean) * (value - result.mean);
    result.wins += value > 0.0;
    result.ties += value == 0.0;
    result.losses += value < 0.0;
  }
  const double deviation = values.size() > 1
                               ? std::sqrt(squared / (values.size() - 1))
                               : 0.0;
  result.lower_95 =
      result.mean - 1.96 * deviation / std::sqrt(values.size());
  return result;
}

struct PairedGameplay {
  Difference score;
  Difference moves;
};

PairedGameplay pairedGameplay(const GameplayCohort& cohort) {
  if (cohort.baseline.size() != cohort.candidate.size() ||
      cohort.baseline.empty()) {
    throw std::invalid_argument("invalid paired gameplay cohort");
  }
  std::vector<double> scores;
  std::vector<double> moves;
  for (std::size_t index = 0; index < cohort.baseline.size(); ++index) {
    scores.push_back(static_cast<double>(cohort.candidate[index].score -
                                         cohort.baseline[index].score));
    moves.push_back(static_cast<double>(cohort.candidate[index].moves -
                                        cohort.baseline[index].moves));
  }
  return {difference(scores), difference(moves)};
}

bool improvesBoth(const GameSummary& baseline, const GameSummary& candidate) {
  return candidate.mean_score > baseline.mean_score &&
         candidate.mean_moves > baseline.mean_moves;
}

void writePrediction(std::ostream& output, const PredictionMetrics& value) {
  output << "{\"examples\":" << value.examples << ",\"pairs\":"
         << value.pairs << ",\"loss\":" << value.loss << ",\"auc\":"
         << value.auc << ",\"matchedPairAccuracy\":"
         << value.pair_accuracy << ",\"firstHalfPairAccuracy\":"
         << value.first_half_accuracy << ",\"secondHalfPairAccuracy\":"
         << value.second_half_accuracy << '}';
}

void writeDataset(std::ostream& output, const MatchedDataset& value) {
  output << "{\"rawFair\":" << value.raw_fair << ",\"rawOracle\":"
         << value.raw_oracle << ",\"matchedStrata\":" << value.strata
         << ",\"matchedPairs\":" << value.pairs.size()
         << ",\"learningExamples\":" << value.examples.size() << '}';
}

void writePolicyDiagnostic(std::ostream& output,
                           const PolicyDiagnostic& diagnostic) {
  output << "{\"states\":" << diagnostic.states
         << ",\"minimumSwitchRate\":" << kMinimumDiagnosticSwitchRate
         << ",\"maximumSwitchRate\":" << kMaximumDiagnosticSwitchRate
         << ",\"selectedCoefficient\":";
  if (diagnostic.selected_coefficient.has_value()) {
    output << *diagnostic.selected_coefficient;
  } else {
    output << "null";
  }
  output << ",\"passed\":" << (diagnostic.passed ? "true" : "false")
         << ",\"grid\":[";
  for (std::size_t index = 0; index < diagnostic.grid.size(); ++index) {
    if (index != 0) output << ',';
    const CoefficientDiagnostic& item = diagnostic.grid[index];
    output << "{\"coefficient\":" << item.coefficient
           << ",\"states\":" << item.states << ",\"switches\":"
           << item.switches << ",\"switchRate\":" << item.switch_rate
           << ",\"allComplete\":"
           << (item.all_complete ? "true" : "false")
           << ",\"allLegal\":" << (item.all_legal ? "true" : "false")
           << ",\"work\":" << item.work << '}';
  }
  output << "]}";
}

void writeGame(std::ostream& output, const GameResult& game) {
  output << "{\"seed\":" << game.seed << ",\"score\":" << game.score
         << ",\"moves\":" << game.moves << ",\"censored\":"
         << (game.censored ? "true" : "false") << ",\"cleared\":"
         << game.cleared << ",\"revealed\":" << game.revealed
         << ",\"maximumChain\":" << game.maximum_chain
         << ",\"work\":" << game.work << ",\"peakCacheEntries\":"
         << game.peak_cache_entries << ",\"elapsedSeconds\":"
         << game.elapsed_seconds << '}';
}

void writeGameSummary(std::ostream& output, const GameSummary& value) {
  output << "{\"games\":" << value.games << ",\"meanScore\":"
         << value.mean_score << ",\"meanMoves\":" << value.mean_moves
         << ",\"censored\":" << value.censored
         << ",\"clearsPerMove\":" << value.clears_per_move
         << ",\"revealsPerMove\":" << value.reveals_per_move
         << ",\"meanMaximumChain\":" << value.mean_maximum_chain
         << ",\"workPerMove\":" << value.work_per_move
         << ",\"movesPerSecond\":" << value.moves_per_second
         << ",\"peakCacheEntries\":" << value.peak_cache_entries << '}';
}

void writeDifference(std::ostream& output, const Difference& value) {
  output << "{\"mean\":" << value.mean << ",\"lower95\":"
         << value.lower_95 << ",\"wins\":" << value.wins
         << ",\"ties\":" << value.ties << ",\"losses\":"
         << value.losses << '}';
}

void writeGameplay(std::ostream& output, std::uint32_t seed_start,
                   const GameplayCohort& cohort, const GameSummary& baseline,
                   const GameSummary& candidate,
                   const PairedGameplay& paired, bool passed) {
  output << "{\"seedStart\":" << seed_start
         << ",\"maximumMoves\":" << kGameplayMaximumMoves
         << ",\"baseline\":";
  writeGameSummary(output, baseline);
  output << ",\"candidate\":";
  writeGameSummary(output, candidate);
  output << ",\"paired\":{\"score\":";
  writeDifference(output, paired.score);
  output << ",\"moves\":";
  writeDifference(output, paired.moves);
  output << "},\"wallSeconds\":" << cohort.wall_seconds
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"pairs\":[";
  for (std::size_t index = 0; index < cohort.baseline.size(); ++index) {
    if (index != 0) output << ',';
    output << "{\"seed\":" << cohort.baseline[index].seed
           << ",\"baseline\":";
    writeGame(output, cohort.baseline[index]);
    output << ",\"candidate\":";
    writeGame(output, cohort.candidate[index]);
    output << '}';
  }
  output << "]}";
}

void writeModel(const std::string& path, const NnueModel& model,
                std::uint64_t fingerprint) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("could not write topology model");
  const PackedParameters packed = pack(model);
  output << std::setprecision(17)
         << "{\n  \"format\":\"drop7-reflection-nnue-v1\",\n"
         << "  \"input\":\"reflection-canonical-board-only\",\n"
         << "  \"cellKinds\":10,\n  \"cells\":49,\n  \"hidden\":"
         << kHidden << ",\n  \"parameterCount\":" << kParameterCount
         << ",\n  \"fingerprintFnv1a64\":\"0x" << std::hex
         << fingerprint << std::dec << "\",\n  \"parameters\":[";
  for (std::size_t index = 0; index < packed.values.size(); ++index) {
    if (index != 0) output << ',';
    output << packed.values[index];
  }
  output << "]\n}\n";
}

void writeBehaviorLabels(const std::string& path,
                         const std::vector<BehaviorLabel>& training,
                         const std::vector<BehaviorLabel>& heldout) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("could not write D4 behavior labels");
  output << std::setprecision(17)
         << "{\"format\":\"drop7-public-d4-root-labels-v1\","
            "\"independentOfResidualExperiment\":true,"
            "\"wholeSeedSplit\":true,\"trainingSeedStart\":"
         << kTrainingSeedStart << ",\"trainingGames\":" << kTrainingGames
         << ",\"trainingRecords\":" << training.size()
         << ",\"heldoutSeedStart\":" << kHeldoutSeedStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"heldoutRecords\":" << heldout.size()
         << ",\"excluded\":[\"gameSeed\",\"score\",\"level\","
            "\"moveIndex\",\"history\",\"futureTape\"]}\n";
  const auto write_split = [&](std::string_view split,
                               const std::vector<BehaviorLabel>& labels) {
    for (const BehaviorLabel& label : labels) {
      output << "{\"split\":\"" << split << "\",\"board\":\"";
      for (const std::uint8_t cell : label.board) {
        output << static_cast<char>('0' + cell);
      }
      output << "\",\"nextDisc\":" << static_cast<int>(label.next_disc)
             << ",\"movesRemaining\":" << label.moves_remaining
             << ",\"action\":" << label.action << ",\"rootQ\":[";
      for (int column = 0; column < kBoardSize; ++column) {
        if (column != 0) output << ',';
        if (std::isfinite(label.root_values[column])) {
          output << label.root_values[column];
        } else {
          output << "null";
        }
      }
      output << "]}\n";
    }
  };
  write_split("training", training);
  write_split("heldout", heldout);
}

struct Options {
  std::string output = "/tmp/drop7-oracle-topology-residual.json";
  std::string model = "/tmp/drop7-oracle-topology-residual-model.json";
  std::string labels = "/tmp/drop7-d4-public-root-labels.jsonl";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing topology residual option value");
    }
    const std::string flag = argv[index];
    if (flag == "--output") {
      result.output = argv[index + 1];
    } else if (flag == "--model") {
      result.model = argv[index + 1];
    } else if (flag == "--labels") {
      result.labels = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown topology residual option " + flag);
    }
  }
  return result;
}

bool predictionGate(const PredictionMetrics& heldout) {
  return heldout.examples >= 200 && heldout.pairs >= 100 &&
         heldout.auc >= kMinimumHeldoutAuc &&
         heldout.pair_accuracy >= kMinimumHeldoutPairAccuracy &&
         heldout.first_half_accuracy >= kMinimumHalfPairAccuracy &&
         heldout.second_half_accuracy >= kMinimumHalfPairAccuracy;
}

bool sameDecision(const residual_search::Decision& left,
                  const residual_search::Decision& right) {
  if (left.action != right.action ||
      left.completed_depth != right.completed_depth ||
      left.complete != right.complete || left.work != right.work ||
      left.nodes != right.nodes || left.cache_hits != right.cache_hits ||
      left.cache_entries != right.cache_entries) {
    return false;
  }
  for (int column = 0; column < kBoardSize; ++column) {
    if (left.root_values[column] != right.root_values[column]) return false;
  }
  return true;
}

bool selfTest(std::ostream& output) {
  const State fixture = frozen::fixtureState(frozen::kTypeScriptFixtures[1]);
  const Board reflected_board = cfpi::detail::mirrorBoard(fixture.board);
  const LearningExample original =
      makeLearningExample(fixture.board, Label::kFair);
  const LearningExample reflected =
      makeLearningExample(reflected_board, Label::kFair);
  const bool reflection_input = original.active == reflected.active;

  std::vector<LearningExample> training;
  training.reserve(64);
  for (int index = 0; index < 64; ++index) {
    Board board = fixture.board;
    const int cell = index % kCellCount;
    if (board[cell] == kEmpty) board[cell] = static_cast<std::uint8_t>(index % 7 + 1);
    training.push_back(makeLearningExample(
        board, index % 2 == 0 ? Label::kFair : Label::kOracle));
  }
  const TrainingResult first_training = trainModel(training);
  const TrainingResult repeat_training = trainModel(training);
  const bool training_deterministic =
      modelFingerprint(first_training.model) ==
      modelFingerprint(repeat_training.model);
  const bool finite_training =
      std::isfinite(first_training.initial_loss) &&
      std::isfinite(first_training.final_loss) &&
      first_training.final_loss < first_training.initial_loss;
  const bool reflection_model =
      boardLogit(first_training.model, fixture.board) ==
      boardLogit(first_training.model, reflected_board);

  const fair::SearchDecision baseline = fair::chooseDepth4Action(fixture);
  const residual_search::Decision zero = residual_search::chooseAction(
      fixture, first_training.model, 0.0);
  bool zero_parity = baseline.action == zero.action && baseline.complete &&
                     zero.complete && baseline.completed_depth == 4 &&
                     zero.completed_depth == 4;
  for (int column = 0; column < kBoardSize; ++column) {
    zero_parity = zero_parity &&
                  baseline.root_values[column] == zero.root_values[column];
  }

  const residual_search::Decision candidate = residual_search::chooseAction(
      fixture, first_training.model, 1'000.0);
  const residual_search::Decision repeat = residual_search::chooseAction(
      fixture, first_training.model, 1'000.0);
  State reflected_state = fixture;
  reflected_state.board = reflected_board;
  const residual_search::Decision mirror = residual_search::chooseAction(
      reflected_state, first_training.model, 1'000.0);
  State metadata = fixture;
  metadata.score = 987'654'321;
  metadata.level = 777;
  metadata.moves_played = 999;
  const residual_search::Decision metadata_decision =
      residual_search::chooseAction(metadata, first_training.model, 1'000.0);
  const bool deterministic = sameDecision(candidate, repeat);
  const bool legal = candidate.complete &&
                     isLegal(fixture.board, candidate.action);
  const bool reflection_policy =
      mirror.complete && mirror.action == kBoardSize - 1 - candidate.action;
  const bool metadata_blind = sameDecision(candidate, metadata_decision);
  const bool ranges = kTrainingSeedStart >= 0x3d00'0000u &&
                      kHeldoutSeedStart < 0x3e00'0000u &&
                      kScreenSeedStart >= 0x3e00'0000u &&
                      kScreenSeedStart != 0x3e9d'0000u;
  const bool resource_bounds = kParameterCount < 4'000 &&
                               fair::kWorstCaseD4Work < fair::kMaximumWork &&
                               fair::kWorstCaseD4CacheEntries <
                                   fair::kMaximumCacheEntries;
  const bool passed = kLevelBonus == 7'000 && reflection_input &&
                      training_deterministic && finite_training &&
                      reflection_model && zero_parity && deterministic &&
                      legal && reflection_policy && metadata_blind && ranges &&
                      resource_bounds;
  output << "ORACLE_TOPOLOGY_RESIDUAL_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"reflectionInput\":"
         << (reflection_input ? "true" : "false")
         << ",\"trainingDeterministic\":"
         << (training_deterministic ? "true" : "false")
         << ",\"finiteTraining\":" << (finite_training ? "true" : "false")
         << ",\"reflectionModel\":"
         << (reflection_model ? "true" : "false")
         << ",\"zeroCoefficientParity\":"
         << (zero_parity ? "true" : "false")
         << ",\"searchDeterministic\":"
         << (deterministic ? "true" : "false")
         << ",\"legal\":" << (legal ? "true" : "false")
         << ",\"reflectionPolicy\":"
         << (reflection_policy ? "true" : "false")
         << ",\"metadataBlind\":"
         << (metadata_blind ? "true" : "false")
         << ",\"seedRangesSealed\":" << (ranges ? "true" : "false")
         << ",\"resourceBounds\":"
         << (resource_bounds ? "true" : "false")
         << ",\"parameterCount\":" << kParameterCount
         << ",\"maximumWork\":" << fair::kMaximumWork
         << ",\"maximumCacheEntries\":" << fair::kMaximumCacheEntries
         << "}\n";
  return passed;
}

void writeArtifact(
    const Options& options, const CollectedSplit& training_collection,
    const CollectedSplit& heldout_collection,
    const MatchedDataset& training_data,
    const MatchedDataset& heldout_data,
    const TrainingResult& training,
    const PredictionMetrics& training_prediction,
    const PredictionMetrics& heldout_prediction, bool prediction_passed,
    std::uint64_t fingerprint, const PolicyDiagnostic& policy,
    const GameplayCohort* screen, const GameSummary* screen_baseline,
    const GameSummary* screen_candidate, const PairedGameplay* screen_paired,
    bool screen_passed, const GameplayCohort* confirmation,
    const GameSummary* confirmation_baseline,
    const GameSummary* confirmation_candidate,
    const PairedGameplay* confirmation_paired, bool confirmation_passed,
    double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write topology artifact");
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"oracle-observable-topology-residual\",\n"
         << "  \"preregistered\":true,\n"
         << "  \"warning\":\"oracle sees the future only while producing fitting labels; model input is board-only\",\n"
         << "  \"dataProtocol\":{\"trainingSeedStart\":"
         << kTrainingSeedStart << ",\"trainingGames\":" << kTrainingGames
         << ",\"heldoutSeedStart\":" << kHeldoutSeedStart
         << ",\"heldoutGames\":" << kHeldoutGames
         << ",\"maximumMoves\":" << kTrainingMaximumMoves
         << ",\"oracleDepth\":" << kOracleDepth
         << ",\"oracleBeam\":" << kOracleBeam
         << ",\"wholeSeedSplit\":true,\"matchedOn\":[\"risePhase\",\"exactOccupancy\",\"exactMaximumHeight\",\"twentyMoveBand\"],"
            "\"optimizerInputs\":[\"reflectionCanonicalBoardCells\"],"
            "\"excludedFromOptimizer\":[\"gameSeed\",\"futureTape\",\"score\",\"level\",\"moveIndex\",\"history\",\"nextDisc\",\"risePhase\"],"
            "\"independentBehaviorLabels\":{\"path\":\""
         << options.labels << "\",\"trainingRecords\":"
         << training_collection.behavior_labels.size()
         << ",\"heldoutRecords\":"
         << heldout_collection.behavior_labels.size()
         << ",\"usedByResidualExperiment\":false}},\n"
         << "  \"collection\":{\"trainingWallSeconds\":"
         << training_collection.wall_seconds
         << ",\"heldoutWallSeconds\":" << heldout_collection.wall_seconds
         << ",\"training\":";
  writeDataset(output, training_data);
  output << ",\"heldout\":";
  writeDataset(output, heldout_data);
  output << "},\n  \"model\":{\"kind\":\"sparse-reflection-invariant-nnue\","
            "\"canonicalization\":\"lexicographically-smaller-horizontal-reflection\","
            "\"inputCount\":"
         << kInputCount << ",\"activeInputsPerBoard\":" << kCellCount
         << ",\"hiddenRelu\":" << kHidden
         << ",\"parameterCount\":" << kParameterCount
         << ",\"epochs\":" << kTrainingEpochs << ",\"batchSize\":"
         << kBatchSize << ",\"learningRate\":" << kLearningRate
         << ",\"l2\":" << kL2 << ",\"initialLoss\":"
         << training.initial_loss << ",\"finalLoss\":"
         << training.final_loss << ",\"fingerprintFnv1a64\":\"0x"
         << std::hex << fingerprint << std::dec << "\",\"path\":\""
         << options.model << "\"},\n  \"prediction\":{\"gate\":{"
            "\"minimumHeldoutAuc\":"
         << kMinimumHeldoutAuc << ",\"minimumHeldoutPairAccuracy\":"
         << kMinimumHeldoutPairAccuracy
         << ",\"minimumHalfPairAccuracy\":"
         << kMinimumHalfPairAccuracy << ",\"minimumHeldoutExamples\":200,"
            "\"minimumHeldoutPairs\":100},\"training\":";
  writePrediction(output, training_prediction);
  output << ",\"heldout\":";
  writePrediction(output, heldout_prediction);
  output << ",\"passed\":" << (prediction_passed ? "true" : "false")
         << "},\n  \"policyDiagnostic\":";
  writePolicyDiagnostic(output, policy);
  output << ",\n  \"search\":{\"baseline\":\"fair-only-full-width-depth4\","
            "\"candidate\":\"same-search-plus-NNUE-at-leaves-only\","
            "\"chanceSamples\":"
         << fair::kChanceSamples << ",\"maximumWork\":"
         << fair::kMaximumWork << ",\"maximumCacheEntries\":"
         << fair::kMaximumCacheEntries << ",\"maximumMoves\":"
         << kGameplayMaximumMoves << "},\n  \"screen\":";
  if (screen == nullptr) {
    output << "null";
  } else {
    writeGameplay(output, kScreenSeedStart, *screen, *screen_baseline,
                  *screen_candidate, *screen_paired, screen_passed);
  }
  output << ",\n  \"confirmation\":";
  if (confirmation == nullptr) {
    output << "null";
  } else {
    writeGameplay(output, kConfirmationSeedStart, *confirmation,
                  *confirmation_baseline, *confirmation_candidate,
                  *confirmation_paired, confirmation_passed);
  }
  output << ",\n  \"screenRan\":" << (screen != nullptr ? "true" : "false")
         << ",\n  \"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\n  \"confirmationRan\":"
         << (confirmation != nullptr ? "true" : "false")
         << ",\n  \"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\n  \"qualified\":"
         << (prediction_passed && policy.passed && screen_passed &&
                     confirmation_passed
                 ? "true"
                 : "false")
         << ",\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
}

int run(const Options& options, std::ostream& output) {
  const auto started = Clock::now();
  const CollectedSplit training_collection =
      collectSplit(kTrainingSeedStart, kTrainingGames, "training");
  const CollectedSplit heldout_collection =
      collectSplit(kHeldoutSeedStart, kHeldoutGames, "heldout");
  writeBehaviorLabels(options.labels, training_collection.behavior_labels,
                      heldout_collection.behavior_labels);
  const MatchedDataset training_data =
      matchRecords(training_collection.fair, training_collection.oracle);
  const MatchedDataset heldout_data =
      matchRecords(heldout_collection.fair, heldout_collection.oracle);
  if (training_data.examples.size() < 64 || heldout_data.examples.empty()) {
    throw std::runtime_error("insufficient matched topology data");
  }
  const TrainingResult training = trainModel(training_data.examples);
  const PredictionMetrics training_prediction = predictionMetrics(
      training.model, training_data,
      kTrainingSeedStart + static_cast<std::uint32_t>(kTrainingGames / 2));
  const PredictionMetrics heldout_prediction = predictionMetrics(
      training.model, heldout_data,
      kHeldoutSeedStart + static_cast<std::uint32_t>(kHeldoutGames / 2));
  const bool prediction_passed = predictionGate(heldout_prediction);
  const std::uint64_t fingerprint = modelFingerprint(training.model);
  writeModel(options.model, training.model, fingerprint);

  PolicyDiagnostic policy;
  if (prediction_passed) {
    policy = diagnosePolicy(
        training.model,
        selectDiagnosticStates(heldout_collection.fair_diagnostics));
  }

  GameplayCohort screen;
  GameSummary screen_baseline;
  GameSummary screen_candidate;
  PairedGameplay screen_paired;
  bool screen_passed = false;
  if (prediction_passed && policy.passed) {
    screen = runGameplayCohort(kScreenSeedStart, kScreenGames,
                               training.model,
                               *policy.selected_coefficient, "screen");
    screen_baseline = summarizeGames(screen.baseline);
    screen_candidate = summarizeGames(screen.candidate);
    screen_paired = pairedGameplay(screen);
    screen_passed = improvesBoth(screen_baseline, screen_candidate);
  }

  GameplayCohort confirmation;
  GameSummary confirmation_baseline;
  GameSummary confirmation_candidate;
  PairedGameplay confirmation_paired;
  bool confirmation_passed = false;
  if (screen_passed) {
    confirmation = runGameplayCohort(
        kConfirmationSeedStart, kConfirmationGames, training.model,
        *policy.selected_coefficient, "confirmation");
    confirmation_baseline = summarizeGames(confirmation.baseline);
    confirmation_candidate = summarizeGames(confirmation.candidate);
    confirmation_paired = pairedGameplay(confirmation);
    confirmation_passed =
        improvesBoth(confirmation_baseline, confirmation_candidate);
  }
  const double wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  writeArtifact(
      options, training_collection, heldout_collection, training_data,
      heldout_data, training, training_prediction, heldout_prediction,
      prediction_passed, fingerprint, policy,
      prediction_passed && policy.passed ? &screen : nullptr,
      prediction_passed && policy.passed ? &screen_baseline : nullptr,
      prediction_passed && policy.passed ? &screen_candidate : nullptr,
      prediction_passed && policy.passed ? &screen_paired : nullptr,
      screen_passed, screen_passed ? &confirmation : nullptr,
      screen_passed ? &confirmation_baseline : nullptr,
      screen_passed ? &confirmation_candidate : nullptr,
      screen_passed ? &confirmation_paired : nullptr, confirmation_passed,
      wall_seconds);

  output << std::fixed << std::setprecision(4)
         << "ORACLE_TOPOLOGY_RESIDUAL_RESULT {\"trainingPairs\":"
         << training_data.pairs.size() << ",\"heldoutPairs\":"
         << heldout_data.pairs.size() << ",\"heldoutAuc\":"
         << heldout_prediction.auc << ",\"heldoutPairAccuracy\":"
         << heldout_prediction.pair_accuracy
         << ",\"firstHalfPairAccuracy\":"
         << heldout_prediction.first_half_accuracy
         << ",\"secondHalfPairAccuracy\":"
         << heldout_prediction.second_half_accuracy
         << ",\"predictionPassed\":"
         << (prediction_passed ? "true" : "false")
         << ",\"policyPassed\":" << (policy.passed ? "true" : "false")
         << ",\"selectedCoefficient\":";
  if (policy.selected_coefficient.has_value()) {
    output << *policy.selected_coefficient;
  } else {
    output << "null";
  }
  output << ",\"screenRan\":"
         << (prediction_passed && policy.passed ? "true" : "false")
         << ",\"screenPassed\":" << (screen_passed ? "true" : "false")
         << ",\"confirmationRan\":"
         << (screen_passed ? "true" : "false")
         << ",\"confirmationPassed\":"
         << (confirmation_passed ? "true" : "false")
         << ",\"fingerprint\":\"0x" << std::hex << fingerprint << std::dec
         << "\",\"wallSeconds\":" << wall_seconds
         << ",\"peakRssBytes\":" << peakRssBytes() << ",\"artifact\":\""
         << options.output << "\",\"model\":\"" << options.model
         << "\",\"behaviorLabels\":\"" << options.labels << "\"}\n";
  return 0;
}

}  // namespace drop7::oracle_topology_residual

#ifndef DROP7_ORACLE_TOPOLOGY_RESIDUAL_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::oracle_topology_residual::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      return drop7::oracle_topology_residual::run(
          drop7::oracle_topology_residual::parseOptions(argc, argv, 2),
          std::cout);
    }
    std::cerr << "usage: drop7_oracle_topology_residual --self-test | --run "
                 "[--output PATH] [--model PATH] [--labels PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_oracle_topology_residual: " << error.what() << '\n';
    return 1;
  }
}
#endif
