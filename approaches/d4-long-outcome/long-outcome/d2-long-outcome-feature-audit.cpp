#define DROP7_D2_LONG_OUTCOME_RANKER_LIBRARY
#include "d2-long-outcome-ranker.cpp"
#undef DROP7_D2_LONG_OUTCOME_RANKER_LIBRARY

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
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Audits candidate architectures on 432 persisted, development-only public
// roots.  It never reads a gameplay seed or a new root family. Exact clear and survival
// counters are recovered by an instrumentation replay of the identical LONG
// tapes and asserted against every persisted scenario return before use.
namespace drop7::d2_long_outcome_feature_audit {

namespace prior = drop7::d2_long_outcome_ranker;
namespace base = drop7::scaled_d4_distill;
using Clock = std::chrono::steady_clock;

constexpr int kFolds = 6;
constexpr int kHeads = 5;
constexpr int kHidden = 12;
constexpr int kLadderInputs = 3;
constexpr int kInputs = base::kFeatureCount + kLadderInputs;
constexpr std::array<int, 3> kEpochCandidates{{40, 80, 120}};
constexpr double kLearningRate = 0.003;
constexpr double kWeightDecay = 0.0002;
constexpr std::array<double, kHeads> kHeadLossWeights{{1.0, 0.25, 0.10,
                                                       0.20, 0.10}};
constexpr std::uint64_t kMaximumCheckpointBytes = 256u * 1024u;
constexpr std::uint64_t kMaximumRssBytes = 256u * 1024u * 1024u;
constexpr double kMaximumReplaySeconds = 20.0 * 60.0;
constexpr double kGateTop1Improvement = 0.02;
constexpr double kGatePairwiseImprovement = 0.01;
constexpr double kGateRegretRatio = 0.95;

static_assert(kFolds == 6 && base::kTrainingGames == 24);
static_assert(base::kHeldoutGames == 12);
static_assert(prior::kHorizon == 25 && prior::kScenarios == 7);
static_assert(kInputs == 1'650 && kHidden == 12 && kHeads == 5);
static_assert((kInputs * kHidden + kHidden + kHidden * kHeads + kHeads +
               kInputs) * sizeof(float) < kMaximumCheckpointBytes);

struct Options {
  std::string labels = "/tmp/drop7-d2-long-outcome-labels.jsonl";
  std::string d4_source = "/tmp/drop7-scaled-d4-distill-labels.jsonl";
  std::string output = "/tmp/drop7-d2-long-outcome-feature-audit.json";
  std::string derived =
      "/tmp/drop7-d2-long-outcome-feature-derived.jsonl";
  std::string checkpoint =
      "/tmp/drop7-d2-long-outcome-multihead-nnue.bin";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string flag = argv[index];
    if (flag == "--labels") result.labels = argv[index + 1];
    else if (flag == "--d4-source") result.d4_source = argv[index + 1];
    else if (flag == "--output") result.output = argv[index + 1];
    else if (flag == "--derived") result.derived = argv[index + 1];
    else if (flag == "--checkpoint") result.checkpoint = argv[index + 1];
    else throw std::invalid_argument("unknown option " + flag);
  }
  return result;
}

struct StoredRoot {
  base::RootLabel label{};
  base::RootLabel d4{};
  std::array<std::array<double, prior::kScenarios>, kBoardSize> returns{};
  std::string split;
};

void skipSeparators(std::string_view line, std::size_t& cursor) {
  while (cursor < line.size() &&
         (line[cursor] == ' ' || line[cursor] == ',')) {
    ++cursor;
  }
}

double parseDouble(std::string_view line, std::size_t& cursor) {
  const std::string owned(line);
  char* end = nullptr;
  const char* begin = owned.c_str() + cursor;
  const double value = std::strtod(begin, &end);
  if (end == begin || !std::isfinite(value)) {
    throw std::runtime_error("invalid stored outcome number");
  }
  cursor = static_cast<std::size_t>(end - owned.c_str());
  return value;
}

StoredRoot parseStoredRoot(std::string_view line) {
  StoredRoot result;
  if (line.find("\"split\":\"fitting\"") != std::string_view::npos) {
    result.split = "fitting";
  } else if (line.find("\"split\":\"heldout\"") !=
             std::string_view::npos) {
    result.split = "heldout";
  } else {
    throw std::runtime_error("stored root has no known split");
  }
  constexpr std::string_view kBoardMarker = "\"board\":\"";
  const std::size_t board_at = line.find(kBoardMarker);
  if (board_at == std::string_view::npos ||
      board_at + kBoardMarker.size() + kCellCount > line.size()) {
    throw std::runtime_error("stored root board is invalid");
  }
  for (int cell = 0; cell < kCellCount; ++cell) {
    const char value = line[board_at + kBoardMarker.size() + cell];
    if (value < '0' || value > '9') {
      throw std::runtime_error("stored root cell is invalid");
    }
    result.label.board[cell] = static_cast<std::uint8_t>(value - '0');
  }
  result.label.next_disc = static_cast<std::uint8_t>(
      base::integerAfter(line, "\"nextDisc\":"));
  result.label.moves_remaining =
      base::integerAfter(line, "\"movesRemaining\":");
  result.label.labeled_action =
      base::integerAfter(line, "\"optimalAction\":");
  result.label.game = base::integerAfter(line, "\"game\":");
  result.label.move_in_game =
      base::integerAfter(line, "\"moveInSourceGame\":");

  constexpr std::string_view kQMarker = "\"rootQ\":[";
  std::size_t cursor = line.find(kQMarker);
  if (cursor == std::string_view::npos) {
    throw std::runtime_error("stored root-Q is missing");
  }
  cursor += kQMarker.size();
  result.label.q.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    skipSeparators(line, cursor);
    if (line.substr(cursor, 4) == "null") {
      cursor += 4;
    } else {
      result.label.q[action] = parseDouble(line, cursor);
      result.label.legal[action] = true;
    }
  }

  constexpr std::string_view kReturnsMarker = "\"scenarioReturns\":[";
  cursor = line.find(kReturnsMarker);
  if (cursor == std::string_view::npos) {
    throw std::runtime_error("stored scenario returns are missing");
  }
  cursor += kReturnsMarker.size();
  for (int action = 0; action < kBoardSize; ++action) {
    skipSeparators(line, cursor);
    if (!result.label.legal[action]) {
      if (line.substr(cursor, 4) != "null") {
        throw std::runtime_error("illegal action has stored returns");
      }
      cursor += 4;
      continue;
    }
    if (cursor >= line.size() || line[cursor++] != '[') {
      throw std::runtime_error("stored scenario vector is invalid");
    }
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      skipSeparators(line, cursor);
      result.returns[action][scenario] = parseDouble(line, cursor);
    }
    skipSeparators(line, cursor);
    if (cursor >= line.size() || line[cursor++] != ']') {
      throw std::runtime_error("stored scenario vector is unterminated");
    }
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (result.label.legal[action] !=
        isLegal(result.label.board, action)) {
      throw std::runtime_error("stored legal mask does not match board");
    }
  }
  return result;
}

struct StoredCorpus {
  std::vector<StoredRoot> fitting;
  std::vector<StoredRoot> heldout;
};

StoredCorpus loadCorpus(const std::string& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("could not open preserved labels");
  std::string line;
  if (!std::getline(input, line) ||
      line.find("drop7-public-d2-closed-loop-outcomes-v1") ==
          std::string::npos ||
      line.find(
          "e97f0a00dad76ce0e47bd60d5824e4e921e57b2cb47990b28b5bd4a562dd56bf") ==
          std::string::npos) {
    throw std::runtime_error("preserved-label metadata mismatch");
  }
  StoredCorpus result;
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    StoredRoot root = parseStoredRoot(line);
    if (root.split == "fitting") result.fitting.push_back(std::move(root));
    else result.heldout.push_back(std::move(root));
  }
  if (result.fitting.size() != prior::kTrainingRoots ||
      result.heldout.size() != prior::kHeldoutRoots) {
    throw std::runtime_error("preserved-label root counts changed");
  }
  return result;
}

void joinD4Range(std::vector<StoredRoot>& targets,
                 const std::vector<base::RootLabel>& source) {
  for (StoredRoot& target : targets) {
    const auto found = std::find_if(
        source.begin(), source.end(), [&](const base::RootLabel& candidate) {
          return candidate.game == target.label.game &&
                 candidate.move_in_game == target.label.move_in_game;
        });
    if (found == source.end()) {
      throw std::runtime_error("could not join original D4 root record");
    }
    bool was_mirrored = false;
    target.d4 = prior::canonicalLabel(*found, was_mirrored);
    if (target.d4.board != target.label.board ||
        target.d4.next_disc != target.label.next_disc ||
        target.d4.moves_remaining != target.label.moves_remaining ||
        target.d4.legal != target.label.legal) {
      throw std::runtime_error("joined D4 public root mismatch");
    }
  }
}

