// Learned-leaf variant of the parameterized fair expectimax.
//
// The only change to the search is the leaf:
//
//     leafValue = (1 - w) * frozen::fairLeaf(state) + w * scale * learnedValue(state)
//
// with w = 0 reproducing the frozen reference exactly (--parity gate).  Depth,
// chance stratification, canonicalization, cache keying, column order, work
// accounting and terminal utility all come from the unmodified frozen code by
// way of the same driver used in
// approaches/lifetime-objective/risk-calibration/search.cpp, which is itself
// proved decision-identical to the reference at its defaults.
//
// WHY THIS LEAF VALUE
//
// Hardcore score is 94.29% flat 17,000-point row-rise bonus and correlates with
// lifetime at r = 0.9995, and the steady-state rate is ~3,400 points per move
// (docs/exploratory/finding-01-score-is-survival.md).  Expected remaining
// lifetime multiplied by 3,400 is therefore *already in score units*, which is
// the same unit the search's immediate-score term carries at weight 1.0.  The
// blend is consequently a mix of two estimates of the same quantity rather than
// a mix of a score and an arbitrary heuristic index.
//
//   --leaf-value lifetime  : expm1(lifetimeHead) * scale, scale defaults to 3400
//   --leaf-value hazard    : sum_k sigmoid(hazard_k) * scale, scale defaults to
//                            17000, i.e. the expected number of further row
//                            rises survived within the model's 12-rise horizon,
//                            priced at one level bonus each.
//
// The lifetime form is expected to be the stronger of the two: it is unbounded
// where the hazard sum saturates at 12 rises (60 moves), and finding-01 puts the
// lifetime a million-point mean needs at ~294 moves, far outside that horizon.

#include "fair-only-depth4-noentry.cpp"

#include "../../../approaches/lifetime-objective/common/harness.hpp"
#include "leafnet.hpp"

#include <atomic>
#include <exception>
#include <memory>
#include <limits>
#include <list>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace drop7::lifetime::learned {

namespace ref = drop7::fair_only_depth4;
namespace frozen = drop7::fair_only_horizon;

// ---------------------------------------------------------------------------
// Seed-lease guard.  Nothing may be played outside the lease assigned to this
// work, with the single exception of the fixed paired evaluation cohort every
// other arm in this session used, which is named explicitly so it cannot be
// widened by accident.
// ---------------------------------------------------------------------------
constexpr std::uint32_t kLeaseFirst = 0xa524'0000u;
constexpr std::uint32_t kLeaseLast = 0xa524'7fffu;
constexpr std::uint32_t kEvalFirst = 0xa51d'1000u;
constexpr std::uint32_t kEvalLast = 0xa51d'103fu;

void assertLease(std::uint32_t first, int games) {
  const std::uint64_t last = static_cast<std::uint64_t>(first) +
                             static_cast<std::uint64_t>(games) - 1;
  const bool inLease = first >= kLeaseFirst && last <= kLeaseLast;
  const bool inEval = first >= kEvalFirst && last <= kEvalLast;
  if (!inLease && !inEval) {
    std::ostringstream message;
    message << "seed range 0x" << std::hex << first << "-0x" << last << std::dec
            << " is outside SEEDLEASE-A52-LEAF (0xa5240000-0xa5247fff) and the"
               " declared fixed evaluation cohort (0xa51d1000-0xa51d103f)";
    throw std::runtime_error(message.str());
  }
}

enum class LeafValueKind { kLifetime, kHazard };

struct SearchParameters {
  int depth = 4;
  int chanceSamples = frozen::kChanceSamples;
  double terminalUtility = frozen::kTerminalUtility;
  std::uint64_t maximumWork = 3'200'000;
  std::size_t maximumCacheEntries = 60'000;
  double blendWeight = 0.0;
  double scale = 3400.0;
  LeafValueKind leafValue = LeafValueKind::kLifetime;
};

class WorkLimitReached : public std::exception {};

struct CacheEntry {
  double value = 0.0;
  std::list<std::string>::iterator order;
};

struct SearchContext {
  std::unordered_map<std::string, CacheEntry> cache;
  std::list<std::string> order;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cacheHits = 0;
};

// Model calls and decisions are counted globally: the harness owns the worker
// pool and joins it before returning, so per-decider counters are unreachable
// afterwards.  Both counters are exact, not sampled.
inline std::atomic<std::uint64_t> gNetEvaluations{0};
inline std::atomic<std::uint64_t> gDecisions{0};
inline std::atomic<std::uint64_t> gLeafEvaluations{0};

