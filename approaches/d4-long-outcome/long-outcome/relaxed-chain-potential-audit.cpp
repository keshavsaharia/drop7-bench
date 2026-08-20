#define DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY
#include "d2-long-outcome-feature-audit.cpp"
#undef DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY

#include <filesystem>
#include <fstream>
#include <sstream>

// Corpus-only audit of an interpretable, relaxed chain-potential feature.
// It replays only the already-fixed LONG first step, then enumerates chosen
// visible values/columns for at most two hypothetical moves.  Future tape
// values are never queried: newly revealed covers become occupied inert cells
// for the rest of the hypothetical line.
namespace drop7::relaxed_chain_potential_audit {

namespace legacy = drop7::d2_long_outcome_feature_audit;
namespace prior = drop7::d2_long_outcome_ranker;
namespace base = drop7::scaled_d4_distill;
using Clock = std::chrono::steady_clock;

constexpr std::uint8_t kUnknownInert = 10;
constexpr int kFeatures = 11;
constexpr int kFolds = 6;
constexpr int kHeldoutHalves = 2;
constexpr int kWorkers = 4;
constexpr std::array<double, 4> kRidgeCandidates{{
    1.0e-4,
    1.0e-3,
    1.0e-2,
    1.0e-1,
}};
constexpr double kClearWeight = 14.0;
constexpr double kRevealWeight = 28.0;
constexpr double kHeightRiskWeight = 0.25;
constexpr int kDangerHeight = 4;
constexpr double kRawFittingCorrelation = 0.10;
constexpr double kRawHeldoutCorrelation = 0.08;
constexpr double kTop1Improvement = 0.02;
constexpr double kTop2Improvement = 0.01;
constexpr double kPairwiseImprovement = 0.005;
constexpr double kRegretRatio = 0.95;
constexpr int kStableFoldsRequired = 5;
constexpr std::uint64_t kMaximumRssBytes = 256u * 1024u * 1024u;
constexpr std::string_view kInputSha256 =
    "621302a0cd8334fa56e5b77c191beb5529eda0e5413b8e7e20d524c852e7ea7a";

static_assert(kUnknownInert > kCracked);
static_assert(kFeatures == 11 && kFolds == 6);
static_assert(prior::kTrainingRoots == 288 && prior::kHeldoutRoots == 144);
static_assert(prior::kScenarios == 7 && prior::kHorizon == 25);
static_assert(base::kTrainingGames == 24 && base::kHeldoutGames == 12);

struct Options {
  std::string labels = "/tmp/drop7-d2-long-outcome-labels.jsonl";
  std::string output = "/tmp/drop7-relaxed-chain-potential-audit.json";
  std::string derived =
      "/tmp/drop7-relaxed-chain-potential-derived.jsonl";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string flag = argv[index];
    if (flag == "--labels") result.labels = argv[index + 1];
    else if (flag == "--output") result.output = argv[index + 1];
    else if (flag == "--derived") result.derived = argv[index + 1];
    else throw std::invalid_argument("unknown option " + flag);
  }
  return result;
}

int maximumHeight(const Board& board) {
  int maximum = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      if (board[indexOf(row, column)] != kEmpty) {
        height = kBoardSize - row;
        break;
      }
    }
    maximum = std::max(maximum, height);
  }
  return maximum;
}

struct CascadeStats {
  std::int64_t wave_score = 0;
  int clears = 0;
  int reveals = 0;
  int waves = 0;
};

void add(CascadeStats& target, const CascadeStats& source) {
  target.wave_score += source.wave_score;
  target.clears += source.clears;
  target.reveals += source.reveals;
  target.waves += source.waves;
}

// Equivalent numbered-pop/cover-hit mechanics, except a cover that would
// reveal is converted to kUnknownInert.  It remains occupied for line lengths
// but is neither numbered nor a cover, preventing clairvoyant future pops.
CascadeStats resolveConservative(Board& board, int starting_depth = 1) {
  CascadeStats result;
  for (int depth = starting_depth;; ++depth) {
    int popper_count = 0;
    const auto poppers = findPoppers(board, popper_count);
    if (popper_count == 0) return result;
    std::array<bool, kCellCount> popping{};
    Board cleared = board;
    for (int offset = 0; offset < popper_count; ++offset) {
      popping[poppers[offset]] = true;
      cleared[poppers[offset]] = kEmpty;
    }
    int reveal_count = 0;
    constexpr std::array<std::array<int, 2>, 4> directions{{
        {{-1, 0}}, {{1, 0}}, {{0, -1}}, {{0, 1}},
    }};
    for (int row = 0; row < kBoardSize; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        const int index = indexOf(row, column);
        const std::uint8_t cell = board[index];
        if (cell != kSolid && cell != kCracked) continue;
        int hits = 0;
        for (const auto& direction : directions) {
          const int neighbor_row = row + direction[0];
          const int neighbor_column = column + direction[1];
          if (inside(neighbor_row, neighbor_column) &&
              popping[indexOf(neighbor_row, neighbor_column)]) {
            ++hits;
          }
        }
        if (hits == 0) continue;
        const int needed = cell == kSolid ? 2 : 1;
        if (hits >= needed) {
          cleared[index] = kUnknownInert;
          ++reveal_count;
        } else {
          cleared[index] = kCracked;
        }
      }
    }
    const std::int64_t points = popper_count * scoreForWave(depth);
    result.wave_score += points;
    result.clears += popper_count;
    result.reveals += reveal_count;
    ++result.waves;
    board = applyGravity(cleared);
  }
}

struct HypotheticalState {
  Board board{};
  int moves_remaining = kMovesPerLevel;
  bool game_over = false;
};

struct HypotheticalMove {
  HypotheticalState state{};
  CascadeStats cascade{};
  bool played = false;
  bool quiet = false;
};

HypotheticalMove playHypothetical(const HypotheticalState& source,
                                  int column, std::uint8_t visible_disc) {
  HypotheticalMove result;
  if (source.game_over || visible_disc < 1 || visible_disc > kBoardSize ||
      !isLegal(source.board, column)) {
    return result;
  }
  result.played = true;
  result.state = source;
  placeDisc(result.state.board, column, visible_disc);
  result.cascade = resolveConservative(result.state.board);
  --result.state.moves_remaining;
  if (result.state.moves_remaining == 0) {
    Board raised{};
    if (!raiseCoveredRow(result.state.board, raised)) {
      result.state.game_over = true;
    } else {
      result.state.board = raised;
      result.state.moves_remaining = kMovesPerLevel;
      const CascadeStats rise = resolveConservative(
          result.state.board, result.cascade.waves + 1);
      add(result.cascade, rise);
    }
  }
  int legal_count = 0;
  legalColumns(result.state.board, legal_count);
  if (!result.state.game_over && legal_count == 0) {
    result.state.game_over = true;
  }
  result.quiet = !result.state.game_over && result.cascade.waves == 0;
  return result;
}