void joinD4(StoredCorpus& corpus, const std::string& source_path) {
  const std::vector<base::RootLabel> fitting =
      base::loadSplit(source_path, "training");
  const std::vector<base::RootLabel> heldout =
      base::loadSplit(source_path, "heldout");
  if (fitting.size() != 1'885 || heldout.size() != 926) {
    throw std::runtime_error("original frozen D4 corpus count changed");
  }
  joinD4Range(corpus.fitting, fitting);
  joinD4Range(corpus.heldout, heldout);
}

// Kept byte-for-byte equivalent in semantics to
// d2_vertical_ladder_probe::verticalLadderFeatures.  Covered cells and inert
// hypothetical additions never pop; horizontal help and reveals are ignored.
struct LadderFeatures {
  double activation = 0.0;
  double chain_clears = 0.0;
  double extra_waves = 0.0;
  double energy = 0.0;
  int best_waves = 0;
};

double readiness(int additions) {
  return additions >= 1 ? std::ldexp(1.0, 1 - additions) : 0.0;
}

LadderFeatures verticalLadderFeatures(const Board& board) {
  LadderFeatures result;
  for (int column = 0; column < kBoardSize; ++column) {
    std::vector<std::uint8_t> stored;
    stored.reserve(kBoardSize);
    for (int row = kBoardSize - 1; row >= 0; --row) {
      const std::uint8_t cell = board[indexOf(row, column)];
      if (cell != kEmpty) stored.push_back(cell);
    }
    const int height = static_cast<int>(stored.size());
    LadderFeatures best;
    for (int additions = 1; additions <= kBoardSize - height; ++additions) {
      std::vector<std::uint8_t> live = stored;
      live.insert(live.end(), additions, kSolid);
      int waves = 0;
      int clears = 0;
      double energy = 0.0;
      for (;;) {
        const int vertical_length = static_cast<int>(live.size());
        int popping = 0;
        for (const std::uint8_t cell : live) {
          popping += isNumbered(cell) && cell == vertical_length;
        }
        if (popping == 0) break;
        ++waves;
        clears += popping;
        std::erase_if(live, [vertical_length](std::uint8_t cell) {
          return isNumbered(cell) && cell == vertical_length;
        });
        if (waves >= 2) {
          energy += popping * static_cast<double>(waves * waves);
        }
      }
      const double discount = readiness(additions);
      LadderFeatures candidate;
      candidate.activation = waves > 0 ? discount : 0.0;
      candidate.chain_clears = discount * std::max(0, clears - 1);
      candidate.extra_waves = discount * std::max(0, waves - 1);
      candidate.energy = discount * energy;
      candidate.best_waves = waves;
      if (candidate.energy > best.energy ||
          (candidate.energy == best.energy &&
           candidate.chain_clears > best.chain_clears)) {
        best = candidate;
      }
    }
    result.activation += best.activation;
    result.chain_clears += best.chain_clears;
    result.extra_waves += best.extra_waves;
    result.energy += best.energy;
    result.best_waves = std::max(result.best_waves, best.best_waves);
  }
  return result;
}

struct ActionAux {
  double pre_ladder = 0.0;
  std::array<double, prior::kScenarios> post_ladder{};
  std::array<bool, prior::kScenarios> scenario_survived{};
  std::array<int, prior::kScenarios> scenario_clears{};
  double expected_post_ladder = 0.0;
  double ladder_delta = 0.0;
  double survival = 0.0;
  double raw_mean_clears = 0.0;
  double mean_clears = 0.0;
  double downside = 0.0;
  double variance = 0.0;
};

struct AuditRoot {
  StoredRoot stored{};
  base::PreparedRoot prepared{};
  std::array<ActionAux, kBoardSize> actions{};
};

AuditRoot replayAndDerive(const StoredRoot& stored,
                          double& maximum_return_error) {
  AuditRoot result;
  result.stored = stored;
  const prior::OutcomeLabel replay = prior::evaluateRoot(stored.label);
  if (replay.label.board != stored.label.board ||
      replay.label.legal != stored.label.legal) {
    throw std::runtime_error("instrumentation replay changed public root");
  }
  result.prepared = base::prepare(stored.label);
  const prior::ObservableState root =
      prior::observable(base::publicState(stored.label));
  const std::uint32_t root_seed = prior::seed32(
      prior::publicHash(root) ^ static_cast<std::uint64_t>(prior::kTapeSeedDomain));
  const double pre = verticalLadderFeatures(root.board).energy;
  double global_minimum = std::numeric_limits<double>::infinity();
  double global_maximum = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!stored.label.legal[action]) continue;
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      const double value = stored.returns[action][scenario];
      global_minimum = std::min(global_minimum, value);
      global_maximum = std::max(global_maximum, value);
      maximum_return_error = std::max(
          maximum_return_error,
          std::abs(value - replay.actions[action].scenarios[scenario].value));
    }
  }
  const double return_range = std::max(1.0e-9, global_maximum - global_minimum);
  double maximum_clears = 0.0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!stored.label.legal[action]) continue;
    ActionAux& aux = result.actions[action];
    aux.pre_ladder = pre;
    double minimum_return = std::numeric_limits<double>::infinity();
    double mean_return = 0.0;
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      MoveResult first_step;
      if (!prior::playSyntheticMove(root, action, root_seed, scenario, 0,
                                    first_step)) {
        throw std::runtime_error("stored legal first step failed");
      }
      aux.post_ladder[scenario] =
          verticalLadderFeatures(first_step.state.board).energy;
      aux.expected_post_ladder +=
          aux.post_ladder[scenario] / prior::kScenarios;
      const prior::ScenarioOutcome& outcome =
          replay.actions[action].scenarios[scenario];
      aux.scenario_survived[scenario] = outcome.survived_horizon;
      aux.scenario_clears[scenario] = outcome.clears;
      aux.survival += outcome.survived_horizon ? 1.0 / prior::kScenarios : 0.0;
      aux.raw_mean_clears +=
          static_cast<double>(outcome.clears) / prior::kScenarios;
      const double value = stored.returns[action][scenario];
      mean_return += value / prior::kScenarios;
      minimum_return = std::min(minimum_return, value);
    }
    maximum_return_error = std::max(
        maximum_return_error, std::abs(mean_return - stored.label.q[action]));
    aux.ladder_delta = aux.expected_post_ladder - pre;
    aux.downside = (minimum_return - global_minimum) / return_range;
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      const double centered = stored.returns[action][scenario] - mean_return;
      aux.variance += centered * centered / prior::kScenarios;
    }
    aux.variance /= return_range * return_range;
    maximum_clears = std::max(maximum_clears, aux.raw_mean_clears);
  }
  const double clear_scale = std::max(1.0, maximum_clears);
  for (ActionAux& aux : result.actions) {
    aux.mean_clears = aux.raw_mean_clears / clear_scale;
  }
  return result;
}

