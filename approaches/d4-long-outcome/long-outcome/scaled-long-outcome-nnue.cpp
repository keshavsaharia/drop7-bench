#define DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY
#include "d2-long-outcome-feature-audit.cpp"
#undef DROP7_D2_LONG_OUTCOME_FEATURE_AUDIT_LIBRARY

#include <filesystem>
#include <fstream>
#include <thread>

// Trains and compares small and large long-outcome NNUEs on an expanded root
// set loaded from a fixed public-D4 artifact.  This file has no game runner or
// gameplay-seed option.  It reads the deterministic label domain only after a
// one-root projection passes the fixed resource gate.
namespace drop7::scaled_long_outcome_nnue {

namespace legacy = drop7::d2_long_outcome_feature_audit;
namespace prior = drop7::d2_long_outcome_ranker;
namespace base = drop7::scaled_d4_distill;
namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr int kFittingGames = 16;
constexpr int kHeldoutGames = 8;
constexpr int kFittingRoots = 1'508;
constexpr int kHeldoutRoots = 465;
constexpr int kTotalRoots = kFittingRoots + kHeldoutRoots;
constexpr int kFolds = 4;
constexpr int kScenarios = 7;
constexpr int kHorizon = 25;
constexpr int kWorkers = 4;
constexpr int kInputs = base::kFeatureCount + 3;
constexpr int kHeads = 5;
constexpr int kSmallHidden = 12;
constexpr int kLargeHidden = 48;
constexpr int kEpochs = 40;
constexpr int kHistoricalLevelBonus = 7'000;
constexpr double kLearningRate = 0.003;
constexpr double kWeightDecay = 0.0002;
constexpr std::array<double, kHeads> kHeadLossWeights{{
    1.0, 0.25, 0.10, 0.20, 0.10,
}};
constexpr double kProjectionSafety = 1.5;
constexpr double kLabelWallCapSeconds = 45.0 * 60.0;
constexpr std::uint64_t kMaximumRssBytes = 256u * 1024u * 1024u;
constexpr std::uint64_t kMaximumCheckpointBytes = 512u * 1024u;
constexpr std::uintmax_t kInputBytes = 527'391;
constexpr std::string_view kInputSha256 =
    "f61801abc9eefe86011f7202620a18c1277fcc1b5a24f4bce5947033b791dd89";
constexpr std::uint32_t kTapeSeedDomain = 0x5343'4c45u;  // "SCLE"
constexpr std::uint32_t kRevealDomain = 0x5352'564cu;  // "SRVL"
constexpr std::uint32_t kVisibleDomain = 0x5356'4953u;  // "SVIS"

constexpr double kTop1VsD2 = 0.02;
constexpr double kTop2VsD2 = 0.01;
constexpr double kPairwiseVsD2 = 0.01;
constexpr double kRegretRatioVsD2 = 0.95;
constexpr double kTop1VsSmall = 0.01;
constexpr double kTop2VsSmall = 0.005;
constexpr double kPairwiseVsSmall = 0.005;
constexpr double kRegretRatioVsSmall = 0.97;
constexpr double kHeadCorrelationGain = 0.02;
constexpr int kStableFoldsRequired = 3;

static_assert(kInputs == 1'650);
static_assert(kFittingRoots >= 4 * prior::kTrainingRoots);
static_assert(kSmallHidden == legacy::kHidden && kHeads == legacy::kHeads);
static_assert(kLargeHidden == 4 * kSmallHidden);
static_assert(kScenarios == prior::kScenarios && kHorizon == prior::kHorizon);
static_assert(base::fair::kLevelBonus == kHistoricalLevelBonus);
static_assert(kFolds == 4 && kFittingGames % kFolds == 0);
static_assert(kFittingRoots + kHeldoutRoots == kTotalRoots);

struct Options {
  std::string roots = "/tmp/drop7-d4-public-root-labels.jsonl";
  std::string output = "/tmp/drop7-scaled-long-outcome-nnue.json";
  std::string labels = "/tmp/drop7-scaled-long-outcome-labels.jsonl";
  std::string small_checkpoint =
      "/tmp/drop7-scaled-long-outcome-small-nnue.bin";
  std::string large_checkpoint =
      "/tmp/drop7-scaled-long-outcome-large-nnue.bin";
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string flag = argv[index];
    if (flag == "--roots") result.roots = argv[index + 1];
    else if (flag == "--output") result.output = argv[index + 1];
    else if (flag == "--labels") result.labels = argv[index + 1];
    else if (flag == "--small-checkpoint") {
      result.small_checkpoint = argv[index + 1];
    } else if (flag == "--large-checkpoint") {
      result.large_checkpoint = argv[index + 1];
    } else {
      throw std::invalid_argument("unknown option " + flag);
    }
  }
  return result;
}

constexpr std::array<std::uint32_t, 64> kSha256Constants{{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
}};

std::string sha256(std::string_view source) {
  std::vector<std::uint8_t> message(source.begin(), source.end());
  const std::uint64_t bit_length =
      static_cast<std::uint64_t>(message.size()) * 8u;
  message.push_back(0x80u);
  while (message.size() % 64 != 56) message.push_back(0u);
  for (int byte = 7; byte >= 0; --byte) {
    message.push_back(
        static_cast<std::uint8_t>((bit_length >> (byte * 8)) & 0xffu));
  }
  std::array<std::uint32_t, 8> hash{{
      0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
      0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
  }};
  for (std::size_t offset = 0; offset < message.size(); offset += 64) {
    std::array<std::uint32_t, 64> words{};
    for (int word = 0; word < 16; ++word) {
      const std::size_t begin = offset + static_cast<std::size_t>(word * 4);
      words[word] = (static_cast<std::uint32_t>(message[begin]) << 24) |
                    (static_cast<std::uint32_t>(message[begin + 1]) << 16) |
                    (static_cast<std::uint32_t>(message[begin + 2]) << 8) |
                    static_cast<std::uint32_t>(message[begin + 3]);
    }
    for (int word = 16; word < 64; ++word) {
      const std::uint32_t s0 =
          std::rotr(words[word - 15], 7) ^
          std::rotr(words[word - 15], 18) ^ (words[word - 15] >> 3);
      const std::uint32_t s1 =
          std::rotr(words[word - 2], 17) ^
          std::rotr(words[word - 2], 19) ^ (words[word - 2] >> 10);
      words[word] = words[word - 16] + s0 + words[word - 7] + s1;
    }
    std::uint32_t a = hash[0];
    std::uint32_t b = hash[1];
    std::uint32_t c = hash[2];
    std::uint32_t d = hash[3];
    std::uint32_t e = hash[4];
    std::uint32_t f = hash[5];
    std::uint32_t g = hash[6];
    std::uint32_t h = hash[7];
    for (int round = 0; round < 64; ++round) {
      const std::uint32_t upper =
          std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const std::uint32_t choose = (e & f) ^ (~e & g);
      const std::uint32_t first =
          h + upper + choose + kSha256Constants[round] + words[round];
      const std::uint32_t lower =
          std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t second = lower + majority;
      h = g;
      g = f;
      f = e;
      e = d + first;
      d = c;
      c = b;
      b = a;
      a = first + second;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }
  std::string result;
  constexpr char digits[] = "0123456789abcdef";
  result.reserve(64);
  for (const std::uint32_t value : hash) {
    for (int nibble = 7; nibble >= 0; --nibble) {
      result.push_back(digits[(value >> (nibble * 4)) & 0x0fu]);
    }
  }
  return result;
}

std::string readWholeFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) throw std::runtime_error("could not read scaled input");
  const std::streampos end = input.tellg();
  if (end < 0) throw std::runtime_error("invalid scaled input size");
  std::string result(static_cast<std::size_t>(end), '\0');
  input.seekg(0);
  input.read(result.data(), static_cast<std::streamsize>(result.size()));
  if (!input) throw std::runtime_error("scaled input read failed");
  return result;
}

struct RootCorpus {
  std::vector<base::RootLabel> fitting;
  std::vector<base::RootLabel> heldout;
};

RootCorpus loadRoots(const std::string& path) {
  std::error_code error;
  if (std::filesystem::file_size(path, error) != kInputBytes || error) {
    throw std::runtime_error("scaled input root byte count mismatch");
  }
  if (sha256(readWholeFile(path)) != kInputSha256) {
    throw std::runtime_error("scaled input root SHA-256 mismatch");
  }
  RootCorpus result{base::loadSplit(path, "training"),
                    base::loadSplit(path, "heldout")};
  if (result.fitting.size() != kFittingRoots ||
      result.heldout.size() != kHeldoutRoots ||
      result.fitting.back().game != kFittingGames - 1 ||
      result.heldout.back().game != kHeldoutGames - 1) {
    throw std::runtime_error("scaled input root/game counts changed");
  }
  return result;
}

int occupiedCells(const Board& board) {
  return static_cast<int>(std::count_if(
      board.begin(), board.end(), [](std::uint8_t cell) {
        return cell != kEmpty;
      }));
}