struct Path {
  CascadeStats cascade{};
  int drops = 0;
  int maximum_height = 0;
  bool survived = false;
  double raw_energy = 0.0;
  double normalized_energy = 0.0;
};

Path makePath(const CascadeStats& cascade, int drops, const Board& board,
              bool survived) {
  Path result;
  result.cascade = cascade;
  result.drops = drops;
  result.maximum_height = maximumHeight(board);
  result.survived = survived;
  result.raw_energy = static_cast<double>(cascade.wave_score) +
                      kClearWeight * cascade.clears +
                      kRevealWeight * cascade.reveals;
  const double height_risk =
      static_cast<double>(std::max(0, result.maximum_height - kDangerHeight));
  const double risk = 1.0 + kHeightRiskWeight * height_risk * height_risk +
                      (survived ? 0.0 : 4.0);
  result.normalized_energy =
      result.raw_energy / (static_cast<double>(drops) * risk);
  return result;
}

bool better(const Path& candidate, const Path& incumbent) {
  if (candidate.normalized_energy != incumbent.normalized_energy) {
    return candidate.normalized_energy > incumbent.normalized_energy;
  }
  if (candidate.cascade.wave_score != incumbent.cascade.wave_score) {
    return candidate.cascade.wave_score > incumbent.cascade.wave_score;
  }
  if (candidate.cascade.clears != incumbent.cascade.clears) {
    return candidate.cascade.clears > incumbent.cascade.clears;
  }
  return candidate.cascade.reveals > incumbent.cascade.reveals;
}

struct Potential {
  Path best_any{};
  Path best_one{};
  Path best_two{};
  Path best_build_release{};
  int first_choices = 0;
  int quiet_release_choices = 0;
  std::uint64_t hypothetical_moves = 0;
};

Potential relaxedPotential(const HypotheticalState& source) {
  Potential result;
  if (source.game_over) {
    result.best_any.maximum_height = kBoardSize + 1;
    result.best_any.drops = 3;
    return result;
  }
  bool have_one = false;
  bool have_two = false;
  bool have_build = false;
  for (int first_column = 0; first_column < kBoardSize; ++first_column) {
    if (!isLegal(source.board, first_column)) continue;
    for (int first_disc = 1; first_disc <= kBoardSize; ++first_disc) {
      ++result.first_choices;
      const HypotheticalMove first = playHypothetical(
          source, first_column, static_cast<std::uint8_t>(first_disc));
      ++result.hypothetical_moves;
      if (!first.played) throw std::runtime_error("legal first hypothesis failed");
      const Path one = makePath(first.cascade, 1, first.state.board,
                                !first.state.game_over);
      if (!have_one || better(one, result.best_one)) {
        result.best_one = one;
        have_one = true;
      }
      bool quiet_can_release = false;
      if (first.state.game_over) continue;
      for (int second_column = 0; second_column < kBoardSize;
           ++second_column) {
        if (!isLegal(first.state.board, second_column)) continue;
        for (int second_disc = 1; second_disc <= kBoardSize; ++second_disc) {
          const HypotheticalMove second = playHypothetical(
              first.state, second_column,
              static_cast<std::uint8_t>(second_disc));
          ++result.hypothetical_moves;
          if (!second.played) {
            throw std::runtime_error("legal second hypothesis failed");
          }
          CascadeStats combined = first.cascade;
          add(combined, second.cascade);
          const Path two = makePath(combined, 2, second.state.board,
                                    !second.state.game_over);
          if (!have_two || better(two, result.best_two)) {
            result.best_two = two;
            have_two = true;
          }
          if (first.quiet && second.cascade.waves > 0) {
            quiet_can_release = true;
            if (!have_build || better(two, result.best_build_release)) {
              result.best_build_release = two;
              have_build = true;
            }
          }
        }
      }
      result.quiet_release_choices += quiet_can_release;
    }
  }
  if (!have_one) return result;
  result.best_any = result.best_one;
  if (have_two && better(result.best_two, result.best_any)) {
    result.best_any = result.best_two;
  }
  return result;
}

using FeatureVector = std::array<double, kFeatures>;

FeatureVector potentialFeatures(const Potential& potential) {
  const Path& best = potential.best_any;
  const double risk =
      static_cast<double>(std::max(0, best.maximum_height - kDangerHeight));
  const double quiet_fraction =
      potential.first_choices > 0
          ? static_cast<double>(potential.quiet_release_choices) /
                potential.first_choices
          : 0.0;
  return {{
      best.normalized_energy,
      potential.best_one.normalized_energy,
      potential.best_two.normalized_energy,
      potential.best_build_release.normalized_energy,
      static_cast<double>(best.cascade.wave_score),
      static_cast<double>(best.cascade.clears),
      static_cast<double>(best.cascade.reveals),
      static_cast<double>(best.cascade.waves),
      static_cast<double>(best.drops),
      risk,
      quiet_fraction,
  }};
}

constexpr std::uint64_t kMaximumHypotheticalMovesPerSuccessor =
    kBoardSize * kBoardSize *
    (1u + static_cast<std::uint64_t>(kBoardSize * kBoardSize));
constexpr std::uint64_t kMaximumHypotheticalMovesPerRoot =
    kBoardSize * prior::kScenarios * kMaximumHypotheticalMovesPerSuccessor;