std::vector<AuditRoot> deriveRange(const std::vector<StoredRoot>& stored,
                                   std::string_view split,
                                   double& maximum_return_error) {
  std::vector<AuditRoot> result(stored.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{0};
  std::mutex error_mutex;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < prior::kParallelism; ++worker) {
    workers.push_back(std::async(std::launch::async, [&]() {
      while (true) {
        const std::size_t index = next.fetch_add(1);
        if (index >= stored.size()) return;
        double local_error = 0.0;
        result[index] = replayAndDerive(stored[index], local_error);
        {
          std::lock_guard<std::mutex> lock(error_mutex);
          maximum_return_error =
              std::max(maximum_return_error, local_error);
        }
        const std::size_t count = completed.fetch_add(1) + 1;
        if (count % prior::kRootsPerGame == 0 || count == stored.size()) {
          std::lock_guard<std::mutex> lock(prior::progress_mutex);
          std::cerr << "feature-audit " << split << " replay " << count << '/'
                    << stored.size() << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  return result;
}

struct PairMoments {
  std::uint64_t pairs = 0;
  double sum_x = 0.0;
  double sum_y = 0.0;
  double sum_xx = 0.0;
  double sum_yy = 0.0;
  double sum_xy = 0.0;
  double sign_credit = 0.0;
};

void observePairs(const AuditRoot& root,
                  const std::array<double, kBoardSize>& prediction,
                  PairMoments& result) {
  for (int first = 0; first < kBoardSize; ++first) {
    if (!root.stored.label.legal[first]) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!root.stored.label.legal[second]) continue;
      const double x = prediction[first] - prediction[second];
      const double y =
          root.prepared.target[first] - root.prepared.target[second];
      result.sum_x += x;
      result.sum_y += y;
      result.sum_xx += x * x;
      result.sum_yy += y * y;
      result.sum_xy += x * y;
      if (std::abs(x) <= base::kTieTolerance ||
          std::abs(y) <= base::kTieTolerance) {
        result.sign_credit += 0.5;
      } else {
        result.sign_credit += ((x > 0.0) == (y > 0.0)) ? 1.0 : 0.0;
      }
      ++result.pairs;
    }
  }
}

double pairCorrelation(const PairMoments& value) {
  if (value.pairs < 2) return 0.0;
  const double count = static_cast<double>(value.pairs);
  const double covariance = value.sum_xy - value.sum_x * value.sum_y / count;
  const double x_variance =
      value.sum_xx - value.sum_x * value.sum_x / count;
  const double y_variance =
      value.sum_yy - value.sum_y * value.sum_y / count;
  const double denominator =
      std::sqrt(std::max(0.0, x_variance) * std::max(0.0, y_variance));
  return denominator > 0.0 ? covariance / denominator : 0.0;
}

struct Metrics {
  base::Ranking ranking{};
  PairMoments pairs{};
};

void addMetrics(Metrics& target, const Metrics& source) {
  target.ranking.roots += source.ranking.roots;
  target.ranking.top1 += source.ranking.top1;
  target.ranking.top2 += source.ranking.top2;
  target.ranking.pairs += source.ranking.pairs;
  target.ranking.pairwise_credit += source.ranking.pairwise_credit;
  target.ranking.normalized_regret += source.ranking.normalized_regret;
  target.pairs.pairs += source.pairs.pairs;
  target.pairs.sum_x += source.pairs.sum_x;
  target.pairs.sum_y += source.pairs.sum_y;
  target.pairs.sum_xx += source.pairs.sum_xx;
  target.pairs.sum_yy += source.pairs.sum_yy;
  target.pairs.sum_xy += source.pairs.sum_xy;
  target.pairs.sign_credit += source.pairs.sign_credit;
}

template <typename ScoreFunction, typename IncludeFunction>
Metrics evaluate(const std::vector<AuditRoot>& roots, ScoreFunction score,
                 IncludeFunction include) {
  Metrics result;
  for (const AuditRoot& root : roots) {
    if (!include(root)) continue;
    const auto prediction = score(root);
    base::observe(root.stored.label, prediction, result.ranking);
    observePairs(root, prediction, result.pairs);
  }
  if (result.ranking.roots == 0 || result.pairs.pairs == 0) {
    throw std::runtime_error("empty feature-audit metric range");
  }
  return result;
}

std::array<double, kBoardSize> d2Scores(const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.stored.label.legal[action]) result[action] = root.prepared.d2[action];
  }
  return result;
}

std::array<double, kBoardSize> ladderScores(const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.stored.label.legal[action]) {
      // Pre-energy is constant within a root, so expected post-energy and
      // delta induce exactly the same sibling ordering/correlation.
      result[action] = root.actions[action].ladder_delta;
    }
  }
  return result;
}

double fitLadderCoefficient(const std::vector<AuditRoot>& roots,
                            int excluded_fold) {
  double numerator = 0.0;
  double denominator = 1.0e-6;
  for (const AuditRoot& root : roots) {
    if (excluded_fold >= 0 && root.stored.label.game % kFolds == excluded_fold) {
      continue;
    }
    for (int first = 0; first < kBoardSize; ++first) {
      if (!root.stored.label.legal[first]) continue;
      for (int second = first + 1; second < kBoardSize; ++second) {
        if (!root.stored.label.legal[second]) continue;
        const double x = root.actions[first].ladder_delta -
                         root.actions[second].ladder_delta;
        const double target =
            (root.prepared.target[first] - root.prepared.target[second]) -
            (root.prepared.d2[first] - root.prepared.d2[second]);
        numerator += x * target;
        denominator += x * x;
      }
    }
  }
  return numerator / denominator;
}

std::array<double, kBoardSize> ladderD2Scores(const AuditRoot& root,
                                               double coefficient) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.stored.label.legal[action]) {
      result[action] = root.prepared.d2[action] +
                       coefficient * root.actions[action].ladder_delta;
    }
  }
  return result;
}

struct LadderAudit {
  Metrics fitting_d2{};
  Metrics fitting_ladder{};
  Metrics fitting_coefficient_cv{};
  std::array<Metrics, kFolds> fitting_d2_folds{};
  std::array<Metrics, kFolds> fitting_cv_folds{};
  std::array<double, kFolds> fold_coefficients{};
  double full_coefficient = 0.0;
  Metrics heldout_d2{};
  Metrics heldout_ladder{};
  Metrics heldout_coefficient{};
  std::array<Metrics, 2> heldout_d2_halves{};
  std::array<Metrics, 2> heldout_coefficient_halves{};
};

LadderAudit auditLadder(const std::vector<AuditRoot>& fitting,
                        const std::vector<AuditRoot>& heldout) {
  const auto all = [](const AuditRoot&) { return true; };
  LadderAudit result;
  result.fitting_d2 = evaluate(fitting, d2Scores, all);
  result.fitting_ladder = evaluate(fitting, ladderScores, all);
  for (int fold = 0; fold < kFolds; ++fold) {
    result.fitting_d2_folds[fold] = evaluate(
        fitting, d2Scores, [fold](const AuditRoot& root) {
          return root.stored.label.game % kFolds == fold;
        });
    result.fold_coefficients[fold] = fitLadderCoefficient(fitting, fold);
    result.fitting_cv_folds[fold] = evaluate(
        fitting,
        [coefficient = result.fold_coefficients[fold]](const AuditRoot& root) {
          return ladderD2Scores(root, coefficient);
        },
        [fold](const AuditRoot& root) {
          return root.stored.label.game % kFolds == fold;
        });
    addMetrics(result.fitting_coefficient_cv,
               result.fitting_cv_folds[fold]);
  }
  result.full_coefficient = fitLadderCoefficient(fitting, -1);
  result.heldout_d2 = evaluate(heldout, d2Scores, all);
  result.heldout_ladder = evaluate(heldout, ladderScores, all);
  result.heldout_coefficient = evaluate(
      heldout,
      [coefficient = result.full_coefficient](const AuditRoot& root) {
        return ladderD2Scores(root, coefficient);
      }, all);
  for (int half = 0; half < 2; ++half) {
    result.heldout_d2_halves[half] = evaluate(
        heldout, d2Scores, [half](const AuditRoot& root) {
          const int middle = base::kHeldoutGames / 2;
          return half == 0 ? root.stored.label.game < middle
                           : root.stored.label.game >= middle;
        });
    result.heldout_coefficient_halves[half] = evaluate(
        heldout,
        [coefficient = result.full_coefficient](const AuditRoot& root) {
          return ladderD2Scores(root, coefficient);
        },
        [half](const AuditRoot& root) {
          const int middle = base::kHeldoutGames / 2;
          return half == 0 ? root.stored.label.game < middle
                           : root.stored.label.game >= middle;
        });
  }
  return result;
}

void writeMetrics(std::ostream& output, const Metrics& value) {
  output << std::setprecision(10) << "{\"roots\":"
         << value.ranking.roots << ",\"top1WithTies\":"
         << base::top1Rate(value.ranking)
         << ",\"top2ContainsOptimal\":"
         << base::top2Rate(value.ranking) << ",\"pairwiseAccuracy\":"
         << base::pairwiseRate(value.ranking)
         << ",\"normalizedRegret\":" << base::regret(value.ranking)
         << ",\"pairDifferencePearson\":"
         << pairCorrelation(value.pairs) << ",\"pairSignAgreement\":"
         << value.pairs.sign_credit / value.pairs.pairs
         << ",\"pairCount\":" << value.pairs.pairs << '}';
}

enum Head : int {
  kMeanReturnResidual = 0,
  kSurvival = 1,
  kNumberedClears = 2,
  kDownside = 3,
  kVariance = 4,
};

struct NeuralModel {
  std::vector<float> input_weights;
  std::array<float, kHidden> hidden_bias{};
  std::array<float, kHeads * kHidden> head_weights{};
  std::array<float, kHeads> head_bias{};
  std::array<float, kInputs> input_scale{};
  int epochs = 0;

  NeuralModel() : input_weights(kInputs * kHidden) {}
};

std::array<double, kLadderInputs> ladderInputs(const AuditRoot& root,
                                               int action) {
  return {{root.actions[action].pre_ladder,
           root.actions[action].expected_post_ladder,
           root.actions[action].ladder_delta}};
}

template <typename Function>
void forEachInput(const AuditRoot& root, int action, bool reflected,
                  Function function) {
  const base::FeatureVector& sparse =
      reflected ? root.prepared.reflected[action]
                : root.prepared.direct[action];
  for (const base::SparseFeature& feature : sparse) {
    function(static_cast<int>(feature.index), feature.value);
  }
  const auto ladder = ladderInputs(root, action);
  for (int index = 0; index < kLadderInputs; ++index) {
    function(base::kFeatureCount + index, ladder[index]);
  }
}