class LearnedLeafSearch {
 public:
  LearnedLeafSearch(SearchParameters parameters, const drop7::leaf::LeafNet* net)
      : parameters_(parameters), net_(net) {
    if (net_ != nullptr) {
      scratch_.assign(static_cast<std::size_t>(net_->hidden() + net_->mid()), 0.0f);
    }
  }

  int chooseAction(const State& source, std::uint64_t& work) {
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = cfpi::detail::canonicalState(source, mirrored);
    SearchContext context;
    int action = -1;
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth, context);
        if (candidate < 0) break;
        action = candidate;
      } catch (const WorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    work += context.work;
    gDecisions.fetch_add(1, std::memory_order_relaxed);
    return mirrored && action >= 0 ? kBoardSize - 1 - action : action;
  }

 private:
  void checkBudget(const SearchContext& context) const {
    if (context.work >= parameters_.maximumWork) throw WorkLimitReached{};
  }

  void cacheValue(SearchContext& context, std::string key, double value) const {
    const auto prior = context.cache.find(key);
    if (prior != context.cache.end()) {
      context.order.erase(prior->second.order);
      context.cache.erase(prior);
    }
    while (context.cache.size() >= parameters_.maximumCacheEntries) {
      const std::string& oldest = context.order.front();
      context.cache.erase(oldest);
      context.order.pop_front();
    }
    context.order.push_back(std::move(key));
    const auto order = std::prev(context.order.end());
    context.cache.emplace(*order, CacheEntry{value, order});
  }

  double evaluateAction(const State& state, int column, int depth,
                        SearchContext& context) {
    const std::uint32_t stateSeed =
        cfpi::detail::scenarioSeedForState(state, frozen::kPolicySeed, depth);
    double value = 0.0;
    for (int sample = 0; sample < parameters_.chanceSamples; ++sample) {
      checkBudget(context);
      cfpi::detail::StratifiedRandom random{stateSeed, sample,
                                            parameters_.chanceSamples, 0};
      MoveResult move;
      const bool played =
          cfpi::detail::playMoveSampled(state, column, random, move);
      ++context.work;
      if (!played) {
        value += parameters_.terminalUtility;
        continue;
      }
      const double scoreDelta = static_cast<double>(move.score_delta);
      if (move.state.game_over) {
        value += scoreDelta + parameters_.terminalUtility;
        continue;
      }
      move.state.score = 0;
      move.state.next_disc = cfpi::detail::sampledNextDisc(
          stateSeed, sample, parameters_.chanceSamples);
      bool ignored = false;
      const State next = cfpi::detail::canonicalState(move.state, ignored);
      value += scoreDelta + bestFutureValue(next, depth - 1, context);
    }
    return value / parameters_.chanceSamples;
  }

  double learnedValue(const State& state) {
    drop7::leaf::LeafOutput out;
    net_->evaluate(state.board.data(), state.next_disc, state.moves_remaining,
                   out, scratch_.data());
    localNetEvaluations_ += 1;
    if (parameters_.leafValue == LeafValueKind::kLifetime) {
      return static_cast<double>(std::expm1(out.lifetimeLog));
    }
    double expectedRises = 0.0;
    for (int k = 0; k < net_->hazardHorizon(); ++k) {
      expectedRises += 1.0 / (1.0 + std::exp(-static_cast<double>(out.hazardLogits[k])));
    }
    return expectedRises;
  }

  double evaluateLeaf(const State& state, SearchContext& context) {
    checkBudget(context);
    ++context.work;
    localLeafEvaluations_ += 1;
    const double fair = frozen::fairLeaf(state);
    // w == 0 short-circuits to the frozen leaf bit-for-bit and never touches
    // the model, so the reference arms of the 2x2 cost exactly what the
    // reference costs.  This is the correctness anchor for --parity.
    const double value =
        parameters_.blendWeight == 0.0
            ? fair
            : (1.0 - parameters_.blendWeight) * fair +
                  parameters_.blendWeight * parameters_.scale * learnedValue(state);
    if (!std::isfinite(value)) throw std::runtime_error("leaf returned a non-finite value");
    return value;
  }

  double bestFutureValue(const State& state, int depth, SearchContext& context) {
    ++context.nodes;
    checkBudget(context);
    if (state.game_over) return parameters_.terminalUtility;
    if (depth == 0) return evaluateLeaf(state, context);
    const std::string key = cfpi::detail::dynamicStateKey(state, depth);
    const auto cached = context.cache.find(key);
    if (cached != context.cache.end()) {
      ++context.cacheHits;
      const double value = cached->second.value;
      context.order.splice(context.order.end(), context.order, cached->second.order);
      return value;
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(state.board, column)) continue;
      best = std::max(best, evaluateAction(state, column, depth, context));
    }
    if (!std::isfinite(best)) best = parameters_.terminalUtility;
    cacheValue(context, key, best);
    return best;
  }

  int rootDecision(const State& canonical, int depth, SearchContext& context) {
    int action = -1;
    double bestValue = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (!isLegal(canonical.board, column)) continue;
      const double value = evaluateAction(canonical, column, depth, context);
      if (value > bestValue) {
        bestValue = value;
        action = column;
      }
    }
    return action;
  }

  SearchParameters parameters_;
  const drop7::leaf::LeafNet* net_ = nullptr;
  std::vector<float> scratch_;
  std::uint64_t localNetEvaluations_ = 0;
  std::uint64_t localLeafEvaluations_ = 0;

 public:
  // Flushed once per worker thread when the decider is destroyed, so the hot
  // path stays free of atomics.
  ~LearnedLeafSearch() {
    gNetEvaluations.fetch_add(localNetEvaluations_, std::memory_order_relaxed);
    gLeafEvaluations.fetch_add(localLeafEvaluations_, std::memory_order_relaxed);
  }
  LearnedLeafSearch(const LearnedLeafSearch& other)
      : parameters_(other.parameters_), net_(other.net_), scratch_(other.scratch_) {}
  LearnedLeafSearch& operator=(const LearnedLeafSearch&) = delete;
};