static_assert(kMaximumHypotheticalMovesPerSuccessor == 2'450);
static_assert(kMaximumHypotheticalMovesPerRoot == 120'050);

struct ActionPotential {
  FeatureVector features{};
  double relaxed_energy = 0.0;
  double build_release_energy = 0.0;
  std::uint64_t hypothetical_moves = 0;
  int terminal_first_steps = 0;
};

struct AuditRoot {
  legacy::StoredRoot stored{};
  base::PreparedRoot prepared{};
  std::array<ActionPotential, kBoardSize> actions{};
};

struct DeriveStats {
  std::uint64_t roots = 0;
  std::uint64_t first_step_transitions = 0;
  std::uint64_t hypothetical_moves = 0;
  std::uint64_t terminal_first_steps = 0;
  std::uint64_t peak_hypothetical_moves_per_root = 0;
  double maximum_stored_mean_error = 0.0;
  double wall_seconds = 0.0;

  void add(const DeriveStats& other) {
    roots += other.roots;
    first_step_transitions += other.first_step_transitions;
    hypothetical_moves += other.hypothetical_moves;
    terminal_first_steps += other.terminal_first_steps;
    peak_hypothetical_moves_per_root = std::max(
        peak_hypothetical_moves_per_root,
        other.peak_hypothetical_moves_per_root);
    maximum_stored_mean_error =
        std::max(maximum_stored_mean_error,
                 other.maximum_stored_mean_error);
  }
};

AuditRoot deriveRoot(const legacy::StoredRoot& stored, DeriveStats& stats) {
  AuditRoot result;
  result.stored = stored;
  result.prepared = base::prepare(stored.label);
  const prior::ObservableState root =
      prior::observable(base::publicState(stored.label));
  const std::uint32_t root_seed = prior::seed32(
      prior::publicHash(root) ^
      static_cast<std::uint64_t>(prior::kTapeSeedDomain));
  std::uint64_t root_hypothetical_moves = 0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!stored.label.legal[action]) continue;
    ActionPotential& action_result = result.actions[action];
    double stored_mean = 0.0;
    for (int scenario = 0; scenario < prior::kScenarios; ++scenario) {
      stored_mean +=
          stored.returns[action][scenario] / prior::kScenarios;
      MoveResult first_step;
      if (!prior::playSyntheticMove(root, action, root_seed, scenario, 0,
                                    first_step)) {
        throw std::runtime_error("preserved legal first-step replay failed");
      }
      ++stats.first_step_transitions;
      const HypotheticalState successor{
          first_step.state.board,
          first_step.state.moves_remaining,
          first_step.state.game_over,
      };
      const Potential potential = relaxedPotential(successor);
      const FeatureVector features = potentialFeatures(potential);
      for (int feature = 0; feature < kFeatures; ++feature) {
        action_result.features[feature] +=
            features[feature] / prior::kScenarios;
      }
      action_result.relaxed_energy +=
          potential.best_any.normalized_energy / prior::kScenarios;
      action_result.build_release_energy +=
          potential.best_build_release.normalized_energy /
          prior::kScenarios;
      action_result.hypothetical_moves += potential.hypothetical_moves;
      action_result.terminal_first_steps += first_step.state.game_over;
      root_hypothetical_moves += potential.hypothetical_moves;
    }
    stats.maximum_stored_mean_error = std::max(
        stats.maximum_stored_mean_error,
        std::abs(stored_mean - stored.label.q[action]));
    stats.hypothetical_moves += action_result.hypothetical_moves;
    stats.terminal_first_steps += action_result.terminal_first_steps;
  }
  stats.roots = 1;
  stats.peak_hypothetical_moves_per_root = root_hypothetical_moves;
  if (root_hypothetical_moves > kMaximumHypotheticalMovesPerRoot) {
    throw std::runtime_error("relaxed enumeration exceeded fixed root bound");
  }
  return result;
}

struct DerivedRange {
  std::vector<AuditRoot> roots;
  DeriveStats stats{};
};

DerivedRange deriveRange(const std::vector<legacy::StoredRoot>& stored,
                         std::string_view split) {
  const auto started = Clock::now();
  DerivedRange result;
  result.roots.resize(stored.size());
  std::vector<DeriveStats> local(stored.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kWorkers; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      while (true) {
        const std::size_t index = next.fetch_add(1);
        if (index >= stored.size()) return;
        result.roots[index] = deriveRoot(stored[index], local[index]);
        const std::size_t count = completed.fetch_add(1) + 1;
        if (count % prior::kRootsPerGame == 0 || count == stored.size()) {
          std::lock_guard<std::mutex> lock(prior::progress_mutex);
          std::cerr << "relaxed-potential " << split << ' ' << count << '/'
                    << stored.size() << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  for (const DeriveStats& item : local) result.stats.add(item);
  result.stats.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (result.stats.roots != stored.size() ||
      result.stats.first_step_transitions >
          stored.size() * kBoardSize * prior::kScenarios ||
      result.stats.maximum_stored_mean_error > 1.0e-8) {
    throw std::runtime_error("relaxed derivation provenance/resource failure");
  }
  return result;
}

struct PairMoments {
  std::uint64_t pairs = 0;
  double sum_x = 0.0;
  double sum_y = 0.0;
  double sum_xx = 0.0;
  double sum_yy = 0.0;
  double sum_xy = 0.0;
};

double correlation(const PairMoments& value) {
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
}

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
      ++result.pairs;
    }
  }
}

template <typename Score, typename Include>
Metrics evaluate(const std::vector<AuditRoot>& roots, Score score,
                 Include include) {
  Metrics result;
  for (const AuditRoot& root : roots) {
    if (!include(root)) continue;
    const auto prediction = score(root);
    base::observe(root.stored.label, prediction, result.ranking);
    observePairs(root, prediction, result.pairs);
  }
  if (result.ranking.roots == 0 || result.pairs.pairs == 0) {
    throw std::runtime_error("empty relaxed-potential metric range");
  }
  return result;
}

std::array<double, kBoardSize> d2Scores(const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.stored.label.legal[action]) {
      result[action] = root.prepared.d2[action];
    }
  }
  return result;
}

std::array<double, kBoardSize> relaxedScores(const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.stored.label.legal[action]) {
      result[action] = root.actions[action].relaxed_energy;
    }
  }
  return result;
}

std::array<double, kBoardSize> buildReleaseScores(const AuditRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.stored.label.legal[action]) {
      result[action] = root.actions[action].build_release_energy;
    }
  }
  return result;
}

struct LinearModel {
  FeatureVector mean{};
  FeatureVector scale{};
  FeatureVector weights{};
  double ridge = 0.0;
};