template <typename IncludeFunction>
std::array<float, kInputs> inputScales(
    const std::vector<AuditRoot>& roots, IncludeFunction include) {
  std::array<double, kInputs> squares{};
  std::uint64_t orientations = 0;
  for (const AuditRoot& root : roots) {
    if (!include(root)) continue;
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.stored.label.legal[action]) continue;
      for (const bool reflected : {false, true}) {
        forEachInput(root, action, reflected, [&](int index, double value) {
          squares[index] += value * value;
        });
        ++orientations;
      }
    }
  }
  if (orientations == 0) throw std::runtime_error("empty NNUE scale corpus");
  std::array<float, kInputs> result{};
  for (int index = 0; index < kInputs; ++index) {
    const double rms =
        std::sqrt(squares[index] / static_cast<double>(orientations));
    result[index] = static_cast<float>(
        std::min(10.0, 1.0 / std::max(0.05, rms)));
  }
  return result;
}

double randomSigned(std::uint32_t& state) {
  state = mix32(state + 0x9e37'79b9u);
  const double unit =
      static_cast<double>(state >> 8u) / static_cast<double>(1u << 24u);
  return 2.0 * unit - 1.0;
}

template <typename IncludeFunction>
NeuralModel initializedModel(const std::vector<AuditRoot>& roots,
                             IncludeFunction include, std::uint32_t seed) {
  NeuralModel result;
  result.input_scale = inputScales(roots, include);
  std::uint32_t random = seed;
  for (float& value : result.input_weights) {
    value = static_cast<float>(0.025 * randomSigned(random));
  }
  for (float& value : result.head_weights) {
    value = static_cast<float>(0.08 * randomSigned(random));
  }
  return result;
}

struct Forward {
  std::array<double, kHidden> direct_z{};
  std::array<double, kHidden> reflected_z{};
  std::array<double, kHidden> hidden{};
  std::array<double, kHeads> heads{};
};

Forward forward(const NeuralModel& model, const AuditRoot& root, int action) {
  Forward result;
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    result.direct_z[hidden] = model.hidden_bias[hidden];
    result.reflected_z[hidden] = model.hidden_bias[hidden];
  }
  forEachInput(root, action, false, [&](int input, double value) {
    const double scaled = value * model.input_scale[input];
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.direct_z[hidden] +=
          model.input_weights[hidden * kInputs + input] * scaled;
    }
  });
  forEachInput(root, action, true, [&](int input, double value) {
    const double scaled = value * model.input_scale[input];
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.reflected_z[hidden] +=
          model.input_weights[hidden * kInputs + input] * scaled;
    }
  });
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    result.hidden[hidden] =
        0.5 * (std::max(0.0, result.direct_z[hidden]) +
               std::max(0.0, result.reflected_z[hidden]));
  }
  for (int head = 0; head < kHeads; ++head) {
    result.heads[head] = model.head_bias[head];
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      result.heads[head] +=
          model.head_weights[head * kHidden + hidden] *
          result.hidden[hidden];
    }
  }
  return result;
}

std::array<double, kHeads> headTargets(const AuditRoot& root, int action) {
  return {{root.prepared.target[action] - root.prepared.d2[action],
           root.actions[action].survival,
           root.actions[action].mean_clears,
           root.actions[action].downside,
           root.actions[action].variance}};
}

struct NeuralGradient {
  std::vector<double> input_weights;
  std::array<double, kHidden> hidden_bias{};
  std::array<double, kHeads * kHidden> head_weights{};
  std::array<double, kHeads> head_bias{};

  NeuralGradient() : input_weights(kInputs * kHidden) {}
};

void accumulateGradient(const NeuralModel& model, const AuditRoot& root,
                        int action, NeuralGradient& gradient) {
  const Forward computed = forward(model, root, action);
  const auto target = headTargets(root, action);
  std::array<double, kHeads> head_gradient{};
  for (int head = 0; head < kHeads; ++head) {
    head_gradient[head] =
        2.0 * kHeadLossWeights[head] * (computed.heads[head] - target[head]);
    gradient.head_bias[head] += head_gradient[head];
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      gradient.head_weights[head * kHidden + hidden] +=
          head_gradient[head] * computed.hidden[hidden];
    }
  }
  std::array<double, kHidden> hidden_gradient{};
  for (int hidden = 0; hidden < kHidden; ++hidden) {
    for (int head = 0; head < kHeads; ++head) {
      hidden_gradient[hidden] +=
          head_gradient[head] *
          model.head_weights[head * kHidden + hidden];
    }
    gradient.hidden_bias[hidden] +=
        0.5 * hidden_gradient[hidden] *
        ((computed.direct_z[hidden] > 0.0 ? 1.0 : 0.0) +
         (computed.reflected_z[hidden] > 0.0 ? 1.0 : 0.0));
  }
  for (const bool reflected : {false, true}) {
    forEachInput(root, action, reflected, [&](int input, double value) {
      const double scaled = value * model.input_scale[input];
      for (int hidden = 0; hidden < kHidden; ++hidden) {
        const double z = reflected ? computed.reflected_z[hidden]
                                   : computed.direct_z[hidden];
        if (z > 0.0) {
          gradient.input_weights[hidden * kInputs + input] +=
              0.5 * hidden_gradient[hidden] * scaled;
        }
      }
    });
  }
}

struct AdamState {
  NeuralGradient first{};
  NeuralGradient second{};
  std::uint64_t step = 0;
};

void updateValue(float& parameter, double gradient, double& first,
                 double& second, std::uint64_t step) {
  first = 0.9 * first + 0.1 * gradient;
  second = 0.999 * second + 0.001 * gradient * gradient;
  const double corrected_first = first / (1.0 - std::pow(0.9, step));
  const double corrected_second = second / (1.0 - std::pow(0.999, step));
  parameter -= static_cast<float>(
      kLearningRate * corrected_first / (std::sqrt(corrected_second) + 1.0e-8));
}

template <typename IncludeFunction>
std::vector<NeuralModel> trainSnapshots(
    const std::vector<AuditRoot>& roots, IncludeFunction include,
    const std::vector<int>& checkpoints, std::uint32_t seed) {
  if (checkpoints.empty() || checkpoints.front() < 1 ||
      !std::is_sorted(checkpoints.begin(), checkpoints.end())) {
    throw std::invalid_argument("invalid neural checkpoints");
  }
  NeuralModel model = initializedModel(roots, include, seed);
  AdamState adam;
  std::vector<NeuralModel> result;
  result.reserve(checkpoints.size());
  for (int epoch = 1; epoch <= checkpoints.back(); ++epoch) {
    NeuralGradient gradient;
    std::uint64_t rows = 0;
    for (const AuditRoot& root : roots) {
      if (!include(root)) continue;
      for (int action = 0; action < kBoardSize; ++action) {
        if (!root.stored.label.legal[action]) continue;
        accumulateGradient(model, root, action, gradient);
        ++rows;
      }
    }
    if (rows == 0) throw std::runtime_error("empty neural training fold");
    ++adam.step;
    const double inverse_rows = 1.0 / static_cast<double>(rows);
    for (std::size_t index = 0; index < model.input_weights.size(); ++index) {
      const double value = gradient.input_weights[index] * inverse_rows +
                           kWeightDecay * model.input_weights[index];
      updateValue(model.input_weights[index], value,
                  adam.first.input_weights[index],
                  adam.second.input_weights[index], adam.step);
    }
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      updateValue(model.hidden_bias[hidden],
                  gradient.hidden_bias[hidden] * inverse_rows,
                  adam.first.hidden_bias[hidden],
                  adam.second.hidden_bias[hidden], adam.step);
    }
    for (int index = 0; index < kHeads * kHidden; ++index) {
      const double value = gradient.head_weights[index] * inverse_rows +
                           kWeightDecay * model.head_weights[index];
      updateValue(model.head_weights[index], value,
                  adam.first.head_weights[index],
                  adam.second.head_weights[index], adam.step);
    }
    for (int head = 0; head < kHeads; ++head) {
      updateValue(model.head_bias[head],
                  gradient.head_bias[head] * inverse_rows,
                  adam.first.head_bias[head],
                  adam.second.head_bias[head], adam.step);
    }
    if (std::find(checkpoints.begin(), checkpoints.end(), epoch) !=
        checkpoints.end()) {
      model.epochs = epoch;
      result.push_back(model);
    }
  }
  if (result.size() != checkpoints.size()) {
    throw std::logic_error("neural checkpoint schedule failed");
  }
  return result;
}

template <typename IncludeFunction>
NeuralModel trainModel(const std::vector<AuditRoot>& roots,
                       IncludeFunction include, int epochs,
                       std::uint32_t seed) {
  return trainSnapshots(roots, include, std::vector<int>{epochs}, seed).front();
}

std::array<double, kBoardSize> neuralScores(const NeuralModel& model,
                                             const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.stored.label.legal[action]) continue;
    result[action] = root.prepared.d2[action] +
                     forward(model, root, action).heads[kMeanReturnResidual];
  }
  return result;
}