// CHECK-tier gate: with w = 0 this driver must select exactly the same column
// as the unmodified reference on every move of every probe game.
bool parityCheck(const SearchParameters& parameters, std::uint32_t seedStart,
                 int games, int maximumMoves, const drop7::leaf::LeafNet* net,
                 std::ostream& out) {
  LearnedLeafSearch mine{parameters, net};
  std::uint64_t mismatches = 0;
  std::uint64_t comparedMoves = 0;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed = seedStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximumMoves) {
      const ref::SearchDecision reference = ref::chooseDepth4Action(state);
      std::uint64_t work = 0;
      const int candidate = mine.chooseAction(state, work);
      ++comparedMoves;
      if (candidate != reference.action) {
        ++mismatches;
        out << "  mismatch seed 0x" << std::hex << seed << std::dec << " move "
            << state.moves_played << ": reference " << reference.action
            << " learned-leaf " << candidate << '\n';
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, reference.action, move)) break;
    }
  }
  out << "parity: " << comparedMoves << " moves compared, " << mismatches
      << " mismatches (blendWeight " << parameters.blendWeight << ")\n";
  return mismatches == 0;
}

// Diagnostic: the two leaf terms are only commensurable if their scales are
// known, so measure them on real corpus states before choosing w.
void leafStats(const drop7::leaf::LeafNet& net, const SearchParameters& parameters,
               const std::string& statesPath, std::size_t count, std::ostream& out) {
  constexpr std::size_t kRecordBytes = 72;
  std::FILE* file = std::fopen(statesPath.c_str(), "rb");
  if (file == nullptr) throw std::runtime_error("cannot open " + statesPath);
  std::fseek(file, 0, SEEK_END);
  const std::size_t total = static_cast<std::size_t>(std::ftell(file)) / kRecordBytes;
  const std::size_t stride = std::max<std::size_t>(1, total / count);
  std::vector<float> scratch(static_cast<std::size_t>(net.hidden() + net.mid()));
  std::vector<double> fairValues, learnedValues;
  std::vector<std::uint8_t> buffer(kRecordBytes);
  drop7::leaf::LeafOutput leafOut;
  for (std::size_t index = 0; index < count; ++index) {
    std::fseek(file, static_cast<long>(index * stride * kRecordBytes), SEEK_SET);
    if (std::fread(buffer.data(), 1, kRecordBytes, file) != kRecordBytes) break;
    State state;
    for (int cell = 0; cell < kCellCount; ++cell) state.board[cell] = buffer[cell];
    state.next_disc = buffer[kCellCount];
    state.moves_remaining = buffer[kCellCount + 1];
    fairValues.push_back(frozen::fairLeaf(state));
    net.evaluate(state.board.data(), state.next_disc, state.moves_remaining,
                 leafOut, scratch.data());
    const double raw = parameters.leafValue == LeafValueKind::kLifetime
                           ? std::expm1(leafOut.lifetimeLog)
                           : [&]() {
                               double sum = 0.0;
                               for (int k = 0; k < net.hazardHorizon(); ++k) {
                                 sum += 1.0 / (1.0 + std::exp(-leafOut.hazardLogits[k]));
                               }
                               return sum;
                             }();
    learnedValues.push_back(raw * parameters.scale);
  }
  std::fclose(file);
  const auto moments = [](const std::vector<double>& values) {
    double mean = 0.0;
    for (double v : values) mean += v;
    mean /= static_cast<double>(values.size());
    double variance = 0.0;
    for (double v : values) variance += (v - mean) * (v - mean);
    variance /= static_cast<double>(values.size() - 1);
    return std::pair<double, double>{mean, std::sqrt(variance)};
  };
  const auto [fairMean, fairSd] = moments(fairValues);
  const auto [learnedMean, learnedSd] = moments(learnedValues);
  double covariance = 0.0;
  for (std::size_t index = 0; index < fairValues.size(); ++index) {
    covariance += (fairValues[index] - fairMean) * (learnedValues[index] - learnedMean);
  }
  covariance /= static_cast<double>(fairValues.size() - 1);
  out << std::setprecision(8)
      << "leaf-stats states " << fairValues.size() << "\n"
      << "  fairLeaf        mean " << fairMean << "  sd " << fairSd << "\n"
      << "  scaled learned  mean " << learnedMean << "  sd " << learnedSd << "\n"
      << "  pearson         " << covariance / (fairSd * learnedSd) << "\n";
}