template <typename Include>
LinearModel fitModel(const std::vector<AuditRoot>& roots, Include include,
                     double ridge) {
  LinearModel result;
  result.ridge = ridge;
  std::uint64_t actions = 0;
  for (const AuditRoot& root : roots) {
    if (!include(root)) continue;
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.stored.label.legal[action]) continue;
      ++actions;
      for (int feature = 0; feature < kFeatures; ++feature) {
        result.mean[feature] += root.actions[action].features[feature];
      }
    }
  }
  if (actions == 0) throw std::runtime_error("empty ridge fitting actions");
  for (double& value : result.mean) value /= static_cast<double>(actions);
  for (const AuditRoot& root : roots) {
    if (!include(root)) continue;
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.stored.label.legal[action]) continue;
      for (int feature = 0; feature < kFeatures; ++feature) {
        const double centered =
            root.actions[action].features[feature] - result.mean[feature];
        result.scale[feature] += centered * centered;
      }
    }
  }
  for (double& value : result.scale) {
    value = std::sqrt(value / static_cast<double>(actions));
    if (value < 1.0e-9) value = 1.0;
  }

  std::array<std::array<double, kFeatures>, kFeatures> matrix{};
  FeatureVector vector{};
  std::uint64_t rows = 0;
  for (const AuditRoot& root : roots) {
    if (!include(root)) continue;
    for (int first = 0; first < kBoardSize; ++first) {
      if (!root.stored.label.legal[first]) continue;
      for (int second = first + 1; second < kBoardSize; ++second) {
        if (!root.stored.label.legal[second]) continue;
        FeatureVector x{};
        for (int feature = 0; feature < kFeatures; ++feature) {
          x[feature] =
              (root.actions[first].features[feature] -
               root.actions[second].features[feature]) /
              result.scale[feature];
        }
        const double target =
            (root.prepared.target[first] - root.prepared.target[second]) -
            (root.prepared.d2[first] - root.prepared.d2[second]);
        for (int row = 0; row < kFeatures; ++row) {
          vector[row] += x[row] * target;
          for (int column = 0; column < kFeatures; ++column) {
            matrix[row][column] += x[row] * x[column];
          }
        }
        ++rows;
      }
    }
  }
  if (rows == 0) throw std::runtime_error("empty ridge pair rows");
  for (int row = 0; row < kFeatures; ++row) {
    vector[row] /= static_cast<double>(rows);
    for (int column = 0; column < kFeatures; ++column) {
      matrix[row][column] /= static_cast<double>(rows);
    }
    matrix[row][row] += ridge;
  }

  // Deterministic partial-pivot Gaussian elimination on the small fixed
  // normal equation.  Ridge guarantees a nonsingular system.
  for (int pivot = 0; pivot < kFeatures; ++pivot) {
    int best = pivot;
    for (int row = pivot + 1; row < kFeatures; ++row) {
      if (std::abs(matrix[row][pivot]) >
          std::abs(matrix[best][pivot])) {
        best = row;
      }
    }
    if (std::abs(matrix[best][pivot]) < 1.0e-12) {
      throw std::runtime_error("ridge solve lost rank");
    }
    if (best != pivot) {
      std::swap(matrix[best], matrix[pivot]);
      std::swap(vector[best], vector[pivot]);
    }
    const double divisor = matrix[pivot][pivot];
    for (int column = pivot; column < kFeatures; ++column) {
      matrix[pivot][column] /= divisor;
    }
    vector[pivot] /= divisor;
    for (int row = 0; row < kFeatures; ++row) {
      if (row == pivot) continue;
      const double factor = matrix[row][pivot];
      for (int column = pivot; column < kFeatures; ++column) {
        matrix[row][column] -= factor * matrix[pivot][column];
      }
      vector[row] -= factor * vector[pivot];
    }
  }
  result.weights = vector;
  for (const double weight : result.weights) {
    if (!std::isfinite(weight)) throw std::runtime_error("nonfinite ridge weight");
  }
  return result;
}

std::array<double, kBoardSize> modelScores(const AuditRoot& root,
                                            const LinearModel& model) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.stored.label.legal[action]) continue;
    double residual = 0.0;
    for (int feature = 0; feature < kFeatures; ++feature) {
      residual += model.weights[feature] *
                  (root.actions[action].features[feature] -
                   model.mean[feature]) /
                  model.scale[feature];
    }
    result[action] = root.prepared.d2[action] + residual;
  }
  return result;
}

bool betterInner(const Metrics& first, const Metrics& second) {
  const double first_top1 = base::top1Rate(first.ranking);
  const double second_top1 = base::top1Rate(second.ranking);
  if (first_top1 != second_top1) return first_top1 > second_top1;
  const double first_top2 = base::top2Rate(first.ranking);
  const double second_top2 = base::top2Rate(second.ranking);
  if (first_top2 != second_top2) return first_top2 > second_top2;
  const double first_pair = base::pairwiseRate(first.ranking);
  const double second_pair = base::pairwiseRate(second.ranking);
  if (first_pair != second_pair) return first_pair > second_pair;
  return base::regret(first.ranking) < base::regret(second.ranking);
}

struct NestedAudit {
  Metrics fitting_d2{};
  Metrics fitting_relaxed{};
  Metrics fitting_build_release{};
  Metrics fitting_candidate_cv{};
  std::array<Metrics, kFolds> d2_folds{};
  std::array<Metrics, kFolds> candidate_folds{};
  std::array<int, kFolds> selected_ridge_indices{};
  int final_ridge_index = 0;
  LinearModel final_model{};
  Metrics heldout_d2{};
  Metrics heldout_relaxed{};
  Metrics heldout_build_release{};
  Metrics heldout_candidate{};
  std::array<Metrics, kHeldoutHalves> heldout_d2_halves{};
  std::array<Metrics, kHeldoutHalves> heldout_relaxed_halves{};
  std::array<Metrics, kHeldoutHalves> heldout_candidate_halves{};
};