struct HeadMoments {
  std::uint64_t rows = 0;
  std::array<double, kHeads> squared_error{};
  std::array<double, kHeads> sum_prediction{};
  std::array<double, kHeads> sum_target{};
  std::array<double, kHeads> sum_prediction_squared{};
  std::array<double, kHeads> sum_target_squared{};
  std::array<double, kHeads> sum_product{};
};

void addHeadMoments(HeadMoments& target, const HeadMoments& source) {
  target.rows += source.rows;
  for (int head = 0; head < kHeads; ++head) {
    target.squared_error[head] += source.squared_error[head];
    target.sum_prediction[head] += source.sum_prediction[head];
    target.sum_target[head] += source.sum_target[head];
    target.sum_prediction_squared[head] += source.sum_prediction_squared[head];
    target.sum_target_squared[head] += source.sum_target_squared[head];
    target.sum_product[head] += source.sum_product[head];
  }
}

double headCorrelation(const HeadMoments& value, int head) {
  const double count = static_cast<double>(value.rows);
  const double covariance = value.sum_product[head] -
      value.sum_prediction[head] * value.sum_target[head] / count;
  const double prediction_variance = value.sum_prediction_squared[head] -
      value.sum_prediction[head] * value.sum_prediction[head] / count;
  const double target_variance = value.sum_target_squared[head] -
      value.sum_target[head] * value.sum_target[head] / count;
  const double denominator = std::sqrt(std::max(0.0, prediction_variance) *
                                       std::max(0.0, target_variance));
  return denominator > 0.0 ? covariance / denominator : 0.0;
}

template <typename IncludeFunction>
std::pair<Metrics, HeadMoments> evaluateNeural(
    const NeuralModel& model, const std::vector<AuditRoot>& roots,
    IncludeFunction include) {
  Metrics metrics = evaluate(
      roots, [&model](const AuditRoot& root) {
        return neuralScores(model, root);
      }, include);
  HeadMoments heads;
  for (const AuditRoot& root : roots) {
    if (!include(root)) continue;
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.stored.label.legal[action]) continue;
      const auto prediction = forward(model, root, action).heads;
      const auto target = headTargets(root, action);
      ++heads.rows;
      for (int head = 0; head < kHeads; ++head) {
        const double error = prediction[head] - target[head];
        heads.squared_error[head] += error * error;
        heads.sum_prediction[head] += prediction[head];
        heads.sum_target[head] += target[head];
        heads.sum_prediction_squared[head] += prediction[head] * prediction[head];
        heads.sum_target_squared[head] += target[head] * target[head];
        heads.sum_product[head] += prediction[head] * target[head];
      }
    }
  }
  return {metrics, heads};
}

struct NeuralAudit {
  Metrics fitting_d2{};
  Metrics fitting_nested_cv{};
  HeadMoments fitting_heads{};
  std::array<Metrics, kFolds> d2_folds{};
  std::array<Metrics, kFolds> folds{};
  std::array<int, kFolds> selected_epochs{};
  int final_epochs = 0;
  NeuralModel final_model{};
  Metrics heldout_d2{};
  Metrics heldout{};
  HeadMoments heldout_heads{};
  std::array<Metrics, 2> heldout_d2_halves{};
  std::array<Metrics, 2> heldout_halves{};
};

bool betterInner(const Metrics& first, const Metrics& second) {
  const double first_pair = base::pairwiseRate(first.ranking);
  const double second_pair = base::pairwiseRate(second.ranking);
  if (first_pair != second_pair) return first_pair > second_pair;
  const double first_top1 = base::top1Rate(first.ranking);
  const double second_top1 = base::top1Rate(second.ranking);
  if (first_top1 != second_top1) return first_top1 > second_top1;
  return base::regret(first.ranking) < base::regret(second.ranking);
}

NeuralAudit auditNeural(const std::vector<AuditRoot>& fitting,
                        const std::vector<AuditRoot>& heldout) {
  const auto all = [](const AuditRoot&) { return true; };
  NeuralAudit result;
  result.fitting_d2 = evaluate(fitting, d2Scores, all);
  for (int outer = 0; outer < kFolds; ++outer) {
    result.d2_folds[outer] = evaluate(
        fitting, d2Scores, [outer](const AuditRoot& root) {
          return root.stored.label.game % kFolds == outer;
        });
    const int inner = (outer + 1) % kFolds;
    const auto inner_train = [outer, inner](const AuditRoot& root) {
      const int fold = root.stored.label.game % kFolds;
      return fold != outer && fold != inner;
    };
    const std::vector<int> checkpoints(kEpochCandidates.begin(),
                                       kEpochCandidates.end());
    const auto candidates = trainSnapshots(
        fitting, inner_train, checkpoints,
        0x4e4e'0000u + static_cast<std::uint32_t>(outer));
    int selected = 0;
    Metrics selected_metrics;
    bool have_selected = false;
    for (int candidate = 0; candidate < static_cast<int>(candidates.size());
         ++candidate) {
      const auto evaluated = evaluateNeural(
          candidates[candidate], fitting, [inner](const AuditRoot& root) {
            return root.stored.label.game % kFolds == inner;
          });
      if (!have_selected || betterInner(evaluated.first, selected_metrics)) {
        selected = candidate;
        selected_metrics = evaluated.first;
        have_selected = true;
      }
    }
    result.selected_epochs[outer] = kEpochCandidates[selected];
    const NeuralModel outer_model = trainModel(
        fitting,
        [outer](const AuditRoot& root) {
          return root.stored.label.game % kFolds != outer;
        }, result.selected_epochs[outer],
        0x4f55'0000u + static_cast<std::uint32_t>(outer));
    const auto evaluated = evaluateNeural(
        outer_model, fitting, [outer](const AuditRoot& root) {
          return root.stored.label.game % kFolds == outer;
        });
    result.folds[outer] = evaluated.first;
    addMetrics(result.fitting_nested_cv, evaluated.first);
    addHeadMoments(result.fitting_heads, evaluated.second);
  }
  std::array<int, kFolds> sorted_epochs = result.selected_epochs;
  std::sort(sorted_epochs.begin(), sorted_epochs.end());
  result.final_epochs = sorted_epochs[(kFolds - 1) / 2];
  result.final_model = trainModel(fitting, all, result.final_epochs,
                                  0x4e4e'464eu);
  result.heldout_d2 = evaluate(heldout, d2Scores, all);
  const auto heldout_evaluation =
      evaluateNeural(result.final_model, heldout, all);
  result.heldout = heldout_evaluation.first;
  result.heldout_heads = heldout_evaluation.second;
  for (int half = 0; half < 2; ++half) {
    const auto include_half = [half](const AuditRoot& root) {
      const int middle = base::kHeldoutGames / 2;
      return half == 0 ? root.stored.label.game < middle
                       : root.stored.label.game >= middle;
    };
    result.heldout_d2_halves[half] =
        evaluate(heldout, d2Scores, include_half);
    result.heldout_halves[half] = evaluateNeural(
        result.final_model, heldout, include_half).first;
  }
  return result;
}

std::uint64_t modelFingerprint(const NeuralModel& model) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  const auto consume = [&hash](float value) {
    std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (int byte = 0; byte < 4; ++byte) {
      hash ^= bits & 0xffu;
      hash *= 0x0000'0100'0000'01b3ull;
      bits >>= 8u;
    }
  };
  for (const float value : model.input_weights) consume(value);
  for (const float value : model.hidden_bias) consume(value);
  for (const float value : model.head_weights) consume(value);
  for (const float value : model.head_bias) consume(value);
  for (const float value : model.input_scale) consume(value);
  hash ^= static_cast<std::uint64_t>(model.epochs);
  hash *= 0x0000'0100'0000'01b3ull;
  return hash;
}

constexpr std::array<char, 8> kCheckpointMagic{{
    'D', '7', 'M', 'H', 'N', 'N', '1', '\0',
}};

struct CheckpointHeader {
  std::array<char, 8> magic{};
  std::uint32_t inputs = 0;
  std::uint32_t hidden = 0;
  std::uint32_t heads = 0;
  std::uint32_t epochs = 0;
  std::uint64_t fingerprint = 0;
};

template <typename Values>
void writeFloats(std::ostream& output, const Values& values) {
  output.write(reinterpret_cast<const char*>(values.data()),
               static_cast<std::streamsize>(values.size() * sizeof(float)));
}

template <typename Values>
void readFloats(std::istream& input, Values& values) {
  input.read(reinterpret_cast<char*>(values.data()),
             static_cast<std::streamsize>(values.size() * sizeof(float)));
}

void writeCheckpoint(const std::string& path, const NeuralModel& model) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not write multi-head checkpoint");
  const CheckpointHeader header{kCheckpointMagic, kInputs, kHidden, kHeads,
                                static_cast<std::uint32_t>(model.epochs),
                                modelFingerprint(model)};
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  writeFloats(output, model.input_weights);
  writeFloats(output, model.hidden_bias);
  writeFloats(output, model.head_weights);
  writeFloats(output, model.head_bias);
  writeFloats(output, model.input_scale);
  if (!output) throw std::runtime_error("multi-head checkpoint write failed");
}

NeuralModel readCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read multi-head checkpoint");
  CheckpointHeader header;
  NeuralModel model;
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  readFloats(input, model.input_weights);
  readFloats(input, model.hidden_bias);
  readFloats(input, model.head_weights);
  readFloats(input, model.head_bias);
  readFloats(input, model.input_scale);
  model.epochs = static_cast<int>(header.epochs);
  const bool payload_ok = static_cast<bool>(input);
  char trailing = 0;
  const bool has_trailing = static_cast<bool>(input.read(&trailing, 1));
  if (!payload_ok || !input.eof() || has_trailing ||
      header.magic != kCheckpointMagic || header.inputs != kInputs ||
      header.hidden != kHidden || header.heads != kHeads ||
      header.fingerprint != modelFingerprint(model)) {
    throw std::runtime_error("invalid multi-head checkpoint");
  }
  return model;
}