struct Options {
  CohortOptions cohort;
  SearchParameters parameters;
  std::string output;
  std::string modelPath;
  std::string statesPath;
  bool parity = false;
  bool scaleSet = false;
  int parityGames = 3;
  int parityMoves = 40;
  std::size_t leafStats = 0;
};

Options parseOptions(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc;) {
    const std::string key = argv[index];
    if (key == "--parity") {
      options.parity = true;
      index += 1;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing value for " + key);
    const std::string value = argv[index + 1];
    if (key == "--seed-start") {
      options.cohort.seedStart = static_cast<std::uint32_t>(std::stoul(value, nullptr, 0));
    } else if (key == "--games") {
      options.cohort.games = std::stoi(value);
    } else if (key == "--max-moves") {
      options.cohort.maximumMoves = std::stoi(value);
    } else if (key == "--threads") {
      options.cohort.threads = std::stoi(value);
    } else if (key == "--depth") {
      options.parameters.depth = std::stoi(value);
    } else if (key == "--chance-samples") {
      options.parameters.chanceSamples = std::stoi(value);
    } else if (key == "--terminal-utility") {
      options.parameters.terminalUtility = std::stod(value);
    } else if (key == "--max-work") {
      options.parameters.maximumWork = std::stoull(value, nullptr, 0);
    } else if (key == "--w") {
      options.parameters.blendWeight = std::stod(value);
    } else if (key == "--scale") {
      options.parameters.scale = std::stod(value);
      options.scaleSet = true;
    } else if (key == "--leaf-value") {
      if (value == "lifetime") options.parameters.leafValue = LeafValueKind::kLifetime;
      else if (value == "hazard") options.parameters.leafValue = LeafValueKind::kHazard;
      else throw std::invalid_argument("--leaf-value must be lifetime or hazard");
    } else if (key == "--model") {
      options.modelPath = value;
    } else if (key == "--states") {
      options.statesPath = value;
    } else if (key == "--leaf-stats") {
      options.leafStats = static_cast<std::size_t>(std::stoul(value));
    } else if (key == "--parity-games") {
      options.parityGames = std::stoi(value);
    } else if (key == "--parity-moves") {
      options.parityMoves = std::stoi(value);
    } else if (key == "--output") {
      options.output = value;
    } else {
      throw std::invalid_argument("unknown option " + key);
    }
    index += 2;
  }
  if (!options.scaleSet && options.parameters.leafValue == LeafValueKind::kHazard) {
    options.parameters.scale = 17000.0;
  }
  return options;
}

}  // namespace drop7::lifetime::learned