NestedAudit audit(const std::vector<AuditRoot>& fitting,
                  const std::vector<AuditRoot>& heldout) {
  const auto all = [](const AuditRoot&) { return true; };
  NestedAudit result;
  result.fitting_d2 = evaluate(fitting, d2Scores, all);
  result.fitting_relaxed = evaluate(fitting, relaxedScores, all);
  result.fitting_build_release =
      evaluate(fitting, buildReleaseScores, all);
  for (int outer = 0; outer < kFolds; ++outer) {
    const int inner = (outer + 1) % kFolds;
    bool selected = false;
    Metrics selected_metrics;
    for (int candidate = 0;
         candidate < static_cast<int>(kRidgeCandidates.size());
         ++candidate) {
      const LinearModel model = fitModel(
          fitting,
          [outer, inner](const AuditRoot& root) {
            const int fold = root.stored.label.game % kFolds;
            return fold != outer && fold != inner;
          },
          kRidgeCandidates[static_cast<std::size_t>(candidate)]);
      const Metrics metrics = evaluate(
          fitting,
          [&model](const AuditRoot& root) { return modelScores(root, model); },
          [inner](const AuditRoot& root) {
            return root.stored.label.game % kFolds == inner;
          });
      if (!selected || betterInner(metrics, selected_metrics)) {
        selected = true;
        selected_metrics = metrics;
        result.selected_ridge_indices[outer] = candidate;
      }
    }
    const LinearModel model = fitModel(
        fitting,
        [outer](const AuditRoot& root) {
          return root.stored.label.game % kFolds != outer;
        },
        kRidgeCandidates[static_cast<std::size_t>(
            result.selected_ridge_indices[outer])]);
    result.d2_folds[outer] = evaluate(
        fitting, d2Scores, [outer](const AuditRoot& root) {
          return root.stored.label.game % kFolds == outer;
        });
    result.candidate_folds[outer] = evaluate(
        fitting,
        [&model](const AuditRoot& root) { return modelScores(root, model); },
        [outer](const AuditRoot& root) {
          return root.stored.label.game % kFolds == outer;
        });
    addMetrics(result.fitting_candidate_cv,
               result.candidate_folds[outer]);
  }
  std::array<int, kFolds> sorted = result.selected_ridge_indices;
  std::sort(sorted.begin(), sorted.end());
  result.final_ridge_index = sorted[(kFolds - 1) / 2];
  result.final_model = fitModel(
      fitting, all,
      kRidgeCandidates[static_cast<std::size_t>(result.final_ridge_index)]);
  result.heldout_d2 = evaluate(heldout, d2Scores, all);
  result.heldout_relaxed = evaluate(heldout, relaxedScores, all);
  result.heldout_build_release =
      evaluate(heldout, buildReleaseScores, all);
  result.heldout_candidate = evaluate(
      heldout,
      [&result](const AuditRoot& root) {
        return modelScores(root, result.final_model);
      },
      all);
  for (int half = 0; half < kHeldoutHalves; ++half) {
    const auto include = [half](const AuditRoot& root) {
      const int middle = base::kHeldoutGames / 2;
      return half == 0 ? root.stored.label.game < middle
                       : root.stored.label.game >= middle;
    };
    result.heldout_d2_halves[half] =
        evaluate(heldout, d2Scores, include);
    result.heldout_relaxed_halves[half] =
        evaluate(heldout, relaxedScores, include);
    result.heldout_candidate_halves[half] = evaluate(
        heldout,
        [&result](const AuditRoot& root) {
          return modelScores(root, result.final_model);
        },
        include);
  }
  return result;
}

struct AcceptanceGate {
  bool raw_fitting_correlation = false;
  bool raw_heldout_correlation = false;
  bool raw_both_halves_positive = false;
  bool cv_top1 = false;
  bool cv_top2 = false;
  bool cv_pairwise = false;
  bool cv_regret = false;
  int stable_folds = 0;
  bool heldout_top1 = false;
  bool heldout_top2 = false;
  bool heldout_pairwise = false;
  bool heldout_regret = false;
  bool heldout_both_halves = false;
  bool passed = false;
};

bool nonregressed(const Metrics& baseline, const Metrics& candidate) {
  return base::top1Rate(candidate.ranking) >=
             base::top1Rate(baseline.ranking) &&
         base::top2Rate(candidate.ranking) >=
             base::top2Rate(baseline.ranking) &&
         base::pairwiseRate(candidate.ranking) >=
             base::pairwiseRate(baseline.ranking) &&
         base::regret(candidate.ranking) <= base::regret(baseline.ranking);
}

AcceptanceGate acceptanceGate(const NestedAudit& value) {
  AcceptanceGate result;
  result.raw_fitting_correlation =
      correlation(value.fitting_relaxed.pairs) >= kRawFittingCorrelation;
  result.raw_heldout_correlation =
      correlation(value.heldout_relaxed.pairs) >= kRawHeldoutCorrelation;
  result.raw_both_halves_positive = true;
  for (int half = 0; half < kHeldoutHalves; ++half) {
    result.raw_both_halves_positive =
        result.raw_both_halves_positive &&
        correlation(value.heldout_relaxed_halves[half].pairs) > 0.0;
  }
  result.cv_top1 =
      base::top1Rate(value.fitting_candidate_cv.ranking) >=
      base::top1Rate(value.fitting_d2.ranking) + kTop1Improvement;
  result.cv_top2 =
      base::top2Rate(value.fitting_candidate_cv.ranking) >=
      base::top2Rate(value.fitting_d2.ranking) + kTop2Improvement;
  result.cv_pairwise =
      base::pairwiseRate(value.fitting_candidate_cv.ranking) >=
      base::pairwiseRate(value.fitting_d2.ranking) + kPairwiseImprovement;
  result.cv_regret =
      base::regret(value.fitting_candidate_cv.ranking) <=
      kRegretRatio * base::regret(value.fitting_d2.ranking);
  for (int fold = 0; fold < kFolds; ++fold) {
    result.stable_folds +=
        nonregressed(value.d2_folds[fold], value.candidate_folds[fold]);
  }
  result.heldout_top1 =
      base::top1Rate(value.heldout_candidate.ranking) >=
      base::top1Rate(value.heldout_d2.ranking) + kTop1Improvement;
  result.heldout_top2 =
      base::top2Rate(value.heldout_candidate.ranking) >=
      base::top2Rate(value.heldout_d2.ranking) + kTop2Improvement;
  result.heldout_pairwise =
      base::pairwiseRate(value.heldout_candidate.ranking) >=
      base::pairwiseRate(value.heldout_d2.ranking) + kPairwiseImprovement;
  result.heldout_regret =
      base::regret(value.heldout_candidate.ranking) <=
      kRegretRatio * base::regret(value.heldout_d2.ranking);
  result.heldout_both_halves = true;
  for (int half = 0; half < kHeldoutHalves; ++half) {
    result.heldout_both_halves =
        result.heldout_both_halves &&
        nonregressed(value.heldout_d2_halves[half],
                     value.heldout_candidate_halves[half]);
  }
  result.passed =
      result.raw_fitting_correlation && result.raw_heldout_correlation &&
      result.raw_both_halves_positive && result.cv_top1 && result.cv_top2 &&
      result.cv_pairwise && result.cv_regret &&
      result.stable_folds >= kStableFoldsRequired && result.heldout_top1 &&
      result.heldout_top2 && result.heldout_pairwise &&
      result.heldout_regret && result.heldout_both_halves;
  return result;
}

void writeMetrics(std::ostream& output, const Metrics& value) {
  output << std::setprecision(12) << "{\"roots\":"
         << value.ranking.roots << ",\"top1WithTies\":"
         << base::top1Rate(value.ranking)
         << ",\"top2ContainsOptimal\":"
         << base::top2Rate(value.ranking) << ",\"pairwiseAccuracy\":"
         << base::pairwiseRate(value.ranking)
         << ",\"normalizedRegret\":" << base::regret(value.ranking)
         << ",\"pairDifferencePearson\":"
         << correlation(value.pairs) << ",\"pairCount\":"
         << value.pairs.pairs << '}';
}