std::uint64_t fileBytes(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not size feature-audit file");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid feature-audit file size");
  return static_cast<std::uint64_t>(end);
}

struct ConsistencyGate {
  bool cv_top1 = false;
  bool cv_pairwise = false;
  bool cv_regret = false;
  int stable_folds = 0;
  bool heldout_top1 = false;
  bool heldout_pairwise = false;
  bool heldout_regret = false;
  bool heldout_halves = false;
  bool passed = false;
};

ConsistencyGate consistencyGate(
    const Metrics& cv_d2, const Metrics& cv_candidate,
    const std::array<Metrics, kFolds>& d2_folds,
    const std::array<Metrics, kFolds>& candidate_folds,
    const Metrics& heldout_d2, const Metrics& heldout_candidate,
    const std::array<Metrics, 2>& heldout_d2_halves,
    const std::array<Metrics, 2>& heldout_candidate_halves) {
  ConsistencyGate result;
  result.cv_top1 =
      base::top1Rate(cv_candidate.ranking) >=
      base::top1Rate(cv_d2.ranking) + kGateTop1Improvement;
  result.cv_pairwise =
      base::pairwiseRate(cv_candidate.ranking) >=
      base::pairwiseRate(cv_d2.ranking) + kGatePairwiseImprovement;
  result.cv_regret =
      base::regret(cv_candidate.ranking) <=
      kGateRegretRatio * base::regret(cv_d2.ranking);
  for (int fold = 0; fold < kFolds; ++fold) {
    result.stable_folds +=
        base::top1Rate(candidate_folds[fold].ranking) >=
            base::top1Rate(d2_folds[fold].ranking) &&
        base::pairwiseRate(candidate_folds[fold].ranking) >=
            base::pairwiseRate(d2_folds[fold].ranking) &&
        base::regret(candidate_folds[fold].ranking) <=
            base::regret(d2_folds[fold].ranking);
  }
  result.heldout_top1 =
      base::top1Rate(heldout_candidate.ranking) >=
      base::top1Rate(heldout_d2.ranking) + kGateTop1Improvement;
  result.heldout_pairwise =
      base::pairwiseRate(heldout_candidate.ranking) >=
      base::pairwiseRate(heldout_d2.ranking) + kGatePairwiseImprovement;
  result.heldout_regret =
      base::regret(heldout_candidate.ranking) <=
      kGateRegretRatio * base::regret(heldout_d2.ranking);
  result.heldout_halves = true;
  for (int half = 0; half < 2; ++half) {
    result.heldout_halves =
        result.heldout_halves &&
        base::top1Rate(heldout_candidate_halves[half].ranking) >=
            base::top1Rate(heldout_d2_halves[half].ranking) &&
        base::pairwiseRate(heldout_candidate_halves[half].ranking) >=
            base::pairwiseRate(heldout_d2_halves[half].ranking) &&
        base::regret(heldout_candidate_halves[half].ranking) <=
            base::regret(heldout_d2_halves[half].ranking);
  }
  result.passed = result.cv_top1 && result.cv_pairwise && result.cv_regret &&
                  result.stable_folds >= 5 && result.heldout_top1 &&
                  result.heldout_pairwise && result.heldout_regret &&
                  result.heldout_halves;
  return result;
}

void writeGate(std::ostream& output, const ConsistencyGate& value) {
  output << "{\"passed\":" << (value.passed ? "true" : "false")
         << ",\"cvTop1\":" << (value.cv_top1 ? "true" : "false")
         << ",\"cvPairwise\":"
         << (value.cv_pairwise ? "true" : "false")
         << ",\"cvRegret\":" << (value.cv_regret ? "true" : "false")
         << ",\"stableOuterFolds\":" << value.stable_folds
         << ",\"requiredStableOuterFolds\":5,\"oldHeldoutTop1\":"
         << (value.heldout_top1 ? "true" : "false")
         << ",\"oldHeldoutPairwise\":"
         << (value.heldout_pairwise ? "true" : "false")
         << ",\"oldHeldoutRegret\":"
         << (value.heldout_regret ? "true" : "false")
         << ",\"oldHeldoutBothHalves\":"
         << (value.heldout_halves ? "true" : "false") << '}';
}

void writeHeadMetrics(std::ostream& output, const HeadMoments& value) {
  constexpr std::array<std::string_view, kHeads> names{{
      "meanReturnResidual", "survival", "numberedClears", "downside",
      "variance",
  }};
  output << '{';
  for (int head = 0; head < kHeads; ++head) {
    if (head > 0) output << ',';
    output << '"' << names[head] << "\":{\"rmse\":"
           << std::sqrt(value.squared_error[head] / value.rows)
           << ",\"pearson\":" << headCorrelation(value, head) << '}';
  }
  output << '}';
}

void writeDerived(const Options& options,
                  const std::vector<AuditRoot>& fitting,
                  const std::vector<AuditRoot>& heldout) {
  std::ofstream output(options.derived);
  if (!output) throw std::runtime_error("could not write derived feature file");
  output << "{\"type\":\"metadata\","
            "\"format\":\"drop7-long-outcome-derived-features-v1\","
            "\"inputSha256\":"
            "\"621302a0cd8334fa56e5b77c191beb5529eda0e5413b8e7e20d524c852e7ea7a\","
            "\"joinedD4SourceSha256\":"
            "\"e97f0a00dad76ce0e47bd60d5824e4e921e57b2cb47990b28b5bd4a562dd56bf\","
            "\"instrumentationReplayOfSameRoots\":true,"
            "\"newRoots\":0,\"newGameSeeds\":0,\"horizon\":25,"
            "\"scenarios\":7}\n";
  const auto write_range = [&](const std::vector<AuditRoot>& roots,
                               std::string_view split) {
    for (const AuditRoot& root : roots) {
      output << std::setprecision(17) << "{\"type\":\"root\",\"split\":\""
             << split << "\",\"game\":" << root.stored.label.game
             << ",\"moveInSourceGame\":"
             << root.stored.label.move_in_game << ",\"board\":\""
             << prior::encodedBoard(root.stored.label.board)
             << "\",\"preLadderEnergy\":";
      double pre = 0.0;
      for (int action = 0; action < kBoardSize; ++action) {
        if (root.stored.label.legal[action]) {
          pre = root.actions[action].pre_ladder;
          break;
        }
      }
      output << pre << ",\"d4Action\":" << root.stored.d4.labeled_action
             << ",\"d4RootQ\":[";
      for (int action = 0; action < kBoardSize; ++action) {
        if (action > 0) output << ',';
        if (root.stored.d4.legal[action]) {
          output << root.stored.d4.q[action];
        } else {
          output << "null";
        }
      }
      output << "],\"actions\":[";
      for (int action = 0; action < kBoardSize; ++action) {
        if (action > 0) output << ',';
        if (!root.stored.label.legal[action]) {
          output << "null";
          continue;
        }
        const ActionAux& aux = root.actions[action];
        output << "{\"postLadderByScenario\":[";
        for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
          if (scenario > 0) output << ',';
          output << aux.post_ladder[scenario];
        }
        output << "],\"expectedPostLadder\":" << aux.expected_post_ladder
               << ",\"ladderDelta\":" << aux.ladder_delta
               << ",\"scenarioReturns\":[";
        for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
          if (scenario > 0) output << ',';
          output << root.stored.returns[action][scenario];
        }
        output << "],\"scenarioSurvived\":[";
        for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
          if (scenario > 0) output << ',';
          output << (aux.scenario_survived[scenario] ? "true" : "false");
        }
        output << "],\"scenarioNumberedClears\":[";
        for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
          if (scenario > 0) output << ',';
          output << aux.scenario_clears[scenario];
        }
        output << ']'
               << ",\"survivalRate\":" << aux.survival
               << ",\"rawMeanNumberedClears\":" << aux.raw_mean_clears
               << ",\"normalizedMeanNumberedClears\":" << aux.mean_clears
               << ",\"normalizedDownside\":" << aux.downside
               << ",\"normalizedVariance\":" << aux.variance << '}';
      }
      output << "]}\n";
    }
  };
  write_range(fitting, "fitting");
  write_range(heldout, "old-heldout-architecture-development");
}