int legalActions(const Board& board) {
  int count = 0;
  legalColumns(board, count);
  return count;
}

std::size_t selectPilot(const std::vector<base::RootLabel>& fitting) {
  std::size_t best = 0;
  for (std::size_t index = 1; index < fitting.size(); ++index) {
    const auto key = std::pair{legalActions(fitting[index].board),
                               occupiedCells(fitting[index].board)};
    const auto best_key = std::pair{legalActions(fitting[best].board),
                                    occupiedCells(fitting[best].board)};
    if (key > best_key) best = index;
  }
  return best;
}

class LabelDeadlineReached : public std::runtime_error {
 public:
  LabelDeadlineReached() : std::runtime_error("scaled label wall cap reached") {}
};

struct ScenarioOutcome {
  double value = 0.0;
  int clears = 0;
  int moves = 0;
  bool survived = false;
};

struct ActionOutcome {
  std::array<ScenarioOutcome, kScenarios> scenarios{};
  double mean_return = 0.0;
  double survival = 0.0;
  double mean_clears = 0.0;
  double downside = 0.0;
  double variance = 0.0;
  double expected_post_ladder = 0.0;
};

struct OutcomeRoot {
  base::RootLabel label{};
  std::array<ActionOutcome, kBoardSize> actions{};
  double pre_ladder = 0.0;
  std::uint64_t transitions = 0;
  prior::D2Metrics d2{};
  double wall_seconds = 0.0;
};

OutcomeRoot evaluateRoot(const base::RootLabel& source,
                         Clock::time_point deadline) {
  const auto started = Clock::now();
  bool was_mirrored = false;
  const State canonical_state =
      cfpi::detail::canonicalState(base::publicState(source), was_mirrored);
  const prior::ObservableState root = prior::observable(canonical_state);
  const std::uint32_t root_seed = prior::seed32(
      prior::publicHash(root) ^ static_cast<std::uint64_t>(kTapeSeedDomain));
  OutcomeRoot result;
  result.label = source;
  result.label.board = root.board;
  result.label.next_disc = root.next_disc;
  result.label.moves_remaining = root.moves_remaining;
  result.label.q.fill(-std::numeric_limits<double>::infinity());
  result.label.legal.fill(false);
  result.pre_ladder = legacy::verticalLadderFeatures(root.board).energy;
  double global_minimum = std::numeric_limits<double>::infinity();
  double global_maximum = -std::numeric_limits<double>::infinity();
  int best_action = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (const int action : base::kActionOrder) {
    if (!isLegal(root.board, action)) continue;
    result.label.legal[action] = true;
    ActionOutcome& action_result = result.actions[action];
    for (int scenario = 0; scenario < kScenarios; ++scenario) {
      prior::ObservableState state = root;
      ScenarioOutcome& outcome = action_result.scenarios[scenario];
      for (int step = 0; step < kHorizon; ++step) {
        if (Clock::now() >= deadline) throw LabelDeadlineReached{};
        const int selected =
            step == 0 ? action : prior::d2Action(state, result.d2);
        if (!isLegal(state.board, selected)) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        MoveResult move;
        if (!prior::playSyntheticMove(
                state, selected, root_seed, scenario, step, move,
                {kRevealDomain, kVisibleDomain})) {
          outcome.value += fair::kTerminalUtility;
          state.game_over = true;
          break;
        }
        ++result.transitions;
        outcome.value += static_cast<double>(move.score_delta);
        ++outcome.moves;
        for (const Wave& wave : move.waves) outcome.clears += wave.cleared;
        if (step == 0) {
          action_result.expected_post_ladder +=
              legacy::verticalLadderFeatures(move.state.board).energy /
              kScenarios;
        }
        state = prior::observable(move.state);
        if (state.game_over) {
          outcome.value += fair::kTerminalUtility;
          break;
        }
      }
      if (!state.game_over) {
        outcome.survived = true;
        outcome.value += fair::fairLeaf(prior::materialize(state));
      }
      action_result.mean_return += outcome.value / kScenarios;
      action_result.survival += outcome.survived ? 1.0 / kScenarios : 0.0;
      action_result.mean_clears +=
          static_cast<double>(outcome.clears) / kScenarios;
      global_minimum = std::min(global_minimum, outcome.value);
      global_maximum = std::max(global_maximum, outcome.value);
    }
    result.label.q[action] = action_result.mean_return;
    if (best_action < 0 || action_result.mean_return > best_value) {
      best_action = action;
      best_value = action_result.mean_return;
    }
  }
  const double return_range =
      std::max(1.0e-9, global_maximum - global_minimum);
  double maximum_clears = 1.0;
  for (int action = 0; action < kBoardSize; ++action) {
    if (!result.label.legal[action]) continue;
    maximum_clears =
        std::max(maximum_clears, result.actions[action].mean_clears);
  }
  for (int action = 0; action < kBoardSize; ++action) {
    if (!result.label.legal[action]) continue;
    ActionOutcome& value = result.actions[action];
    double minimum = std::numeric_limits<double>::infinity();
    for (const ScenarioOutcome& scenario : value.scenarios) {
      minimum = std::min(minimum, scenario.value);
      const double centered = scenario.value - value.mean_return;
      value.variance += centered * centered / kScenarios;
    }
    value.downside = (minimum - global_minimum) / return_range;
    value.variance /= return_range * return_range;
    value.mean_clears /= maximum_clears;
  }
  result.label.labeled_action = best_action;
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (best_action < 0 ||
      result.transitions > prior::kMaximumSyntheticTransitionsPerRoot ||
      result.d2.calls > prior::kMaximumD2CallsPerRoot ||
      result.d2.work > result.d2.calls * prior::kWorstD2Work ||
      result.d2.peak_cache_entries > prior::kWorstD2CacheEntries) {
    throw std::runtime_error("scaled outcome root exceeded resource bound");
  }
  (void)was_mirrored;
  return result;
}

struct LabelCost {
  std::uint64_t roots = 0;
  std::uint64_t transitions = 0;
  prior::D2Metrics d2{};
  double aggregate_root_seconds = 0.0;
  double wall_seconds = 0.0;
};

void observeCost(const OutcomeRoot& root, LabelCost& cost) {
  ++cost.roots;
  cost.transitions += root.transitions;
  cost.d2.calls += root.d2.calls;
  cost.d2.work += root.d2.work;
  cost.d2.nodes += root.d2.nodes;
  cost.d2.cache_hits += root.d2.cache_hits;
  cost.d2.peak_cache_entries =
      std::max(cost.d2.peak_cache_entries, root.d2.peak_cache_entries);
  cost.aggregate_root_seconds += root.wall_seconds;
}

struct LabeledRange {
  std::vector<OutcomeRoot> roots;
  LabelCost cost{};
};