void writeGate(std::ostream& output, const AcceptanceGate& value) {
  output << "{\"passed\":" << (value.passed ? "true" : "false")
         << ",\"rawFittingCorrelation\":"
         << (value.raw_fitting_correlation ? "true" : "false")
         << ",\"rawHeldoutCorrelation\":"
         << (value.raw_heldout_correlation ? "true" : "false")
         << ",\"rawBothHeldoutHalvesPositive\":"
         << (value.raw_both_halves_positive ? "true" : "false")
         << ",\"cvTop1\":" << (value.cv_top1 ? "true" : "false")
         << ",\"cvTop2\":" << (value.cv_top2 ? "true" : "false")
         << ",\"cvPairwise\":"
         << (value.cv_pairwise ? "true" : "false")
         << ",\"cvRegret\":" << (value.cv_regret ? "true" : "false")
         << ",\"stableOuterFolds\":" << value.stable_folds
         << ",\"requiredStableOuterFolds\":" << kStableFoldsRequired
         << ",\"heldoutTop1\":"
         << (value.heldout_top1 ? "true" : "false")
         << ",\"heldoutTop2\":"
         << (value.heldout_top2 ? "true" : "false")
         << ",\"heldoutPairwise\":"
         << (value.heldout_pairwise ? "true" : "false")
         << ",\"heldoutRegret\":"
         << (value.heldout_regret ? "true" : "false")
         << ",\"heldoutBothHalves\":"
         << (value.heldout_both_halves ? "true" : "false") << '}';
}

void writeDeriveStats(std::ostream& output, const DeriveStats& value) {
  output << "{\"roots\":" << value.roots
         << ",\"firstStepTransitions\":" << value.first_step_transitions
         << ",\"hypotheticalMoves\":" << value.hypothetical_moves
         << ",\"terminalFirstSteps\":" << value.terminal_first_steps
         << ",\"peakHypotheticalMovesPerRoot\":"
         << value.peak_hypothetical_moves_per_root
         << ",\"maximumStoredMeanError\":"
         << value.maximum_stored_mean_error
         << ",\"wallSeconds\":" << value.wall_seconds << '}';
}

void writeDerivedRange(std::ostream& output,
                       const std::vector<AuditRoot>& roots,
                       std::string_view split) {
  for (const AuditRoot& root : roots) {
    output << std::setprecision(10) << "{\"split\":\"" << split
           << "\",\"game\":" << root.stored.label.game
           << ",\"moveInSourceGame\":"
           << root.stored.label.move_in_game << ",\"actions\":[";
    for (int action = 0; action < kBoardSize; ++action) {
      if (action != 0) output << ',';
      if (!root.stored.label.legal[action]) {
        output << "null";
        continue;
      }
      const ActionPotential& value = root.actions[action];
      output << "{\"action\":" << action << ",\"features\":[";
      for (int feature = 0; feature < kFeatures; ++feature) {
        if (feature != 0) output << ',';
        output << value.features[feature];
      }
      output << "],\"relaxedEnergy\":" << value.relaxed_energy
             << ",\"buildReleaseEnergy\":"
             << value.build_release_energy
             << ",\"hypotheticalMoves\":" << value.hypothetical_moves
             << ",\"terminalFirstSteps\":"
             << value.terminal_first_steps << '}';
    }
    output << "]}\n";
  }
}