double neuralSwapGap(const NeuralModel& model,
                     const std::vector<AuditRoot>& roots) {
  double maximum = 0.0;
  for (const AuditRoot& root : roots) {
    AuditRoot swapped = root;
    for (int action = 0; action < kBoardSize; ++action) {
      std::swap(swapped.prepared.direct[action],
                swapped.prepared.reflected[action]);
      if (!root.stored.label.legal[action]) continue;
      const auto first = forward(model, root, action).heads;
      const auto second = forward(model, swapped, action).heads;
      for (int head = 0; head < kHeads; ++head) {
        maximum = std::max(maximum, std::abs(first[head] - second[head]));
      }
    }
  }
  return maximum;
}

struct NeuralThroughput {
  std::uint64_t action_evaluations = 0;
  double seconds = 0.0;
  double actions_per_second = 0.0;
  double checksum = 0.0;
};

NeuralThroughput benchmarkNeural(const NeuralModel& model,
                                 const std::vector<AuditRoot>& roots) {
  constexpr int kRepetitions = 100;
  NeuralThroughput result;
  const auto started = Clock::now();
  for (int repetition = 0; repetition < kRepetitions; ++repetition) {
    for (const AuditRoot& root : roots) {
      for (int action = 0; action < kBoardSize; ++action) {
        if (!root.stored.label.legal[action]) continue;
        const auto predicted = forward(model, root, action).heads;
        result.checksum += predicted[kMeanReturnResidual] *
                           static_cast<double>(action + 1 + (repetition & 1));
        ++result.action_evaluations;
      }
    }
  }
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.actions_per_second = result.action_evaluations / result.seconds;
  return result;
}

void writeFoldMetrics(std::ostream& output,
                      const std::array<Metrics, kFolds>& d2,
                      const std::array<Metrics, kFolds>& candidate,
                      const std::array<int, kFolds>* epochs = nullptr,
                      const std::array<double, kFolds>* coefficients = nullptr) {
  output << '[';
  for (int fold = 0; fold < kFolds; ++fold) {
    if (fold > 0) output << ',';
    output << "{\"fold\":" << fold;
    if (epochs != nullptr) {
      output << ",\"selectedEpochs\":" << (*epochs)[fold];
    }
    if (coefficients != nullptr) {
      output << ",\"fittedCoefficient\":" << (*coefficients)[fold];
    }
    output << ",\"d2\":";
    writeMetrics(output, d2[fold]);
    output << ",\"candidate\":";
    writeMetrics(output, candidate[fold]);
    output << '}';
  }
  output << ']';
}

void writeArtifact(
    const Options& options, const LadderAudit& ladder,
    const NeuralAudit& neural, const ConsistencyGate& ladder_gate,
    const ConsistencyGate& neural_gate, double maximum_return_error,
    double replay_seconds, double total_seconds, std::uint64_t checkpoint_bytes,
    std::uint64_t checkpoint_fingerprint, double neural_swap_gap,
    const NeuralThroughput& throughput, bool resources_passed) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write feature-audit artifact");
  const bool preregister =
      resources_passed && (ladder_gate.passed || neural_gate.passed);
  output << std::setprecision(10)
         << "{\n  \"experiment\":\"d2-long-outcome-feature-audit\",\n"
            "  \"status\":\"complete\",\n"
            "  \"evidenceClass\":\"architecture-development-only\",\n"
            "  \"claimBoundary\":\"old heldout was previously opened and is not reusable formal evidence; no gameplay or new label family\",\n"
            "  \"input\":{\"preservedLabels\":\""
         << options.labels
         << "\",\"sha256\":\"621302a0cd8334fa56e5b77c191beb5529eda0e5413b8e7e20d524c852e7ea7a\","
            "\"joinedOriginalD4Source\":\""
         << options.d4_source
         << "\",\"joinedOriginalD4Sha256\":\"e97f0a00dad76ce0e47bd60d5824e4e921e57b2cb47990b28b5bd4a562dd56bf\","
            "\"fittingRoots\":288,\"oldHeldoutRoots\":144,"
            "\"instrumentationReplayOfIdenticalRootsAndTapes\":true,"
            "\"maximumPersistedReturnReplayError\":"
         << maximum_return_error
         << ",\"newRoots\":0,\"newGameSeeds\":0,\"newTapeDomains\":0,"
            "\"fresh3eSeedsRead\":0,\"validation7dSeedsRead\":0,"
            "\"finalD7SeedsRead\":0},\n"
            "  \"verticalLadder\":{\"definition\":\"exact conservative vertical-only inert-addition activation/clears/waves energy from approaches/fair-expectimax/vertical-ladder/d2-vertical-ladder-probe.cpp\","
            "\"actionFeature\":\"expected post-first-step energy over the same seven CRN scenarios; delta subtracts root pre-energy and has identical within-root ranking\","
            "\"fittingD2\":";
  writeMetrics(output, ladder.fitting_d2);
  output << ",\"fittingLadderAlone\":";
  writeMetrics(output, ladder.fitting_ladder);
  output << ",\"fittingD2PlusCoefficientNestedCV\":";
  writeMetrics(output, ladder.fitting_coefficient_cv);
  output << ",\"fullFittingCoefficient\":" << ladder.full_coefficient
         << ",\"folds\":";
  writeFoldMetrics(output, ladder.fitting_d2_folds,
                   ladder.fitting_cv_folds, nullptr,
                   &ladder.fold_coefficients);
  output << ",\"oldHeldoutD2\":";
  writeMetrics(output, ladder.heldout_d2);
  output << ",\"oldHeldoutLadderAlone\":";
  writeMetrics(output, ladder.heldout_ladder);
  output << ",\"oldHeldoutD2PlusCoefficient\":";
  writeMetrics(output, ladder.heldout_coefficient);
  output << ",\"consistencyGate\":";
  writeGate(output, ladder_gate);
  output << "},\n  \"multiHeadNNUE\":{\"architecture\":{\"inputs\":"
         << kInputs << ",\"hiddenReLU\":" << kHidden
         << ",\"heads\":[\"meanReturnResidual\",\"survival\","
            "\"numberedClears\",\"downside\",\"variance\"],"
            "\"reflection\":\"shared direct/reflected accumulator then exact mean\","
            "\"nestedSelection\":\"six outer whole-game folds; next fold inner-validates 40/80/120 epochs; retrain outer model; final epoch is lower median\","
            "\"finalEpochs\":"
         << neural.final_epochs << ",\"checkpointBytes\":"
         << checkpoint_bytes << ",\"checkpointLimitBytes\":"
         << kMaximumCheckpointBytes << ",\"fingerprintFnv1a64\":\"0x"
         << std::hex << checkpoint_fingerprint << std::dec << "\"},"
            "\"fittingD2\":";
  writeMetrics(output, neural.fitting_d2);
  output << ",\"fittingNestedCV\":";
  writeMetrics(output, neural.fitting_nested_cv);
  output << ",\"fittingNestedHeadPrediction\":";
  writeHeadMetrics(output, neural.fitting_heads);
  output << ",\"folds\":";
  writeFoldMetrics(output, neural.d2_folds, neural.folds,
                   &neural.selected_epochs);
  output << ",\"oldHeldoutD2\":";
  writeMetrics(output, neural.heldout_d2);
  output << ",\"oldHeldoutCandidate\":";
  writeMetrics(output, neural.heldout);
  output << ",\"oldHeldoutHeadPrediction\":";
  writeHeadMetrics(output, neural.heldout_heads);
  output << ",\"consistencyGate\":";
  writeGate(output, neural_gate);
  output << "},\n  \"implementation\":{\"replaySeconds\":" << replay_seconds
         << ",\"totalSeconds\":" << total_seconds
         << ",\"maximumReflectionAccumulatorSwapGap\":"
         << neural_swap_gap << ",\"inferenceActionsPerSecond\":"
         << throughput.actions_per_second << ",\"benchmarkActions\":"
         << throughput.action_evaluations << ",\"benchmarkSeconds\":"
         << throughput.seconds << ",\"benchmarkChecksum\":"
         << throughput.checksum << ",\"peakRssBytes\":"
         << prior::peakRssBytes() << ",\"rssLimitBytes\":"
         << kMaximumRssBytes << ",\"resourceChecksPassed\":"
         << (resources_passed ? "true" : "false")
         << "},\n  \"preregisterNewDisjointCorpus\":";
  if (preregister) {
    output << "{\"recommended\":true,\"collectionExecuted\":false,"
              "\"candidate\":\""
           << (neural_gate.passed ? "multi-head-NNUE" : "vertical-ladder-D2")
           << "\",\"requiredNextStep\":\"freeze a new disjoint training-only public-root label corpus and formal gate before any collection\"}";
  } else {
    output << "{\"recommended\":false,\"collectionExecuted\":false,"
              "\"reason\":\"no candidate consistently beat exact D2 in nested fitting CV and old-heldout development evidence\"}";
  }
  output << ",\n  \"derivedFeatures\":\"" << options.derived
         << "\",\n  \"conclusion\":\""
         << (preregister
                 ? "candidate merits a newly preregistered disjoint label experiment; no collection was performed"
                 : "vertical ladder and nonlinear multi-head hypotheses rejected at architecture development; exact D2 remains anchor")
         << "\"\n}\n";
}