int main(int argc, char** argv) {
  using namespace drop7;
  using namespace drop7::lifetime;
  namespace learned = drop7::lifetime::learned;
  try {
    auto options = learned::parseOptions(argc, argv);
    std::unique_ptr<drop7::leaf::LeafNet> net;
    if (!options.modelPath.empty()) {
      net = std::make_unique<drop7::leaf::LeafNet>(options.modelPath);
      std::cerr << "leaf model " << options.modelPath << " parameters "
                << net->parameterCount() << " fnv1a 0x" << std::hex << net->digest()
                << std::dec << "\n";
    }
    if (options.parameters.blendWeight != 0.0 && net == nullptr) {
      throw std::runtime_error("--w is non-zero but no --model was given");
    }

    if (options.leafStats > 0) {
      if (net == nullptr || options.statesPath.empty()) {
        throw std::runtime_error("--leaf-stats needs --model and --states");
      }
      learned::leafStats(*net, options.parameters, options.statesPath,
                         options.leafStats, std::cout);
      return 0;
    }

    if (options.parity) {
      const bool ok = learned::parityCheck(options.parameters, options.cohort.seedStart,
                                           options.parityGames, options.parityMoves,
                                           net.get(), std::cout);
      std::cout << (ok ? "PARITY OK\n" : "PARITY FAILED\n");
      return ok ? 0 : 1;
    }

    learned::assertLease(options.cohort.seedStart, options.cohort.games);

    const auto started = std::chrono::steady_clock::now();
    auto records = runCohort(options.cohort, [&]() {
      return [&, search = learned::LearnedLeafSearch{options.parameters, net.get()}](
                 const State& state, std::uint64_t& work) mutable {
        const int action = search.chooseAction(state, work);
        return action;
      };
    });
    const double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - started).count();

    std::ostringstream config;
    config << std::setprecision(12) << "{\"depth\": " << options.parameters.depth
           << ", \"chanceSamples\": " << options.parameters.chanceSamples
           << ", \"terminalUtility\": " << options.parameters.terminalUtility
           << ", \"maximumWork\": " << options.parameters.maximumWork
           << ", \"blendWeight\": " << options.parameters.blendWeight
           << ", \"scale\": " << options.parameters.scale
           << ", \"leafValue\": \""
           << (options.parameters.leafValue == learned::LeafValueKind::kLifetime
                   ? "lifetime" : "hazard")
           << "\", \"leafModel\": \"" << options.modelPath
           << "\", \"leafModelDigest\": \""
           << (net ? [&]() { std::ostringstream s; s << "0x" << std::hex << net->digest(); return s.str(); }() : std::string("none"))
           << "\"}";
    const std::uint64_t decisions = learned::gDecisions.load();
    const std::uint64_t netCalls = learned::gNetEvaluations.load();
    const std::uint64_t leafCalls = learned::gLeafEvaluations.load();
    std::uint64_t moveTotal = 0;
    for (const GameRecord& record : records) {
      moveTotal += static_cast<std::uint64_t>(record.moves);
    }
    std::ostringstream cost;
    cost << std::setprecision(12) << "{\n"
         << "  \"format\": \"drop7-learned-leaf-cost-v1\",\n"
         << "  \"decisions\": " << decisions << ",\n"
         << "  \"moves\": " << moveTotal << ",\n"
         << "  \"leafEvaluations\": " << leafCalls << ",\n"
         << "  \"modelEvaluations\": " << netCalls << ",\n"
         << "  \"leafEvaluationsPerDecision\": "
         << (decisions ? static_cast<double>(leafCalls) / decisions : 0.0) << ",\n"
         << "  \"modelEvaluationsPerDecision\": "
         << (decisions ? static_cast<double>(netCalls) / decisions : 0.0) << ",\n"
         << "  \"wallSeconds\": " << wall << ",\n"
         << "  \"threads\": " << options.cohort.threads << ",\n"
         << "  \"cpuSecondsPerDecision\": "
         << (decisions ? wall * options.cohort.threads / decisions : 0.0) << ",\n"
         << "  \"gamesPerHour\": "
         << (wall > 0.0 ? 3600.0 * records.size() / wall : 0.0) << "\n}\n";
    if (!options.output.empty()) {
      std::ofstream costFile(options.output + ".cost.json");
      if (costFile) costFile << cost.str();
    }
    std::cerr << cost.str();
    if (options.output.empty()) {
      writeArtifact(std::cout, "learned-leaf-fair-search", config.str(),
                    options.cohort, records, wall);
    } else {
      std::ofstream file(options.output);
      if (!file) throw std::runtime_error("cannot open " + options.output);
      writeArtifact(file, "learned-leaf-fair-search", config.str(), options.cohort,
                    records, wall);
      std::cerr << "wrote " << options.output << "\n";
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "learned-leaf failed: " << error.what() << '\n';
    return 1;
  }
}