void writeDerived(const Options& options,
                  const std::vector<AuditRoot>& fitting,
                  const std::vector<AuditRoot>& heldout) {
  std::ofstream output(options.derived);
  if (!output) throw std::runtime_error("could not write relaxed derived file");
  output << "{\"type\":\"metadata\",\"format\":\"drop7-relaxed-chain-potential-v1\""
         << ",\"inputSha256\":\"" << kInputSha256 << "\""
         << ",\"inputRoots\":432,\"newRoots\":0,\"newGameSeeds\":0"
         << ",\"firstStepOnly\":true,\"futureTapeReads\":0"
         << ",\"conservativeUnknownRevealToken\":"
         << static_cast<int>(kUnknownInert) << "}\n";
  writeDerivedRange(output, fitting, "fitting");
  writeDerivedRange(output, heldout, "heldout-burned-development");
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(const Options& options, std::ostream& output) {
  std::error_code error;
  constexpr std::uintmax_t kInputBytes = 488'297;
  expect(std::filesystem::file_size(options.labels, error) == kInputBytes &&
             !error,
         "preserved corpus byte count");
  const legacy::StoredCorpus corpus = legacy::loadCorpus(options.labels);
  expect(corpus.fitting.size() == prior::kTrainingRoots &&
             corpus.heldout.size() == prior::kHeldoutRoots,
         "preserved corpus counts");

  Board reveal_fixture{};
  reveal_fixture[indexOf(kBoardSize - 1, 0)] = 1;
  reveal_fixture[indexOf(kBoardSize - 1, 1)] = kCracked;
  const CascadeStats conservative = resolveConservative(reveal_fixture);
  int remaining_poppers = 0;
  findPoppers(reveal_fixture, remaining_poppers);
  const bool conservative_reveal =
      conservative.clears == 1 && conservative.reveals == 1 &&
      conservative.waves == 1 &&
      reveal_fixture[indexOf(kBoardSize - 1, 1)] == kUnknownInert &&
      remaining_poppers == 0 && !isNumbered(kUnknownInert);
  expect(conservative_reveal, "conservative reveal semantics");

  HypotheticalState empty;
  empty.moves_remaining = 5;
  const Potential first = relaxedPotential(empty);
  const Potential repeat = relaxedPotential(empty);
  expect(first.best_build_release.normalized_energy > 0.0 &&
             first.quiet_release_choices > 0 &&
             first.hypothetical_moves == repeat.hypothetical_moves &&
             potentialFeatures(first) == potentialFeatures(repeat) &&
             first.hypothetical_moves <=
                 kMaximumHypotheticalMovesPerSuccessor,
         "build-release/determinism/resource fixture");

  HypotheticalState asymmetric;
  asymmetric.moves_remaining = 3;
  asymmetric.board[indexOf(6, 0)] = 2;
  asymmetric.board[indexOf(6, 1)] = 3;
  asymmetric.board[indexOf(6, 4)] = kCracked;
  asymmetric.board[indexOf(5, 4)] = 6;
  HypotheticalState reflected = asymmetric;
  reflected.board = cfpi::detail::mirrorBoard(asymmetric.board);
  const FeatureVector ordinary_features =
      potentialFeatures(relaxedPotential(asymmetric));
  const FeatureVector reflected_features =
      potentialFeatures(relaxedPotential(reflected));
  expect(ordinary_features == reflected_features,
         "relaxed potential reflection");

  CascadeStats fixed_stats;
  fixed_stats.wave_score = 100;
  fixed_stats.clears = 3;
  fixed_stats.reveals = 1;
  Board low{};
  for (int row = 3; row < kBoardSize; ++row) low[indexOf(row, 0)] = kSolid;
  Board high = low;
  high[indexOf(1, 0)] = kSolid;
  high[indexOf(2, 0)] = kSolid;
  const Path one_drop = makePath(fixed_stats, 1, low, true);
  const Path two_drop = makePath(fixed_stats, 2, low, true);
  const Path high_risk = makePath(fixed_stats, 1, high, true);
  expect(one_drop.normalized_energy > two_drop.normalized_energy &&
             one_drop.normalized_energy > high_risk.normalized_energy,
         "activation/height normalization");

  std::vector<AuditRoot> synthetic(8);
  for (int game = 0; game < static_cast<int>(synthetic.size()); ++game) {
    AuditRoot& root = synthetic[static_cast<std::size_t>(game)];
    root.stored.label.game = game;
    root.stored.label.legal[0] = true;
    root.stored.label.legal[1] = true;
    root.stored.label.labeled_action = 0;
    root.prepared.target[0] = 1.0;
    root.prepared.target[1] = 0.0;
    root.prepared.d2[0] = 0.0;
    root.prepared.d2[1] = 0.0;
    root.actions[0].features[0] = 1.0 + 0.01 * game;
    root.actions[1].features[0] = 0.0;
  }
  const auto all = [](const AuditRoot&) { return true; };
  const LinearModel model = fitModel(synthetic, all, 1.0e-2);
  const auto synthetic_scores = modelScores(synthetic.front(), model);
  expect(synthetic_scores[0] > synthetic_scores[1],
         "interpretable ridge residual wiring");

  DeriveStats one_root_stats;
  const AuditRoot derived =
      deriveRoot(corpus.fitting.front(), one_root_stats);
  int legal_count = 0;
  for (const bool legal : derived.stored.label.legal) legal_count += legal;
  expect(one_root_stats.roots == 1 &&
             one_root_stats.first_step_transitions ==
                 static_cast<std::uint64_t>(legal_count * prior::kScenarios) &&
             one_root_stats.hypothetical_moves <=
                 kMaximumHypotheticalMovesPerRoot &&
             one_root_stats.maximum_stored_mean_error <= 1.0e-8,
         "single preserved-root derivation");

  const bool protocol =
      kFeatures == 11 && kFolds == 6 && kWorkers == 4 &&
      kRidgeCandidates ==
          std::array<double, 4>{{1.0e-4, 1.0e-3, 1.0e-2, 1.0e-1}} &&
      kRawFittingCorrelation == 0.10 &&
      kRawHeldoutCorrelation == 0.08 && kTop1Improvement == 0.02 &&
      kTop2Improvement == 0.01 && kPairwiseImprovement == 0.005 &&
      kRegretRatio == 0.95 && kStableFoldsRequired == 5;
  const bool passed = conservative_reveal && protocol &&
                      prior::peakRssBytes() <= kMaximumRssBytes;
  output << std::setprecision(12)
         << "RELAXED_CHAIN_POTENTIAL_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"preservedRoots\":"
         << corpus.fitting.size() + corpus.heldout.size()
         << ",\"noGameplaySeedApi\":true"
         << ",\"firstStepOnly\":true"
         << ",\"conservativeUnknownReveal\":"
         << (conservative_reveal ? "true" : "false")
         << ",\"buildThenRelease\":true"
         << ",\"activationHeightNormalization\":true"
         << ",\"reflectionExact\":true"
         << ",\"deterministic\":true"
         << ",\"ridgeResidual\":true"
         << ",\"resourceBounds\":true"
         << ",\"protocolFrozen\":" << (protocol ? "true" : "false")
         << ",\"peakRssBytes\":" << prior::peakRssBytes() << "}\n";
  return passed;
}

void writeArtifact(const Options& options, const DerivedRange& fitting,
                   const DerivedRange& heldout, const NestedAudit& result,
                   const AcceptanceGate& gate, double total_wall) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write relaxed audit artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"relaxed-chain-potential-audit\",\n"
         << "  \"qualityOnly\":true,\n"
         << "  \"newCorpusRoots\":0,\n"
         << "  \"newGameplaySeeds\":[],\n"
         << "  \"input\":{\"path\":\"" << options.labels
         << "\",\"sha256\":\"" << kInputSha256
         << "\",\"fittingRoots\":288,\"oldHeldoutRoots\":144"
         << ",\"oldHeldoutStatus\":\"already-burned-development-evidence\"},\n"
         << "  \"frozenFeature\":{\"firstStepCrnReplayScenarios\":7"
         << ",\"hypotheticalVisibleDrops\":[1,2]"
         << ",\"chosenDiscValues\":[1,2,3,4,5,6,7]"
         << ",\"allLegalColumns\":true"
         << ",\"futureTapeReads\":0"
         << ",\"newlyRevealedCover\":\"occupied-inert-unknown\""
         << ",\"waveScoreExact\":true"
         << ",\"clearWeight\":" << kClearWeight
         << ",\"revealWeight\":" << kRevealWeight
         << ",\"activationDivisor\":\"drop-count\""
         << ",\"dangerHeight\":" << kDangerHeight
         << ",\"heightRiskWeight\":" << kHeightRiskWeight
         << ",\"buildThenReleaseRequiresQuietFirst\":true"
         << ",\"featureCount\":" << kFeatures << "},\n"
         << "  \"nestedCv\":{\"outerWholeGameFolds\":" << kFolds
         << ",\"innerFold\":\"next-modulo-six\""
         << ",\"ridgeCandidates\":[0.0001,0.001,0.01,0.1]"
         << ",\"innerSelectionOrder\":[\"top1\",\"top2\",\"pairwise\",\"regret\"]"
         << ",\"finalRidgeRule\":\"lower-median-selected-index\"},\n"
         << "  \"frozenGateThresholds\":{\"rawFittingPairPearson\":"
         << kRawFittingCorrelation
         << ",\"rawHeldoutPairPearson\":" << kRawHeldoutCorrelation
         << ",\"rawEachHeldoutHalfPositive\":true"
         << ",\"top1AbsoluteImprovement\":" << kTop1Improvement
         << ",\"top2AbsoluteImprovement\":" << kTop2Improvement
         << ",\"pairwiseAbsoluteImprovement\":"
         << kPairwiseImprovement
         << ",\"maximumRegretRatio\":" << kRegretRatio
         << ",\"stableOuterFoldsRequired\":"
         << kStableFoldsRequired
         << ",\"bothHeldoutHalvesMustNonregressAllMetrics\":true},\n"
         << "  \"derivation\":{\"fitting\":";
  writeDeriveStats(output, fitting.stats);
  output << ",\"heldout\":";
  writeDeriveStats(output, heldout.stats);
  output << ",\"derivedPath\":\"" << options.derived
         << "\",\"derivedBytes\":"
         << std::filesystem::file_size(options.derived) << "},\n"
         << "  \"fitting\":{\"exactD2\":";
  writeMetrics(output, result.fitting_d2);
  output << ",\"rawRelaxedPotential\":";
  writeMetrics(output, result.fitting_relaxed);
  output << ",\"rawBuildRelease\":";
  writeMetrics(output, result.fitting_build_release);
  output << ",\"nestedCvD2PlusPotential\":";
  writeMetrics(output, result.fitting_candidate_cv);
  output << ",\"folds\":[";
  for (int fold = 0; fold < kFolds; ++fold) {
    if (fold != 0) output << ',';
    output << "{\"fold\":" << fold << ",\"ridgeIndex\":"
           << result.selected_ridge_indices[fold] << ",\"exactD2\":";
    writeMetrics(output, result.d2_folds[fold]);
    output << ",\"candidate\":";
    writeMetrics(output, result.candidate_folds[fold]);
    output << '}';
  }
  output << "]},\n  \"finalModel\":{\"ridgeIndex\":"
         << result.final_ridge_index << ",\"ridge\":"
         << result.final_model.ridge << ",\"mean\":[";
  for (int feature = 0; feature < kFeatures; ++feature) {
    if (feature != 0) output << ',';
    output << result.final_model.mean[feature];
  }
  output << "],\"scale\":[";
  for (int feature = 0; feature < kFeatures; ++feature) {
    if (feature != 0) output << ',';
    output << result.final_model.scale[feature];
  }
  output << "],\"weights\":[";
  for (int feature = 0; feature < kFeatures; ++feature) {
    if (feature != 0) output << ',';
    output << result.final_model.weights[feature];
  }
  output << "]},\n  \"oldHeldout\":{\"exactD2\":";
  writeMetrics(output, result.heldout_d2);
  output << ",\"rawRelaxedPotential\":";
  writeMetrics(output, result.heldout_relaxed);
  output << ",\"rawBuildRelease\":";
  writeMetrics(output, result.heldout_build_release);
  output << ",\"d2PlusPotential\":";
  writeMetrics(output, result.heldout_candidate);
  output << ",\"halves\":[";
  for (int half = 0; half < kHeldoutHalves; ++half) {
    if (half != 0) output << ',';
    output << "{\"half\":" << half << ",\"exactD2\":";
    writeMetrics(output, result.heldout_d2_halves[half]);
    output << ",\"rawRelaxedPotential\":";
    writeMetrics(output, result.heldout_relaxed_halves[half]);
    output << ",\"candidate\":";
    writeMetrics(output, result.heldout_candidate_halves[half]);
    output << '}';
  }
  output << "]},\n  \"acceptanceGate\":";
  writeGate(output, gate);
  output << ",\n  \"conclusion\":\""
         << (gate.passed ? "passed-proposal-only" : "rejected") << "\""
         << ",\n  \"transferProposal\":";
  if (gate.passed) {
    output << "{\"status\":\"preregister-before-any-new-data\""
           << ",\"proposal\":\"freeze these coefficients and test once on a separately reserved whole-game screen\"}";
  } else {
    output << "null";
  }
  output << ",\n  \"totalWallSeconds\":" << total_wall
         << ",\n  \"peakRssBytes\":" << prior::peakRssBytes() << "\n}\n";
}