LabeledRange generateRange(const std::vector<base::RootLabel>& source,
                           std::string_view split,
                           Clock::time_point deadline,
                           std::optional<std::pair<std::size_t, OutcomeRoot>>
                               reused_pilot = std::nullopt) {
  const auto started = Clock::now();
  LabeledRange result;
  result.roots.resize(source.size());
  std::atomic<std::size_t> next{0};
  std::atomic<std::size_t> completed{0};
  std::mutex exception_mutex;
  std::exception_ptr exception;
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < kWorkers; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      try {
        while (true) {
          const std::size_t index = next.fetch_add(1);
          if (index >= source.size()) return;
          if (reused_pilot.has_value() && index == reused_pilot->first) {
            result.roots[index] = reused_pilot->second;
          } else {
            result.roots[index] = evaluateRoot(source[index], deadline);
          }
          if (prior::peakRssBytes() > kMaximumRssBytes) {
            throw std::runtime_error("scaled label RSS cap reached");
          }
          const std::size_t count = completed.fetch_add(1) + 1;
          if (count % 25 == 0 || count == source.size()) {
            std::lock_guard<std::mutex> lock(prior::progress_mutex);
            std::cerr << "scaled-long-label " << split << ' ' << count << '/'
                      << source.size() << '\n';
          }
        }
      } catch (...) {
        std::lock_guard<std::mutex> lock(exception_mutex);
        if (exception == nullptr) exception = std::current_exception();
        next.store(source.size());
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  if (exception != nullptr) std::rethrow_exception(exception);
  for (const OutcomeRoot& root : result.roots) observeCost(root, result.cost);
  result.cost.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  if (result.cost.roots != source.size() ||
      result.cost.transitions >
          source.size() * prior::kMaximumSyntheticTransitionsPerRoot ||
      result.cost.d2.work > result.cost.d2.calls * prior::kWorstD2Work ||
      result.cost.d2.peak_cache_entries > prior::kWorstD2CacheEntries) {
    throw std::runtime_error("scaled label range resource/full-root failure");
  }
  return result;
}

void writeBoard(std::ostream& output, const Board& board) {
  for (const std::uint8_t cell : board) {
    output << static_cast<char>('0' + cell);
  }
}

void writeLabelRange(std::ostream& output,
                     const std::vector<OutcomeRoot>& roots,
                     std::string_view split) {
  for (const OutcomeRoot& root : roots) {
    output << std::setprecision(12) << "{\"split\":\"" << split
           << "\",\"game\":" << root.label.game
           << ",\"moveInSourceGame\":" << root.label.move_in_game
           << ",\"board\":\"";
    writeBoard(output, root.label.board);
    output << "\",\"nextDisc\":" << static_cast<int>(root.label.next_disc)
           << ",\"movesRemaining\":" << root.label.moves_remaining
           << ",\"optimalAction\":" << root.label.labeled_action
           << ",\"preLadder\":" << root.pre_ladder
           << ",\"actions\":[";
    for (int action = 0; action < kBoardSize; ++action) {
      if (action != 0) output << ',';
      if (!root.label.legal[action]) {
        output << "null";
        continue;
      }
      const ActionOutcome& value = root.actions[action];
      output << "{\"action\":" << action
             << ",\"meanReturn\":" << value.mean_return
             << ",\"survival\":" << value.survival
             << ",\"normalizedMeanClears\":" << value.mean_clears
             << ",\"downside\":" << value.downside
             << ",\"variance\":" << value.variance
             << ",\"expectedPostLadder\":"
             << value.expected_post_ladder << ",\"scenarios\":[";
      for (int scenario = 0; scenario < kScenarios; ++scenario) {
        if (scenario != 0) output << ',';
        const ScenarioOutcome& item = value.scenarios[scenario];
        output << "{\"return\":" << item.value
               << ",\"clears\":" << item.clears
               << ",\"moves\":" << item.moves
               << ",\"survived\":"
               << (item.survived ? "true" : "false") << '}';
      }
      output << "]}";
    }
    output << "],\"transitions\":" << root.transitions
           << ",\"d2Calls\":" << root.d2.calls
           << ",\"d2Work\":" << root.d2.work
           << ",\"seconds\":" << root.wall_seconds << "}\n";
  }
}

void writeLabels(const Options& options, const LabeledRange& fitting,
                 const LabeledRange& heldout) {
  std::ofstream output(options.labels);
  if (!output) throw std::runtime_error("could not write scaled labels");
  output << "{\"type\":\"metadata\",\"format\":\"drop7-scaled-public-d2-long-outcomes-v1\""
         << ",\"sourceSha256\":\"" << kInputSha256 << "\""
         << ",\"newRootStates\":0,\"newGameplaySeeds\":0"
         << ",\"levelBonus\":" << kHistoricalLevelBonus
         << ",\"scoreSemantics\":\"historical 7k Sequence-style; not five-drop Hardcore/Blitz score-calibrated\""
         << ",\"tapeSeedDomain\":" << kTapeSeedDomain
         << ",\"revealDomain\":" << kRevealDomain
         << ",\"visibleDomain\":" << kVisibleDomain
         << ",\"horizon\":" << kHorizon
         << ",\"scenarios\":" << kScenarios
         << ",\"fittingRoots\":" << fitting.roots.size()
         << ",\"heldoutRoots\":" << heldout.roots.size() << "}\n";
  writeLabelRange(output, fitting.roots, "fitting");
  writeLabelRange(output, heldout.roots, "old-heldout-burned");
}

struct PreparedRoot {
  OutcomeRoot outcome{};
  base::PreparedRoot prepared{};
};

std::vector<PreparedRoot> prepareRange(
    const std::vector<OutcomeRoot>& outcomes) {
  std::vector<PreparedRoot> result;
  result.reserve(outcomes.size());
  for (const OutcomeRoot& outcome : outcomes) {
    result.push_back({outcome, base::prepare(outcome.label)});
  }
  return result;
}

enum Head : int {
  kReturnResidual = 0,
  kSurvival = 1,
  kClears = 2,
  kDownside = 3,
  kVariance = 4,
};

template <typename Function>
void forEachInput(const PreparedRoot& root, int action, bool reflected,
                  Function function) {
  const base::FeatureVector& sparse =
      reflected ? root.prepared.reflected[action]
                : root.prepared.direct[action];
  for (const base::SparseFeature& feature : sparse) {
    function(static_cast<int>(feature.index), feature.value);
  }
  const ActionOutcome& value = root.outcome.actions[action];
  const std::array<double, 3> ladder{{
      root.outcome.pre_ladder,
      value.expected_post_ladder,
      value.expected_post_ladder - root.outcome.pre_ladder,
  }};
  for (int index = 0; index < 3; ++index) {
    function(base::kFeatureCount + index, ladder[index]);
  }
}

struct NeuralModel {
  int hidden = 0;
  int epochs = 0;
  std::vector<float> input_weights;
  std::vector<float> hidden_bias;
  std::vector<float> head_weights;
  std::array<float, kHeads> head_bias{};
  std::array<float, kInputs> input_scale{};

  explicit NeuralModel(int hidden_count = 0)
      : hidden(hidden_count),
        input_weights(static_cast<std::size_t>(kInputs * hidden_count)),
        hidden_bias(static_cast<std::size_t>(hidden_count)),
        head_weights(static_cast<std::size_t>(kHeads * hidden_count)) {}
};

template <typename Include>
std::array<float, kInputs> inputScales(
    const std::vector<PreparedRoot>& roots, Include include) {
  std::array<double, kInputs> squares{};
  std::uint64_t orientations = 0;
  for (const PreparedRoot& root : roots) {
    if (!include(root)) continue;
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.outcome.label.legal[action]) continue;
      for (const bool reflected : {false, true}) {
        forEachInput(root, action, reflected, [&](int index, double value) {
          squares[index] += value * value;
        });
        ++orientations;
      }
    }
  }
  if (orientations == 0) throw std::runtime_error("empty scale corpus");
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

template <typename Include>
NeuralModel initializedModel(const std::vector<PreparedRoot>& roots,
                             Include include, int hidden,
                             std::uint32_t seed) {
  NeuralModel result(hidden);
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
  std::vector<double> direct_z;
  std::vector<double> reflected_z;
  std::vector<double> hidden;
  std::array<double, kHeads> heads{};

  explicit Forward(int hidden_count)
      : direct_z(static_cast<std::size_t>(hidden_count)),
        reflected_z(static_cast<std::size_t>(hidden_count)),
        hidden(static_cast<std::size_t>(hidden_count)) {}
};

Forward forward(const NeuralModel& model, const PreparedRoot& root,
                int action) {
  Forward result(model.hidden);
  for (int hidden = 0; hidden < model.hidden; ++hidden) {
    result.direct_z[hidden] = model.hidden_bias[hidden];
    result.reflected_z[hidden] = model.hidden_bias[hidden];
  }
  forEachInput(root, action, false, [&](int input, double value) {
    const double scaled = value * model.input_scale[input];
    for (int hidden = 0; hidden < model.hidden; ++hidden) {
      result.direct_z[hidden] +=
          model.input_weights[hidden * kInputs + input] * scaled;
    }
  });
  forEachInput(root, action, true, [&](int input, double value) {
    const double scaled = value * model.input_scale[input];
    for (int hidden = 0; hidden < model.hidden; ++hidden) {
      result.reflected_z[hidden] +=
          model.input_weights[hidden * kInputs + input] * scaled;
    }
  });
  for (int hidden = 0; hidden < model.hidden; ++hidden) {
    result.hidden[hidden] =
        0.5 * (std::max(0.0, result.direct_z[hidden]) +
               std::max(0.0, result.reflected_z[hidden]));
  }
  for (int head = 0; head < kHeads; ++head) {
    result.heads[head] = model.head_bias[head];
    for (int hidden = 0; hidden < model.hidden; ++hidden) {
      result.heads[head] +=
          model.head_weights[head * model.hidden + hidden] *
          result.hidden[hidden];
    }
  }
  return result;
}

std::array<double, kHeads> headTargets(const PreparedRoot& root, int action) {
  const ActionOutcome& value = root.outcome.actions[action];
  return {{
      root.prepared.target[action] - root.prepared.d2[action],
      value.survival,
      value.mean_clears,
      value.downside,
      value.variance,
  }};
}

struct Gradient {
  std::vector<double> input_weights;
  std::vector<double> hidden_bias;
  std::vector<double> head_weights;
  std::array<double, kHeads> head_bias{};

  explicit Gradient(int hidden)
      : input_weights(static_cast<std::size_t>(kInputs * hidden)),
        hidden_bias(static_cast<std::size_t>(hidden)),
        head_weights(static_cast<std::size_t>(kHeads * hidden)) {}
};