int run(const Options& options, std::ostream& report) {
  const auto started = Clock::now();
  StoredCorpus corpus = loadCorpus(options.labels);
  joinD4(corpus, options.d4_source);
  double maximum_return_error = 0.0;
  const std::vector<AuditRoot> fitting =
      deriveRange(corpus.fitting, "fitting", maximum_return_error);
  const std::vector<AuditRoot> heldout =
      deriveRange(corpus.heldout, "old-heldout", maximum_return_error);
  const double replay_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (replay_seconds > kMaximumReplaySeconds) {
    throw std::runtime_error("identical-root instrumentation replay exceeded limit");
  }
  if (maximum_return_error > 1.0e-8) {
    throw std::runtime_error("instrumentation replay disagreed with preserved returns");
  }
  writeDerived(options, fitting, heldout);

  const LadderAudit ladder = auditLadder(fitting, heldout);
  const NeuralAudit neural = auditNeural(fitting, heldout);
  writeCheckpoint(options.checkpoint, neural.final_model);
  const NeuralModel restored = readCheckpoint(options.checkpoint);
  if (modelFingerprint(restored) != modelFingerprint(neural.final_model)) {
    throw std::runtime_error("multi-head checkpoint roundtrip failed");
  }
  const std::uint64_t checkpoint_bytes = fileBytes(options.checkpoint);
  const ConsistencyGate ladder_gate = consistencyGate(
      ladder.fitting_d2, ladder.fitting_coefficient_cv,
      ladder.fitting_d2_folds, ladder.fitting_cv_folds,
      ladder.heldout_d2, ladder.heldout_coefficient,
      ladder.heldout_d2_halves, ladder.heldout_coefficient_halves);
  const ConsistencyGate neural_gate = consistencyGate(
      neural.fitting_d2, neural.fitting_nested_cv,
      neural.d2_folds, neural.folds, neural.heldout_d2, neural.heldout,
      neural.heldout_d2_halves, neural.heldout_halves);
  const double swap_gap = neuralSwapGap(restored, heldout);
  const NeuralThroughput throughput = benchmarkNeural(restored, heldout);
  const double total_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  const bool resources_passed =
      checkpoint_bytes <= kMaximumCheckpointBytes &&
      prior::peakRssBytes() <= kMaximumRssBytes &&
      swap_gap <= 1.0e-12 && maximum_return_error <= 1.0e-8;
  writeArtifact(options, ladder, neural, ladder_gate, neural_gate,
                maximum_return_error, replay_seconds, total_seconds,
                checkpoint_bytes, modelFingerprint(restored), swap_gap,
                throughput, resources_passed);
  report << std::fixed << std::setprecision(6)
         << "D2_LONG_FEATURE_AUDIT {\"status\":\"complete\","
            "\"ladderCorrelation\":"
         << pairCorrelation(ladder.heldout_ladder.pairs)
         << ",\"ladderCoefficient\":" << ladder.full_coefficient
         << ",\"ladderGatePassed\":"
         << (ladder_gate.passed ? "true" : "false")
         << ",\"d2Top1\":" << base::top1Rate(neural.heldout_d2.ranking)
         << ",\"nnueTop1\":" << base::top1Rate(neural.heldout.ranking)
         << ",\"d2Pairwise\":"
         << base::pairwiseRate(neural.heldout_d2.ranking)
         << ",\"nnuePairwise\":"
         << base::pairwiseRate(neural.heldout.ranking)
         << ",\"d2Regret\":" << base::regret(neural.heldout_d2.ranking)
         << ",\"nnueRegret\":" << base::regret(neural.heldout.ranking)
         << ",\"nnueGatePassed\":"
         << (neural_gate.passed ? "true" : "false")
         << ",\"newCorpusCollected\":false,\"artifact\":\""
         << options.output << "\"}\n";
  return 0;
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool inherited = base::fair::selfTest(output);
  Board board{};
  board[indexOf(6, 2)] = 5;
  board[indexOf(5, 2)] = 4;
  const LadderFeatures ladder = verticalLadderFeatures(board);
  const Board reflected_board = cfpi::detail::mirrorBoard(board);
  const bool ladder_exact =
      std::abs(ladder.energy - 1.0) <= 1.0e-12 &&
      std::abs(ladder.chain_clears - 0.25) <= 1.0e-12 &&
      ladder.best_waves == 2 &&
      verticalLadderFeatures(reflected_board).energy == ladder.energy;

  const State fixture = base::fair::frozen::fixtureState(
      base::fair::frozen::kTypeScriptFixtures[1]);
  const prior::OutcomeLabel outcome =
      prior::evaluateRoot(prior::rootLabel(fixture));
  StoredRoot stored;
  stored.label = outcome.label;
  stored.split = "fitting";
  for (int action = 0; action < kBoardSize; ++action) {
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      stored.returns[action][scenario] =
          outcome.actions[action].scenarios[scenario].value;
    }
  }
  double return_error = 0.0;
  AuditRoot derived = replayAndDerive(stored, return_error);
  std::vector<AuditRoot> tiny(8, derived);
  for (int index = 0; index < static_cast<int>(tiny.size()); ++index) {
    tiny[index].stored.label.game = index;
  }
  const auto all = [](const AuditRoot&) { return true; };
  const NeuralModel first = trainModel(tiny, all, 2, 0x1234'5678u);
  const NeuralModel repeat = trainModel(tiny, all, 2, 0x1234'5678u);
  const bool deterministic =
      modelFingerprint(first) == modelFingerprint(repeat);
  writeCheckpoint(options.checkpoint, first);
  const NeuralModel restored = readCheckpoint(options.checkpoint);
  const bool checkpoint =
      modelFingerprint(first) == modelFingerprint(restored) &&
      fileBytes(options.checkpoint) <= kMaximumCheckpointBytes;
  const std::vector<AuditRoot> singleton{derived};
  const double swap_gap = neuralSwapGap(restored, singleton);
  const auto ordinary = forward(restored, derived, outcome.label.labeled_action).heads;
  derived.stored.label.game = 999;
  derived.stored.label.move_in_game = -777;
  const auto metadata = forward(restored, derived, outcome.label.labeled_action).heads;
  const bool metadata_blind = ordinary == metadata;
  const bool legal = isLegal(outcome.label.board, outcome.label.labeled_action);
  const bool resources =
      fileBytes(options.checkpoint) <= kMaximumCheckpointBytes &&
      first.input_weights.size() == static_cast<std::size_t>(kInputs * kHidden);
  const bool protocol =
      kInputs == 1'650 && kHidden == 12 && kHeads == 5 &&
      kEpochCandidates == std::array<int, 3>{{40, 80, 120}} &&
      prior::kTrainingRoots == 288 && prior::kHeldoutRoots == 144;
  const bool passed =
      inherited && ladder_exact && return_error <= 1.0e-8 &&
      deterministic && checkpoint && swap_gap == 0.0 && metadata_blind &&
      legal && resources && protocol;
  output << std::setprecision(12)
         << "D2_LONG_FEATURE_AUDIT_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"inheritedD4\":" << (inherited ? "true" : "false")
         << ",\"exactVerticalLadder\":"
         << (ladder_exact ? "true" : "false")
         << ",\"replayReturnError\":" << return_error
         << ",\"deterministicTraining\":"
         << (deterministic ? "true" : "false")
         << ",\"checkpoint\":" << (checkpoint ? "true" : "false")
         << ",\"reflectionSwapGap\":" << swap_gap
         << ",\"metadataBlind\":" << (metadata_blind ? "true" : "false")
         << ",\"resources\":" << (resources ? "true" : "false")
         << ",\"protocol\":" << (protocol ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::d2_long_outcome_feature_audit

#ifndef DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::d2_long_outcome_feature_audit::parseOptions(argc, argv, 2);
      return drop7::d2_long_outcome_feature_audit::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::d2_long_outcome_feature_audit::parseOptions(argc, argv, 2);
      return drop7::d2_long_outcome_feature_audit::run(options, std::cout);
    }
    std::cerr << "usage: drop7_d2_long_outcome_feature_audit "
                 "--self-test | --run "
                 "[--labels PATH --d4-source PATH --output PATH --derived PATH "
                 "--checkpoint PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_d2_long_outcome_feature_audit: "
              << error.what() << '\n';
    return 1;
  }
}
#endif