int run(const Options& options, std::ostream& report) {
  const auto started = Clock::now();
  if (!selfTest(options, report)) return EXIT_FAILURE;
  const legacy::StoredCorpus corpus = legacy::loadCorpus(options.labels);
  DerivedRange fitting = deriveRange(corpus.fitting, "fitting");
  DerivedRange heldout = deriveRange(corpus.heldout, "old-heldout");
  writeDerived(options, fitting.roots, heldout.roots);
  const NestedAudit result = audit(fitting.roots, heldout.roots);
  const AcceptanceGate gate = acceptanceGate(result);
  const double total_wall =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (prior::peakRssBytes() > kMaximumRssBytes) {
    throw std::runtime_error("relaxed audit exceeded RSS cap");
  }
  writeArtifact(options, fitting, heldout, result, gate, total_wall);
  report << std::fixed << std::setprecision(6)
         << "RELAXED_CHAIN_POTENTIAL_RESULT {\"passed\":"
         << (gate.passed ? "true" : "false")
         << ",\"fittingRawPairPearson\":"
         << correlation(result.fitting_relaxed.pairs)
         << ",\"heldoutRawPairPearson\":"
         << correlation(result.heldout_relaxed.pairs)
         << ",\"cvTop1\":"
         << base::top1Rate(result.fitting_candidate_cv.ranking)
         << ",\"heldoutTop1\":"
         << base::top1Rate(result.heldout_candidate.ranking)
         << ",\"stableFolds\":" << gate.stable_folds
         << ",\"wallSeconds\":" << total_wall
         << ",\"peakRssBytes\":" << prior::peakRssBytes()
         << ",\"output\":\"" << options.output << "\"}\n";
  return EXIT_SUCCESS;
}

}  // namespace drop7::relaxed_chain_potential_audit

#ifndef DROP7_RELAXED_CHAIN_POTENTIAL_AUDIT_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const auto options =
        drop7::relaxed_chain_potential_audit::parseOptions(argc, argv, 2);
    const std::string mode = argv[1];
    if (mode == "--self-test") {
      return drop7::relaxed_chain_potential_audit::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--run") {
      return drop7::relaxed_chain_potential_audit::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_relaxed_chain_potential_audit --self-test|--run "
        "[--labels PATH] [--output PATH] [--derived PATH]");
  } catch (const std::exception& error) {
    std::cerr << "relaxed chain-potential audit error: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
#endif