void accumulateGradient(const NeuralModel& model, const PreparedRoot& root,
                        int action, Gradient& gradient) {
  const Forward computed = forward(model, root, action);
  const auto target = headTargets(root, action);
  std::array<double, kHeads> head_gradient{};
  for (int head = 0; head < kHeads; ++head) {
    head_gradient[head] =
        2.0 * kHeadLossWeights[head] * (computed.heads[head] - target[head]);
    gradient.head_bias[head] += head_gradient[head];
    for (int hidden = 0; hidden < model.hidden; ++hidden) {
      gradient.head_weights[head * model.hidden + hidden] +=
          head_gradient[head] * computed.hidden[hidden];
    }
  }
  std::vector<double> hidden_gradient(
      static_cast<std::size_t>(model.hidden));
  for (int hidden = 0; hidden < model.hidden; ++hidden) {
    for (int head = 0; head < kHeads; ++head) {
      hidden_gradient[hidden] +=
          head_gradient[head] *
          model.head_weights[head * model.hidden + hidden];
    }
    gradient.hidden_bias[hidden] +=
        0.5 * hidden_gradient[hidden] *
        ((computed.direct_z[hidden] > 0.0 ? 1.0 : 0.0) +
         (computed.reflected_z[hidden] > 0.0 ? 1.0 : 0.0));
  }
  for (const bool reflected : {false, true}) {
    forEachInput(root, action, reflected, [&](int input, double value) {
      const double scaled = value * model.input_scale[input];
      for (int hidden = 0; hidden < model.hidden; ++hidden) {
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
  Gradient first;
  Gradient second;
  std::uint64_t step = 0;

  explicit AdamState(int hidden) : first(hidden), second(hidden) {}
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

template <typename Include>
NeuralModel trainModel(const std::vector<PreparedRoot>& roots,
                       Include include, int hidden, int epochs,
                       std::uint32_t seed) {
  NeuralModel model = initializedModel(roots, include, hidden, seed);
  AdamState adam(hidden);
  for (int epoch = 1; epoch <= epochs; ++epoch) {
    Gradient gradient(hidden);
    std::uint64_t rows = 0;
    for (const PreparedRoot& root : roots) {
      if (!include(root)) continue;
      for (int action = 0; action < kBoardSize; ++action) {
        if (!root.outcome.label.legal[action]) continue;
        accumulateGradient(model, root, action, gradient);
        ++rows;
      }
    }
    if (rows == 0) throw std::runtime_error("empty scaled NNUE fold");
    ++adam.step;
    const double inverse = 1.0 / static_cast<double>(rows);
    for (std::size_t index = 0; index < model.input_weights.size(); ++index) {
      const double value = gradient.input_weights[index] * inverse +
                           kWeightDecay * model.input_weights[index];
      updateValue(model.input_weights[index], value,
                  adam.first.input_weights[index],
                  adam.second.input_weights[index], adam.step);
    }
    for (int hidden_index = 0; hidden_index < hidden; ++hidden_index) {
      updateValue(model.hidden_bias[hidden_index],
                  gradient.hidden_bias[hidden_index] * inverse,
                  adam.first.hidden_bias[hidden_index],
                  adam.second.hidden_bias[hidden_index], adam.step);
    }
    for (std::size_t index = 0; index < model.head_weights.size(); ++index) {
      const double value = gradient.head_weights[index] * inverse +
                           kWeightDecay * model.head_weights[index];
      updateValue(model.head_weights[index], value,
                  adam.first.head_weights[index],
                  adam.second.head_weights[index], adam.step);
    }
    for (int head = 0; head < kHeads; ++head) {
      updateValue(model.head_bias[head], gradient.head_bias[head] * inverse,
                  adam.first.head_bias[head], adam.second.head_bias[head],
                  adam.step);
    }
  }
  model.epochs = epochs;
  return model;
}

std::array<double, kBoardSize> d2Scores(const PreparedRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (root.outcome.label.legal[action]) result[action] = root.prepared.d2[action];
  }
  return result;
}

std::array<double, kBoardSize> neuralScores(const NeuralModel& model,
                                             const PreparedRoot& root) {
  std::array<double, kBoardSize> result{};
  result.fill(-std::numeric_limits<double>::infinity());
  for (int action = 0; action < kBoardSize; ++action) {
    if (!root.outcome.label.legal[action]) continue;
    result[action] = root.prepared.d2[action] +
                     forward(model, root, action).heads[kReturnResidual];
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

void addHeads(HeadMoments& target, const HeadMoments& source) {
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
  if (value.rows < 2) return 0.0;
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

void observeHeadRow(HeadMoments& moments,
                    const std::array<double, kHeads>& prediction,
                    const std::array<double, kHeads>& target) {
  ++moments.rows;
  for (int head = 0; head < kHeads; ++head) {
    const double error = prediction[head] - target[head];
    moments.squared_error[head] += error * error;
    moments.sum_prediction[head] += prediction[head];
    moments.sum_target[head] += target[head];
    moments.sum_prediction_squared[head] += prediction[head] * prediction[head];
    moments.sum_target_squared[head] += target[head] * target[head];
    moments.sum_product[head] += prediction[head] * target[head];
  }
}

struct Evaluation {
  base::Ranking ranking{};
  HeadMoments heads{};
};

void addEvaluation(Evaluation& target, const Evaluation& source) {
  target.ranking.roots += source.ranking.roots;
  target.ranking.top1 += source.ranking.top1;
  target.ranking.top2 += source.ranking.top2;
  target.ranking.pairs += source.ranking.pairs;
  target.ranking.pairwise_credit += source.ranking.pairwise_credit;
  target.ranking.normalized_regret += source.ranking.normalized_regret;
  addHeads(target.heads, source.heads);
}

template <typename Include>
Evaluation evaluateD2(const std::vector<PreparedRoot>& roots,
                      Include include) {
  Evaluation result;
  for (const PreparedRoot& root : roots) {
    if (!include(root)) continue;
    base::observe(root.outcome.label, d2Scores(root), result.ranking);
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.outcome.label.legal[action]) continue;
      std::array<double, kHeads> prediction{};
      prediction[kSurvival] = root.prepared.d2[action];
      prediction[kDownside] = root.prepared.d2[action];
      observeHeadRow(result.heads, prediction, headTargets(root, action));
    }
  }
  return result;
}

template <typename Include>
Evaluation evaluateModel(const NeuralModel& model,
                         const std::vector<PreparedRoot>& roots,
                         Include include) {
  Evaluation result;
  for (const PreparedRoot& root : roots) {
    if (!include(root)) continue;
    base::observe(root.outcome.label, neuralScores(model, root), result.ranking);
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.outcome.label.legal[action]) continue;
      observeHeadRow(result.heads, forward(model, root, action).heads,
                     headTargets(root, action));
    }
  }
  return result;
}

struct FoldAudit {
  Evaluation d2{};
  Evaluation small{};
  Evaluation large{};
};

struct ModelAudit {
  std::array<FoldAudit, kFolds> folds{};
  Evaluation fitting_d2_cv{};
  Evaluation fitting_small_cv{};
  Evaluation fitting_large_cv{};
  NeuralModel final_small{};
  NeuralModel final_large{};
  Evaluation heldout_d2{};
  Evaluation heldout_small{};
  Evaluation heldout_large{};
  std::array<Evaluation, 2> heldout_d2_halves{};
  std::array<Evaluation, 2> heldout_small_halves{};
  std::array<Evaluation, 2> heldout_large_halves{};
  double training_seconds = 0.0;
};

ModelAudit auditModels(const std::vector<PreparedRoot>& fitting,
                       const std::vector<PreparedRoot>& heldout) {
  const auto started = Clock::now();
  ModelAudit result;
  std::array<std::future<FoldAudit>, kFolds> futures;
  for (int fold = 0; fold < kFolds; ++fold) {
    futures[fold] = std::async(std::launch::async, [&, fold] {
      const auto training = [fold](const PreparedRoot& root) {
        return root.outcome.label.game % kFolds != fold;
      };
      const auto validation = [fold](const PreparedRoot& root) {
        return root.outcome.label.game % kFolds == fold;
      };
      const NeuralModel small = trainModel(
          fitting, training, kSmallHidden, kEpochs,
          0x534d'0000u ^ static_cast<std::uint32_t>(fold));
      const NeuralModel large = trainModel(
          fitting, training, kLargeHidden, kEpochs,
          0x4c47'0000u ^ static_cast<std::uint32_t>(fold));
      return FoldAudit{evaluateD2(fitting, validation),
                       evaluateModel(small, fitting, validation),
                       evaluateModel(large, fitting, validation)};
    });
  }
  for (int fold = 0; fold < kFolds; ++fold) {
    result.folds[fold] = futures[fold].get();
    addEvaluation(result.fitting_d2_cv, result.folds[fold].d2);
    addEvaluation(result.fitting_small_cv, result.folds[fold].small);
    addEvaluation(result.fitting_large_cv, result.folds[fold].large);
  }

  const auto all = [](const PreparedRoot&) { return true; };
  auto small_future = std::async(std::launch::async, [&] {
    return trainModel(fitting, all, kSmallHidden, kEpochs, 0x534d'ffffu);
  });
  auto large_future = std::async(std::launch::async, [&] {
    return trainModel(fitting, all, kLargeHidden, kEpochs, 0x4c47'ffffu);
  });
  result.final_small = small_future.get();
  result.final_large = large_future.get();
  result.heldout_d2 = evaluateD2(heldout, all);
  result.heldout_small = evaluateModel(result.final_small, heldout, all);
  result.heldout_large = evaluateModel(result.final_large, heldout, all);
  for (int half = 0; half < 2; ++half) {
    const auto include = [half](const PreparedRoot& root) {
      return root.outcome.label.game / (kHeldoutGames / 2) == half;
    };
    result.heldout_d2_halves[half] = evaluateD2(heldout, include);
    result.heldout_small_halves[half] =
        evaluateModel(result.final_small, heldout, include);
    result.heldout_large_halves[half] =
        evaluateModel(result.final_large, heldout, include);
  }
  result.training_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

double headRmse(const HeadMoments& value, int head) {
  if (value.rows == 0) return 0.0;
  return std::sqrt(value.squared_error[head] /
                   static_cast<double>(value.rows));
}

bool rankingNonregressed(const Evaluation& candidate,
                         const Evaluation& anchor) {
  return base::top1Rate(candidate.ranking) >=
             base::top1Rate(anchor.ranking) &&
         base::top2Rate(candidate.ranking) >=
             base::top2Rate(anchor.ranking) &&
         base::pairwiseRate(candidate.ranking) >=
             base::pairwiseRate(anchor.ranking) &&
         base::regret(candidate.ranking) <= base::regret(anchor.ranking);
}

bool rankingImproved(const Evaluation& candidate, const Evaluation& anchor,
                     double top1, double top2, double pairwise,
                     double regret_ratio) {
  return base::top1Rate(candidate.ranking) >=
             base::top1Rate(anchor.ranking) + top1 &&
         base::top2Rate(candidate.ranking) >=
             base::top2Rate(anchor.ranking) + top2 &&
         base::pairwiseRate(candidate.ranking) >=
             base::pairwiseRate(anchor.ranking) + pairwise &&
         base::regret(candidate.ranking) <=
             regret_ratio * base::regret(anchor.ranking);
}

struct FrozenGate {
  bool cv_vs_d2 = false;
  bool cv_vs_small = false;
  int stable_folds = 0;
  bool heldout_vs_d2 = false;
  bool heldout_vs_small = false;
  bool heldout_halves = false;
  bool cv_survival = false;
  bool cv_downside = false;
  bool heldout_survival = false;
  bool heldout_downside = false;
  bool heldout_head_halves = false;
  bool passed = false;
};

FrozenGate applyFrozenGate(const ModelAudit& audit) {
  FrozenGate result;
  result.cv_vs_d2 = rankingImproved(
      audit.fitting_large_cv, audit.fitting_d2_cv, kTop1VsD2, kTop2VsD2,
      kPairwiseVsD2, kRegretRatioVsD2);
  result.cv_vs_small = rankingImproved(
      audit.fitting_large_cv, audit.fitting_small_cv, kTop1VsSmall,
      kTop2VsSmall, kPairwiseVsSmall, kRegretRatioVsSmall);
  for (const FoldAudit& fold : audit.folds) {
    result.stable_folds +=
        rankingNonregressed(fold.large, fold.d2) &&
        rankingNonregressed(fold.large, fold.small);
  }
  result.heldout_vs_d2 = rankingImproved(
      audit.heldout_large, audit.heldout_d2, kTop1VsD2, kTop2VsD2,
      kPairwiseVsD2, kRegretRatioVsD2);
  result.heldout_vs_small = rankingImproved(
      audit.heldout_large, audit.heldout_small, kTop1VsSmall,
      kTop2VsSmall, kPairwiseVsSmall, kRegretRatioVsSmall);
  result.heldout_halves = true;
  result.heldout_head_halves = true;
  for (int half = 0; half < 2; ++half) {
    result.heldout_halves =
        result.heldout_halves &&
        rankingNonregressed(audit.heldout_large_halves[half],
                            audit.heldout_d2_halves[half]) &&
        rankingNonregressed(audit.heldout_large_halves[half],
                            audit.heldout_small_halves[half]);
    result.heldout_head_halves =
        result.heldout_head_halves &&
        headCorrelation(audit.heldout_large_halves[half].heads, kSurvival) >=
            headCorrelation(audit.heldout_small_halves[half].heads,
                            kSurvival) &&
        headCorrelation(audit.heldout_large_halves[half].heads, kDownside) >=
            headCorrelation(audit.heldout_small_halves[half].heads,
                            kDownside);
  }
  result.cv_survival =
      headCorrelation(audit.fitting_large_cv.heads, kSurvival) >=
      headCorrelation(audit.fitting_small_cv.heads, kSurvival) +
          kHeadCorrelationGain;
  result.cv_downside =
      headCorrelation(audit.fitting_large_cv.heads, kDownside) >=
      headCorrelation(audit.fitting_small_cv.heads, kDownside) +
          kHeadCorrelationGain;
  result.heldout_survival =
      headCorrelation(audit.heldout_large.heads, kSurvival) >=
      headCorrelation(audit.heldout_small.heads, kSurvival) +
          kHeadCorrelationGain;
  result.heldout_downside =
      headCorrelation(audit.heldout_large.heads, kDownside) >=
      headCorrelation(audit.heldout_small.heads, kDownside) +
          kHeadCorrelationGain;
  result.passed =
      result.cv_vs_d2 && result.cv_vs_small &&
      result.stable_folds >= kStableFoldsRequired &&
      result.heldout_vs_d2 && result.heldout_vs_small &&
      result.heldout_halves && result.cv_survival && result.cv_downside &&
      result.heldout_survival && result.heldout_downside &&
      result.heldout_head_halves;
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
  hash ^= static_cast<std::uint64_t>(model.hidden);
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(model.epochs);
  hash *= 0x0000'0100'0000'01b3ull;
  return hash;
}

constexpr std::array<char, 8> kCheckpointMagic{{
    'D', '7', 'S', 'L', 'N', 'N', '1', '\0',
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
  if (model.hidden != kSmallHidden && model.hidden != kLargeHidden) {
    throw std::runtime_error("invalid scaled checkpoint hidden size");
  }
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not write scaled checkpoint");
  const CheckpointHeader header{
      kCheckpointMagic, kInputs, static_cast<std::uint32_t>(model.hidden),
      kHeads, static_cast<std::uint32_t>(model.epochs),
      modelFingerprint(model)};
  output.write(reinterpret_cast<const char*>(&header), sizeof(header));
  writeFloats(output, model.input_weights);
  writeFloats(output, model.hidden_bias);
  writeFloats(output, model.head_weights);
  writeFloats(output, model.head_bias);
  writeFloats(output, model.input_scale);
  if (!output) throw std::runtime_error("scaled checkpoint write failed");
}

NeuralModel readCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not read scaled checkpoint");
  CheckpointHeader header;
  input.read(reinterpret_cast<char*>(&header), sizeof(header));
  if (!input || header.magic != kCheckpointMagic ||
      header.inputs != kInputs || header.heads != kHeads ||
      (header.hidden != kSmallHidden && header.hidden != kLargeHidden)) {
    throw std::runtime_error("invalid scaled checkpoint header");
  }
  NeuralModel model(static_cast<int>(header.hidden));
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
      header.fingerprint != modelFingerprint(model)) {
    throw std::runtime_error("invalid scaled checkpoint payload");
  }
  return model;
}

std::uint64_t fileBytes(const std::string& path) {
  std::error_code error;
  const std::uintmax_t bytes = std::filesystem::file_size(path, error);
  if (error || bytes > std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("could not size scaled file");
  }
  return static_cast<std::uint64_t>(bytes);
}

void writeHeadMetrics(std::ostream& output, const HeadMoments& value) {
  constexpr std::array<std::string_view, kHeads> names{{
      "meanReturnResidual", "survival", "normalizedMeanClears", "downside",
      "variance",
  }};
  output << "{\"rows\":" << value.rows;
  for (int head = 0; head < kHeads; ++head) {
    output << ",\"" << names[head] << "\":{\"rmse\":"
           << headRmse(value, head) << ",\"pearson\":"
           << headCorrelation(value, head) << '}';
  }
  output << '}';
}

void writeEvaluation(std::ostream& output, const Evaluation& value) {
  output << "{\"roots\":" << value.ranking.roots
         << ",\"top1WithTies\":" << base::top1Rate(value.ranking)
         << ",\"top2WithTies\":" << base::top2Rate(value.ranking)
         << ",\"pairwise\":" << base::pairwiseRate(value.ranking)
         << ",\"normalizedRegret\":" << base::regret(value.ranking)
         << ",\"headPrediction\":";
  writeHeadMetrics(output, value.heads);
  output << '}';
}

void writeGate(std::ostream& output, const FrozenGate& value) {
  output << "{\"passed\":" << (value.passed ? "true" : "false")
         << ",\"cvVsD2AllThresholds\":"
         << (value.cv_vs_d2 ? "true" : "false")
         << ",\"cvVsSmallAllThresholds\":"
         << (value.cv_vs_small ? "true" : "false")
         << ",\"stableOuterFolds\":" << value.stable_folds
         << ",\"requiredStableOuterFolds\":" << kStableFoldsRequired
         << ",\"oldHeldoutVsD2AllThresholds\":"
         << (value.heldout_vs_d2 ? "true" : "false")
         << ",\"oldHeldoutVsSmallAllThresholds\":"
         << (value.heldout_vs_small ? "true" : "false")
         << ",\"oldHeldoutBothHalvesRankingNonregression\":"
         << (value.heldout_halves ? "true" : "false")
         << ",\"cvSurvivalCorrelationGain\":"
         << (value.cv_survival ? "true" : "false")
         << ",\"cvDownsideCorrelationGain\":"
         << (value.cv_downside ? "true" : "false")
         << ",\"oldHeldoutSurvivalCorrelationGain\":"
         << (value.heldout_survival ? "true" : "false")
         << ",\"oldHeldoutDownsideCorrelationGain\":"
         << (value.heldout_downside ? "true" : "false")
         << ",\"oldHeldoutBothHalvesHeadNonregression\":"
         << (value.heldout_head_halves ? "true" : "false") << '}';
}

void writeLabelCost(std::ostream& output, const LabelCost& value) {
  output << "{\"roots\":" << value.roots
         << ",\"syntheticTransitions\":" << value.transitions
         << ",\"d2Calls\":" << value.d2.calls
         << ",\"d2Work\":" << value.d2.work
         << ",\"d2Nodes\":" << value.d2.nodes
         << ",\"d2CacheHits\":" << value.d2.cache_hits
         << ",\"peakD2CacheEntries\":" << value.d2.peak_cache_entries
         << ",\"aggregateRootSeconds\":" << value.aggregate_root_seconds
         << ",\"parallelWallSeconds\":" << value.wall_seconds << '}';
}

double reflectionSwapGap(const NeuralModel& model,
                         const std::vector<PreparedRoot>& roots) {
  double gap = 0.0;
  for (const PreparedRoot& root : roots) {
    PreparedRoot swapped = root;
    std::swap(swapped.prepared.direct, swapped.prepared.reflected);
    for (int action = 0; action < kBoardSize; ++action) {
      if (!root.outcome.label.legal[action]) continue;
      const auto ordinary = forward(model, root, action).heads;
      const auto reflected = forward(model, swapped, action).heads;
      for (int head = 0; head < kHeads; ++head) {
        gap = std::max(gap, std::abs(ordinary[head] - reflected[head]));
      }
    }
  }
  return gap;
}

struct Projection {
  std::size_t pilot_index = 0;
  int game = -1;
  int move = -1;
  int legal_actions = 0;
  int occupied = 0;
  double pilot_seconds = 0.0;
  double projected_seconds = 0.0;
  bool passed = false;
};

void writeProjectionArtifact(const Options& options,
                             const Projection& projection) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write projection artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"scaled-long-outcome-nnue\",\n"
            "  \"status\":\"stopped-by-frozen-projection-cap\",\n"
            "  \"evidenceClass\":\"architecture-development-only\",\n"
            "  \"input\":{\"path\":\""
         << options.roots << "\",\"bytes\":" << kInputBytes
         << ",\"sha256\":\"" << kInputSha256
         << "\",\"fittingRoots\":" << kFittingRoots
         << ",\"oldHeldoutRoots\":" << kHeldoutRoots
         << ",\"newGameplaySeeds\":0},\n  \"projection\":{\"pilotIndex\":"
         << projection.pilot_index << ",\"game\":" << projection.game
         << ",\"moveInSourceGame\":" << projection.move
         << ",\"legalActions\":" << projection.legal_actions
         << ",\"occupiedCells\":" << projection.occupied
         << ",\"pilotSeconds\":" << projection.pilot_seconds
         << ",\"workers\":" << kWorkers
         << ",\"safetyMultiplier\":" << kProjectionSafety
         << ",\"projectedLabelSeconds\":" << projection.projected_seconds
         << ",\"hardCapSeconds\":" << kLabelWallCapSeconds
         << ",\"passed\":false},\n"
            "  \"conclusion\":\"projection exceeded the preregistered label cap; no corpus, model, gameplay, or proposal was produced\"\n}\n";
}

void writeFoldArray(std::ostream& output,
                    const std::array<FoldAudit, kFolds>& folds) {
  output << '[';
  for (int fold = 0; fold < kFolds; ++fold) {
    if (fold != 0) output << ',';
    output << "{\"fold\":" << fold << ",\"heldoutGamesModulo4\":"
           << fold << ",\"d2\":";
    writeEvaluation(output, folds[fold].d2);
    output << ",\"priorSmall\":";
    writeEvaluation(output, folds[fold].small);
    output << ",\"newLarge\":";
    writeEvaluation(output, folds[fold].large);
    output << '}';
  }
  output << ']';
}

void writeHalfArray(std::ostream& output, const ModelAudit& audit) {
  output << '[';
  for (int half = 0; half < 2; ++half) {
    if (half != 0) output << ',';
    output << "{\"half\":" << half << ",\"gameBegin\":"
           << half * (kHeldoutGames / 2) << ",\"gameEndExclusive\":"
           << (half + 1) * (kHeldoutGames / 2) << ",\"d2\":";
    writeEvaluation(output, audit.heldout_d2_halves[half]);
    output << ",\"priorSmall\":";
    writeEvaluation(output, audit.heldout_small_halves[half]);
    output << ",\"newLarge\":";
    writeEvaluation(output, audit.heldout_large_halves[half]);
    output << '}';
  }
  output << ']';
}

void writeArtifact(const Options& options, const Projection& projection,
                   const LabeledRange& fitting_labels,
                   const LabeledRange& heldout_labels,
                   const ModelAudit& audit, const FrozenGate& gate,
                   std::uint64_t small_bytes, std::uint64_t large_bytes,
                   std::uint64_t small_fingerprint,
                   std::uint64_t large_fingerprint,
                   double small_swap_gap, double large_swap_gap,
                   double label_seconds, double total_seconds,
                   bool resources_passed) {
  const std::string label_sha = sha256(readWholeFile(options.labels));
  const std::string small_sha = sha256(readWholeFile(options.small_checkpoint));
  const std::string large_sha = sha256(readWholeFile(options.large_checkpoint));
  const bool proposal = resources_passed && gate.passed;
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not write scaled audit artifact");
  output << std::setprecision(12)
         << "{\n  \"experiment\":\"scaled-long-outcome-nnue\",\n"
            "  \"status\":\"complete\",\n"
            "  \"evidenceClass\":\"architecture-development-only\",\n"
            "  \"claimBoundary\":\"all roots come from the preserved public-D4 fitting and already-burned old-heldout corpus; no gameplay result is claimed\",\n"
            "  \"input\":{\"path\":\""
         << options.roots << "\",\"bytes\":" << kInputBytes
         << ",\"sha256\":\"" << kInputSha256
         << "\",\"fittingGames\":" << kFittingGames
         << ",\"fittingRoots\":" << kFittingRoots
         << ",\"oldHeldoutGames\":" << kHeldoutGames
         << ",\"oldHeldoutRoots\":" << kHeldoutRoots
         << ",\"capacityVsPrior288Roots\":"
         << static_cast<double>(kFittingRoots) / prior::kTrainingRoots
         << ",\"atLeastFourTimesPriorFittingRoots\":true,"
            "\"newRootStates\":0,\"newGameplaySeeds\":0,"
            "\"reserved3d1SeedsRead\":0,\"reserved3d2SeedsRead\":0},\n"
            "  \"labelProtocol\":{\"policy\":\"fresh full-width exact public-D2 after each forced first sibling action\",\"levelBonus\":"
         << kHistoricalLevelBonus
         << ",\"scoreSemantics\":\"historical 7k Sequence-style; not five-drop Hardcore/Blitz score-calibrated\","
            "\"horizon\":"
         << kHorizon << ",\"scenarios\":" << kScenarios
         << ",\"commonRandomNumbersAcrossSiblings\":true,"
            "\"tapeSeedDomain\":"
         << kTapeSeedDomain << ",\"revealDomain\":" << kRevealDomain
         << ",\"visibleDomain\":" << kVisibleDomain
         << ",\"labelsPath\":\"" << options.labels
         << "\",\"labelsBytes\":" << fileBytes(options.labels)
         << ",\"labelsSha256\":\"" << label_sha << "\"},\n"
            "  \"projection\":{\"pilotSelection\":\"maximum legal actions, then occupied cells, then earliest fitting root\","
            "\"pilotIndex\":"
         << projection.pilot_index << ",\"game\":" << projection.game
         << ",\"moveInSourceGame\":" << projection.move
         << ",\"legalActions\":" << projection.legal_actions
         << ",\"occupiedCells\":" << projection.occupied
         << ",\"pilotSeconds\":" << projection.pilot_seconds
         << ",\"workers\":" << kWorkers
         << ",\"safetyMultiplier\":" << kProjectionSafety
         << ",\"projectedLabelSeconds\":" << projection.projected_seconds
         << ",\"hardCapSeconds\":" << kLabelWallCapSeconds
         << ",\"passed\":" << (projection.passed ? "true" : "false")
         << ",\"pilotReused\":true},\n"
            "  \"labelCost\":{\"fitting\":";
  writeLabelCost(output, fitting_labels.cost);
  output << ",\"oldHeldout\":";
  writeLabelCost(output, heldout_labels.cost);
  output << ",\"totalLabelWallSeconds\":" << label_seconds << "},\n"
            "  \"architecture\":{\"inputs\":"
         << kInputs
         << ",\"publicActionRelativeSparseInputs\":"
         << base::kFeatureCount
         << ",\"verticalLadderInputs\":3,\"heads\":[\"meanReturnResidual\",\"survival\",\"normalizedMeanClears\",\"downside\",\"variance\"],"
            "\"reflection\":\"shared direct/reflected accumulator ReLU outputs averaged exactly\","
            "\"priorSmallHidden\":"
         << kSmallHidden << ",\"newLargeHidden\":" << kLargeHidden
         << ",\"epochs\":" << kEpochs
         << ",\"learningRate\":" << kLearningRate
         << ",\"weightDecay\":" << kWeightDecay
         << ",\"selection\":\"four outer whole-game folds by game modulo four; fixed architecture and optimization\"},\n"
            "  \"fittingWholeGameCV\":{\"d2\":";
  writeEvaluation(output, audit.fitting_d2_cv);
  output << ",\"priorSmall\":";
  writeEvaluation(output, audit.fitting_small_cv);
  output << ",\"newLarge\":";
  writeEvaluation(output, audit.fitting_large_cv);
  output << ",\"folds\":";
  writeFoldArray(output, audit.folds);
  output << "},\n  \"oldHeldoutBurnedOnce\":{\"d2\":";
  writeEvaluation(output, audit.heldout_d2);
  output << ",\"priorSmall\":";
  writeEvaluation(output, audit.heldout_small);
  output << ",\"newLarge\":";
  writeEvaluation(output, audit.heldout_large);
  output << ",\"halves\":";
  writeHalfArray(output, audit);
  output << "},\n  \"frozenAcceptance\":{\"rankingThresholds\":{"
            "\"vsD2\":{\"top1Gain\":"
         << kTop1VsD2 << ",\"top2Gain\":" << kTop2VsD2
         << ",\"pairwiseGain\":" << kPairwiseVsD2
         << ",\"maximumRegretRatio\":" << kRegretRatioVsD2
         << "},\"vsPriorSmall\":{\"top1Gain\":" << kTop1VsSmall
         << ",\"top2Gain\":" << kTop2VsSmall
         << ",\"pairwiseGain\":" << kPairwiseVsSmall
         << ",\"maximumRegretRatio\":" << kRegretRatioVsSmall
         << "}},\"survivalAndDownsidePearsonGain\":"
         << kHeadCorrelationGain << ",\"gate\":";
  writeGate(output, gate);
  output << "},\n  \"checkpoints\":{\"priorSmall\":{\"path\":\""
         << options.small_checkpoint << "\",\"bytes\":" << small_bytes
         << ",\"sha256\":\"" << small_sha
         << "\",\"fingerprintFnv1a64\":\"0x" << std::hex
         << small_fingerprint << std::dec << "\"},\"newLarge\":{\"path\":\""
         << options.large_checkpoint << "\",\"bytes\":" << large_bytes
         << ",\"sha256\":\"" << large_sha
         << "\",\"fingerprintFnv1a64\":\"0x" << std::hex
         << large_fingerprint << std::dec
         << "\"},\"perCheckpointLimitBytes\":"
         << kMaximumCheckpointBytes << "},\n"
            "  \"implementation\":{\"trainingSeconds\":"
         << audit.training_seconds << ",\"totalSeconds\":" << total_seconds
         << ",\"smallReflectionSwapGap\":" << small_swap_gap
         << ",\"largeReflectionSwapGap\":" << large_swap_gap
         << ",\"peakRssBytes\":" << prior::peakRssBytes()
         << ",\"rssLimitBytes\":" << kMaximumRssBytes
         << ",\"resourceChecksPassed\":"
         << (resources_passed ? "true" : "false")
         << "},\n  \"nextExperimentProposal\":";
  if (proposal) {
    output << "{\"recommended\":true,\"executed\":false,"
              "\"candidate\":\"48-hidden reflection-exact long-outcome NNUE residual over exact D2\","
              "\"requiredNextStep\":\"freeze a new disjoint gameplay protocol and seed family before measuring score or survival\"}";
  } else {
    output << "{\"recommended\":false,\"executed\":false,"
              "\"reason\":\"the larger model did not satisfy every frozen CV, burned-heldout, stability, head-correlation, and resource gate\"}";
  }
  output << ",\n  \"conclusion\":\""
         << (proposal
                 ? "the sample-bottleneck hypothesis passed architecture-development gates; proposal only, with no gameplay performed"
                 : "the sample-bottleneck hypothesis is rejected; exact D2 remains the deployment anchor")
         << "\"\n}\n";
}

int run(const Options& options, std::ostream& report) {
  const auto started = Clock::now();
  const RootCorpus corpus = loadRoots(options.roots);
  const std::size_t pilot_index = selectPilot(corpus.fitting);
  const base::RootLabel& pilot_source = corpus.fitting[pilot_index];
  const auto label_started = Clock::now();
  const auto label_deadline = label_started +
      std::chrono::duration_cast<Clock::duration>(
          std::chrono::duration<double>(kLabelWallCapSeconds));
  const OutcomeRoot pilot = evaluateRoot(pilot_source, label_deadline);
  Projection projection;
  projection.pilot_index = pilot_index;
  projection.game = pilot_source.game;
  projection.move = pilot_source.move_in_game;
  projection.legal_actions = legalActions(pilot_source.board);
  projection.occupied = occupiedCells(pilot_source.board);
  projection.pilot_seconds = pilot.wall_seconds;
  projection.projected_seconds =
      pilot.wall_seconds * static_cast<double>(kTotalRoots) /
      static_cast<double>(kWorkers) * kProjectionSafety;
  projection.passed =
      projection.projected_seconds <= kLabelWallCapSeconds;
  report << std::fixed << std::setprecision(3)
         << "SCALED_LONG_NNUE_PROJECTION {\"pilotIndex\":" << pilot_index
         << ",\"pilotSeconds\":" << projection.pilot_seconds
         << ",\"projectedLabelSeconds\":" << projection.projected_seconds
         << ",\"capSeconds\":" << kLabelWallCapSeconds
         << ",\"passed\":" << (projection.passed ? "true" : "false")
         << "}\n";
  if (!projection.passed) {
    writeProjectionArtifact(options, projection);
    return 0;
  }
  if (prior::peakRssBytes() > kMaximumRssBytes) {
    throw std::runtime_error("scaled pilot exceeded RSS cap");
  }

  const LabeledRange fitting_labels = generateRange(
      corpus.fitting, "fitting", label_deadline,
      std::pair<std::size_t, OutcomeRoot>{pilot_index, pilot});
  const LabeledRange heldout_labels = generateRange(
      corpus.heldout, "old-heldout-burned", label_deadline);
  const double label_seconds =
      std::chrono::duration<double>(Clock::now() - label_started).count();
  if (label_seconds > kLabelWallCapSeconds) {
    throw std::runtime_error("scaled labels exceeded wall cap");
  }
  writeLabels(options, fitting_labels, heldout_labels);
  const std::vector<PreparedRoot> fitting =
      prepareRange(fitting_labels.roots);
  const std::vector<PreparedRoot> heldout =
      prepareRange(heldout_labels.roots);
  const ModelAudit audit = auditModels(fitting, heldout);
  const FrozenGate gate = applyFrozenGate(audit);

  writeCheckpoint(options.small_checkpoint, audit.final_small);
  writeCheckpoint(options.large_checkpoint, audit.final_large);
  const NeuralModel restored_small = readCheckpoint(options.small_checkpoint);
  const NeuralModel restored_large = readCheckpoint(options.large_checkpoint);
  const std::uint64_t small_fingerprint = modelFingerprint(restored_small);
  const std::uint64_t large_fingerprint = modelFingerprint(restored_large);
  if (small_fingerprint != modelFingerprint(audit.final_small) ||
      large_fingerprint != modelFingerprint(audit.final_large)) {
    throw std::runtime_error("scaled checkpoint roundtrip failed");
  }
  const std::uint64_t small_bytes = fileBytes(options.small_checkpoint);
  const std::uint64_t large_bytes = fileBytes(options.large_checkpoint);
  const double small_swap_gap = reflectionSwapGap(restored_small, heldout);
  const double large_swap_gap = reflectionSwapGap(restored_large, heldout);
  const double total_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  const bool resources_passed =
      label_seconds <= kLabelWallCapSeconds &&
      small_bytes <= kMaximumCheckpointBytes &&
      large_bytes <= kMaximumCheckpointBytes &&
      prior::peakRssBytes() <= kMaximumRssBytes &&
      small_swap_gap <= 1.0e-12 && large_swap_gap <= 1.0e-12;
  writeArtifact(options, projection, fitting_labels, heldout_labels, audit,
                gate, small_bytes, large_bytes, small_fingerprint,
                large_fingerprint, small_swap_gap, large_swap_gap,
                label_seconds, total_seconds, resources_passed);
  report << std::fixed << std::setprecision(6)
         << "SCALED_LONG_NNUE_AUDIT {\"status\":\"complete\","
            "\"cvD2Top1\":"
         << base::top1Rate(audit.fitting_d2_cv.ranking)
         << ",\"cvSmallTop1\":"
         << base::top1Rate(audit.fitting_small_cv.ranking)
         << ",\"cvLargeTop1\":"
         << base::top1Rate(audit.fitting_large_cv.ranking)
         << ",\"heldoutD2Top1\":"
         << base::top1Rate(audit.heldout_d2.ranking)
         << ",\"heldoutSmallTop1\":"
         << base::top1Rate(audit.heldout_small.ranking)
         << ",\"heldoutLargeTop1\":"
         << base::top1Rate(audit.heldout_large.ranking)
         << ",\"stableFolds\":" << gate.stable_folds
         << ",\"gatePassed\":" << (gate.passed ? "true" : "false")
         << ",\"resourcesPassed\":"
         << (resources_passed ? "true" : "false")
         << ",\"gameplayExecuted\":false,\"artifact\":\""
         << options.output << "\"}\n";
  return 0;
}

PreparedRoot syntheticPreparedRoot() {
  const State state = base::fair::frozen::fixtureState(
      base::fair::frozen::kTypeScriptFixtures[1]);
  OutcomeRoot outcome;
  outcome.label.board = state.board;
  outcome.label.next_disc = state.next_disc;
  outcome.label.moves_remaining = state.moves_remaining;
  outcome.label.q.fill(-std::numeric_limits<double>::infinity());
  outcome.pre_ladder = legacy::verticalLadderFeatures(state.board).energy;
  int best_action = -1;
  double best_value = -std::numeric_limits<double>::infinity();
  for (int action = 0; action < kBoardSize; ++action) {
    if (!isLegal(state.board, action)) continue;
    outcome.label.legal[action] = true;
    const double value = static_cast<double>((action + 2) * (action + 1));
    outcome.label.q[action] = value;
    outcome.actions[action].mean_return = value;
    outcome.actions[action].survival = 0.25 + 0.1 * action;
    outcome.actions[action].mean_clears = 0.05 * action;
    outcome.actions[action].downside = 0.08 * action;
    outcome.actions[action].variance = 0.03 * (kBoardSize - action);
    outcome.actions[action].expected_post_ladder =
        outcome.pre_ladder + 0.02 * action;
    if (value > best_value) {
      best_value = value;
      best_action = action;
    }
  }
  outcome.label.labeled_action = best_action;
  return {outcome, base::prepare(outcome.label)};
}

Evaluation syntheticEvaluation(int top1, int top2, double pairwise,
                               double regret, bool strong_heads) {
  Evaluation result;
  result.ranking.roots = 100;
  result.ranking.top1 = top1;
  result.ranking.top2 = top2;
  result.ranking.pairs = 100;
  result.ranking.pairwise_credit = 100.0 * pairwise;
  result.ranking.normalized_regret = 100.0 * regret;
  for (int row = 0; row < 4; ++row) {
    std::array<double, kHeads> target{};
    std::array<double, kHeads> prediction{};
    target[kSurvival] = row;
    target[kDownside] = row;
    if (strong_heads) {
      prediction[kSurvival] = row;
      prediction[kDownside] = row;
    } else {
      prediction[kSurvival] = row % 2;
      prediction[kDownside] = row % 2;
    }
    observeHeadRow(result.heads, prediction, target);
  }
  return result;
}

bool selfTest(const Options& options, std::ostream& output) {
  const bool inherited = base::fair::selfTest(output);
  const bool sha =
      sha256("abc") ==
      "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
  const PreparedRoot prepared = syntheticPreparedRoot();
  std::vector<PreparedRoot> tiny(8, prepared);
  for (int index = 0; index < static_cast<int>(tiny.size()); ++index) {
    tiny[index].outcome.label.game = index;
  }
  const auto all = [](const PreparedRoot&) { return true; };
  const NeuralModel first =
      trainModel(tiny, all, kSmallHidden, 2, 0x1234'5678u);
  const NeuralModel repeat =
      trainModel(tiny, all, kSmallHidden, 2, 0x1234'5678u);
  const bool deterministic =
      modelFingerprint(first) == modelFingerprint(repeat);
  writeCheckpoint(options.small_checkpoint, first);
  const NeuralModel restored = readCheckpoint(options.small_checkpoint);
  const bool checkpoint =
      modelFingerprint(first) == modelFingerprint(restored) &&
      fileBytes(options.small_checkpoint) <= kMaximumCheckpointBytes;
  const double swap_gap = reflectionSwapGap(restored, tiny);
  const std::uint64_t calculated_large_bytes =
      sizeof(CheckpointHeader) +
      sizeof(float) * static_cast<std::uint64_t>(
          kInputs * kLargeHidden + kLargeHidden + kHeads * kLargeHidden +
          kHeads + kInputs);
  const bool resources =
      calculated_large_bytes <= kMaximumCheckpointBytes &&
      first.input_weights.size() ==
          static_cast<std::size_t>(kInputs * kSmallHidden);

  const Evaluation d2 = syntheticEvaluation(20, 40, 0.50, 0.40, false);
  const Evaluation small = syntheticEvaluation(25, 45, 0.52, 0.35, false);
  const Evaluation large = syntheticEvaluation(28, 47, 0.54, 0.30, true);
  ModelAudit passing;
  passing.fitting_d2_cv = d2;
  passing.fitting_small_cv = small;
  passing.fitting_large_cv = large;
  passing.heldout_d2 = d2;
  passing.heldout_small = small;
  passing.heldout_large = large;
  for (int fold = 0; fold < kFolds; ++fold) {
    passing.folds[fold] = {d2, small, large};
  }
  for (int half = 0; half < 2; ++half) {
    passing.heldout_d2_halves[half] = d2;
    passing.heldout_small_halves[half] = small;
    passing.heldout_large_halves[half] = large;
  }
  const FrozenGate positive_gate = applyFrozenGate(passing);
  ModelAudit failing = passing;
  failing.fitting_large_cv = d2;
  const FrozenGate negative_gate = applyFrozenGate(failing);
  const bool gate_wiring = positive_gate.passed && !negative_gate.passed;
  const bool protocol =
      kFittingRoots == 1'508 && kHeldoutRoots == 465 &&
      kFittingRoots >= 4 * prior::kTrainingRoots && kFolds == 4 &&
      kScenarios == 7 && kHorizon == 25 && kWorkers == 4 &&
      kInputs == 1'650 && kSmallHidden == 12 && kLargeHidden == 48 &&
      kEpochs == 40 && kTapeSeedDomain == 0x5343'4c45u &&
      kRevealDomain == 0x5352'564cu && kVisibleDomain == 0x5356'4953u;
  const bool passed = inherited && sha && deterministic && checkpoint &&
                      swap_gap == 0.0 && resources && gate_wiring && protocol;
  output << std::setprecision(12)
         << "SCALED_LONG_NNUE_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"inheritedEngine\":" << (inherited ? "true" : "false")
         << ",\"sha256\":" << (sha ? "true" : "false")
         << ",\"deterministicTraining\":"
         << (deterministic ? "true" : "false")
         << ",\"checkpointRoundtrip\":"
         << (checkpoint ? "true" : "false")
         << ",\"reflectionSwapGap\":" << swap_gap
         << ",\"largeCheckpointBound\":"
         << (resources ? "true" : "false")
         << ",\"gateWiring\":" << (gate_wiring ? "true" : "false")
         << ",\"protocol\":" << (protocol ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::scaled_long_outcome_nnue

#ifndef DROP7_SCALED_LONG_OUTCOME_NNUE_LIBRARY
int main(int argc, char** argv) {
  try {
    if (argc >= 2 && std::string_view(argv[1]) == "--self-test") {
      const auto options =
          drop7::scaled_long_outcome_nnue::parseOptions(argc, argv, 2);
      return drop7::scaled_long_outcome_nnue::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc >= 2 && std::string_view(argv[1]) == "--run") {
      const auto options =
          drop7::scaled_long_outcome_nnue::parseOptions(argc, argv, 2);
      return drop7::scaled_long_outcome_nnue::run(options, std::cout);
    }
    std::cerr << "usage: drop7_scaled_long_outcome_nnue "
                 "--self-test | --run "
                 "[--roots PATH --output PATH --labels PATH "
                 "--small-checkpoint PATH --large-checkpoint PATH]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_scaled_long_outcome_nnue: " << error.what() << '\n';
    return 1;
  }
}
#endif
