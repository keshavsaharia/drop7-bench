#define main drop7_constructive_spectrum_frozen_entrypoint
#include "../constructive-spectrum/constructive-spectrum.cpp"
#undef main

#include <map>
#include <queue>
#include <unordered_set>

// Learns public root-action values from labels that compare every legal
// sibling under common h100 public tapes.  The labels directly target action
// ranking.
namespace drop7::panel_value_nnue {

namespace constructive = drop7::constructive_spectrum;
namespace fair = drop7::fair_only_horizon;
namespace detail = drop7::cfpi::detail;

using Clock = std::chrono::steady_clock;
using PublicState = constructive::PublicState;

constexpr std::uint32_t kOriginSeedStart = 0x3d6c'1000u;
constexpr std::uint32_t kOriginSeedEndExclusive = 0x3d6c'1400u;
constexpr int kOriginGames = 1'024;
constexpr int kD1OriginGames = 512;
constexpr int kConstructiveOriginGames = 512;
constexpr int kMaximumRootsPerGame = 8;
constexpr std::array<int, kMaximumRootsPerGame> kMilestones{{
    5, 10, 15, 20, 25, 30, 40, 50,
}};
constexpr int kTrainingPerPolicy = 384;
constexpr int kHeldoutPerPolicy = 128;
constexpr std::uint32_t kSplitDomain = 0x5041'4e53u;

constexpr int kLabelScenarios = 15;
constexpr int kLabelHorizon = 100;
constexpr std::uint32_t kTapeDomain = 0x5041'4e4cu;
constexpr std::uint32_t kTapeDiscDomain = 0x5044'4953u;
constexpr std::uint32_t kTapeRevealDomain = 0x5052'564cu;
constexpr std::uint32_t kEventMultiplier = 0x9e37'79b9u;
constexpr int kReservoirPerTrainingRoot = 42;
constexpr std::size_t kReservoirCap = 262'144;
constexpr std::uint64_t kReservoirDomain = 0x5245'5345'5256'4f49ull;

constexpr int kPreflightRoots = 64;
constexpr double kProjectionSafety = 1.35;
constexpr double kProjectionLimitSeconds = 75.0 * 60.0;
constexpr double kWallLimitSeconds = 75.0 * 60.0;
constexpr std::uint64_t kRssLimitBytes = 256ull * 1024ull * 1024ull;
constexpr int kMaximumThreads = 8;

constexpr std::uint32_t kScreenSeedStart = 0x3d6c'8000u;
constexpr std::uint32_t kScreenSeedEndExclusive = 0x3d6c'8020u;
constexpr int kScreenGames = 32;
constexpr int kScreenMaximumMoves = 1'000;
constexpr double kD1RootWindow = 2'500.0;
constexpr int kMaximumScreenActions = 2;
constexpr int kRootScenarios = 7;
constexpr std::uint32_t kRootSuccessorDomain = 0x504e'5254u;

constexpr int kHidden = 96;
constexpr int kHeads = 5;
constexpr int kEpochs = 16;
constexpr int kBatchSize = 512;
constexpr float kLearningRate = 0.001f;
constexpr float kWeightDecay = 1.0e-5f;
constexpr float kGradientNorm = 2.0f;
constexpr float kDownsideQuantile = 0.20f;
constexpr std::uint32_t kNetworkSeed = 0x504e'4e31u;
constexpr std::uint32_t kShuffleDomain = 0x504e'5348u;

constexpr double kRequiredTopOne = 0.30;
constexpr double kRequiredPairwise = 0.58;
constexpr double kMaximumNormalizedRegret = 0.30;
constexpr double kRequiredPairwiseGain = 0.03;
constexpr double kRequiredRegretRatio = 0.90;
constexpr double kScreenScoreRatio = 1.20;
constexpr double kScreenMoveRatio = 1.20;
constexpr double kScreenFlowGain = 0.05;
constexpr int kScreenJointWins = 20;

constexpr std::uint64_t kCheckpointMagic = 0x4437'504e'4e55'4531ull;
constexpr std::uint32_t kCheckpointVersion = 1;

static_assert(kLevelBonus == 17'000);
static_assert(kOriginSeedEndExclusive - kOriginSeedStart == kOriginGames);
static_assert(kD1OriginGames + kConstructiveOriginGames == kOriginGames);
static_assert(kTrainingPerPolicy + kHeldoutPerPolicy == 512);
static_assert(kLabelScenarios == 15 && kLabelHorizon == 100);
static_assert(kReservoirPerTrainingRoot *
                  (kTrainingPerPolicy * 2 * kMaximumRootsPerGame) <=
              kReservoirCap);
static_assert(kScreenSeedEndExclusive - kScreenSeedStart == kScreenGames);
static_assert(kRootScenarios == kBoardSize);

std::uint64_t peakRssBytes() { return constructive::peakRssBytes(); }

void enforceRss() {
  if (peakRssBytes() > kRssLimitBytes) {
    throw std::runtime_error("panel NNUE exceeded 256 MiB RSS");
  }
}

struct Deadline {
  Clock::time_point started = Clock::now();
  double seconds() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
  }
  void check() const {
    if (seconds() > kWallLimitSeconds) {
      throw std::runtime_error("panel NNUE exceeded 75 minute wall cap");
    }
  }
};

enum class SeedUse : std::uint8_t { kOrigin, kScreen };

bool allowedSeed(std::uint32_t seed, SeedUse use) {
  const std::uint32_t begin =
      use == SeedUse::kOrigin ? kOriginSeedStart : kScreenSeedStart;
  const std::uint32_t end = use == SeedUse::kOrigin
                                ? kOriginSeedEndExclusive
                                : kScreenSeedEndExclusive;
  const std::uint8_t prefix = static_cast<std::uint8_t>(seed >> 24u);
  return seed >= begin && seed < end && prefix != 0x4d && prefix != 0x7d &&
         prefix != 0xd7;
}

void requireSeed(std::uint32_t seed, SeedUse use) {
  if (!allowedSeed(seed, use)) {
    throw std::invalid_argument("seed outside frozen panel-NNUE lanes");
  }
}

std::uint64_t mix64(std::uint64_t value) {
  value ^= value >> 30u;
  value *= 0xbf58'476d'1ce4'e5b9ull;
  value ^= value >> 27u;
  value *= 0x94d0'49bb'1331'11ebull;
  return value ^ (value >> 31u);
}

PublicState canonicalPublic(const PublicState& source) {
  bool ignored = false;
  return constructive::canonicalPublic(source, ignored);
}

std::uint64_t publicHash(const PublicState& source) {
  const PublicState state = canonicalPublic(source);
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const std::uint8_t cell : state.board) {
    hash ^= static_cast<std::uint64_t>(cell + 1u);
    hash *= 0x0000'0100'0000'01b3ull;
  }
  hash ^= state.next_disc;
  hash *= 0x0000'0100'0000'01b3ull;
  hash ^= static_cast<std::uint64_t>(state.moves_remaining + 1u);
  hash *= 0x0000'0100'0000'01b3ull;
  return mix64(hash);
}

std::string publicKey(const PublicState& source) {
  const PublicState state = canonicalPublic(source);
  std::string key;
  key.reserve(kCellCount + 2);
  for (const std::uint8_t token : state.board) {
    key.push_back(static_cast<char>(token));
  }
  key.push_back(static_cast<char>(state.next_disc));
  key.push_back(static_cast<char>(state.moves_remaining));
  return key;
}

enum class OriginPolicy : std::uint8_t { kFairD1, kConstructive };

std::string_view policyName(OriginPolicy policy) {
  return policy == OriginPolicy::kFairD1 ? "fair-d1"
                                         : "constructive-spectrum";
}

struct SplitTable {
  std::array<bool, kOriginGames> heldout{};
};

SplitTable buildSplit() {
  SplitTable result;
  for (int policy = 0; policy < 2; ++policy) {
    std::vector<std::pair<std::uint32_t, int>> order;
    order.reserve(512);
    const int base = policy * 512;
    for (int offset = 0; offset < 512; ++offset) {
      const std::uint32_t seed =
          kOriginSeedStart + static_cast<std::uint32_t>(base + offset);
      order.push_back({mix32(seed ^ kSplitDomain), base + offset});
    }
    std::sort(order.begin(), order.end());
    for (int rank = 0; rank < kHeldoutPerPolicy; ++rank) {
      result.heldout[order[rank].second] = true;
    }
  }
  return result;
}

struct Root {
  std::uint32_t origin_seed = 0;
  OriginPolicy policy = OriginPolicy::kFairD1;
  int milestone = 0;
  PublicState state{};
  bool heldout = false;
};

std::vector<Root> collectGameRoots(std::uint32_t seed, int roots_per_game,
                                   bool heldout, const Deadline& deadline) {
  requireSeed(seed, SeedUse::kOrigin);
  const int game_index = static_cast<int>(seed - kOriginSeedStart);
  const OriginPolicy policy = game_index < kD1OriginGames
                                  ? OriginPolicy::kFairD1
                                  : OriginPolicy::kConstructive;
  State state = initialHeadlessState(seed);
  std::vector<Root> result;
  result.reserve(roots_per_game);
  int milestone = 0;
  while (!state.game_over && milestone < roots_per_game) {
    if ((state.moves_played & 7) == 0) deadline.check();
    if (state.moves_played == kMilestones[milestone]) {
      int legal_count = 0;
      legalColumns(state.board, legal_count);
      if (legal_count >= 2) {
        result.push_back({seed, policy, kMilestones[milestone],
                          canonicalPublic(constructive::publicState(state)),
                          heldout});
      }
      ++milestone;
      if (milestone >= roots_per_game) break;
    }
    const PublicState public_state = constructive::publicState(state);
    const int action =
        policy == OriginPolicy::kFairD1
            ? constructive::chooseFairD1(public_state)
            : constructive::chooseAction(public_state).action;
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("panel roll-in selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("panel roll-in transition failed");
    }
  }
  return result;
}

struct PublicTape {
  std::uint32_t seed = 0;
  int move = 0;

  std::uint8_t nextDiscForMove(int move_index) const {
    const std::uint32_t bits =
        mix32(seed ^ kTapeDiscDomain ^
              (static_cast<std::uint32_t>(move_index + 1) *
               kEventMultiplier));
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32u) + 1u);
  }

  std::uint8_t revealDisc(int event) const {
    const std::uint32_t bits =
        mix32(seed ^ kTapeRevealDomain ^
              (static_cast<std::uint32_t>(move + 1) * 0x85eb'ca6bu) ^
              (static_cast<std::uint32_t>(event + 1) * 0xc2b2'ae35u));
    return static_cast<std::uint8_t>(
        ((static_cast<std::uint64_t>(bits) * kBoardSize) >> 32u) + 1u);
  }
};

struct PublicMoveRandom {
  const PublicTape& tape;
  int event = 0;
  std::uint8_t nextDisc() { return tape.revealDisc(event++); }
};

bool playTapeMove(State& state, int action, PublicTape& tape,
                  MoveResult& result) {
  PublicMoveRandom random{tape, 0};
  if (!detail::playMoveSampled(state, action, random, result)) return false;
  ++tape.move;
  state = result.state;
  if (!state.game_over) state.next_disc = tape.nextDiscForMove(tape.move);
  result.state = state;
  return true;
}

std::uint32_t tapeSeed(const PublicState& root, int scenario) {
  if (scenario < 0 || scenario >= kLabelScenarios) {
    throw std::invalid_argument("invalid label scenario");
  }
  return static_cast<std::uint32_t>(mix64(
      publicHash(root) ^ kTapeDomain ^
      (static_cast<std::uint64_t>(scenario + 1) *
       0x9e37'79b9'7f4a'7c15ull)));
}

struct ValueExample {
  PublicState state{};
  float mean_return = 0.0f;
  float survival = 0.0f;
  float clears = 0.0f;
  float reveals = 0.0f;
  float downside_return = 0.0f;
  std::uint64_t priority = 0;
  std::uint32_t origin_seed = 0;
};

struct ActionLabel {
  int action = -1;
  double mean_return = 0.0;
  double survival = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double downside_return = 0.0;
};

struct Panel {
  Root root{};
  std::vector<ActionLabel> actions;
  std::vector<ValueExample> reservoir;
  std::uint64_t transitions = 0;
  std::uint64_t d1_work = 0;
};

struct Snapshot {
  PublicState state{};
  int moves = 0;
  std::int64_t score = 0;
  int clears = 0;
  int reveals = 0;
  int step = 0;
};

struct ScenarioOutcome {
  double value = 0.0;
  int moves = 0;
  std::int64_t score = 0;
  int clears = 0;
  int reveals = 0;
  bool survived = false;
  std::vector<ValueExample> examples;
  std::uint64_t transitions = 0;
  std::uint64_t d1_work = 0;
};

std::uint64_t reservoirPriority(const Root& root, int action, int scenario,
                                int step) {
  return mix64(kReservoirDomain ^ publicHash(root.state) ^
               (static_cast<std::uint64_t>(action + 1) << 48u) ^
               (static_cast<std::uint64_t>(scenario + 1) << 32u) ^
               static_cast<std::uint64_t>(step + 1));
}

ScenarioOutcome replayAction(const Root& root, int action, int scenario,
                             bool collect_examples,
                             const Deadline& deadline) {
  State state = constructive::materialize(root.state);
  state.score = 0;
  state.level = 1;
  state.moves_played = 0;
  PublicTape tape{tapeSeed(root.state, scenario), 0};
  ScenarioOutcome result;
  std::vector<Snapshot> snapshots;
  snapshots.reserve(kLabelHorizon);
  MoveResult move;
  if (!playTapeMove(state, action, tape, move)) {
    throw std::runtime_error("forced panel action failed");
  }
  ++result.transitions;
  int clears = 0;
  int reveals = 0;
  for (const Wave& wave : move.waves) {
    clears += wave.cleared;
    reveals += wave.revealed;
  }
  int moves = 1;
  if (!state.game_over && collect_examples) {
    snapshots.push_back({canonicalPublic(constructive::publicState(state)),
                         moves, state.score, clears, reveals, moves});
  }
  while (!state.game_over && moves < kLabelHorizon) {
    if ((moves & 31) == 0) deadline.check();
    const PublicState public_state = constructive::publicState(state);
    fair::SearchContext context;
    const fair::RootEvaluation decision =
        fair::rootDecision(constructive::materialize(public_state), 1,
                           context);
    if (decision.action < 0 || context.work > 70 || !context.cache.empty()) {
      throw std::runtime_error("panel h100 D1 continuation incomplete");
    }
    result.d1_work += context.work;
    if (!playTapeMove(state, decision.action, tape, move)) {
      throw std::runtime_error("panel h100 transition failed");
    }
    ++result.transitions;
    ++moves;
    for (const Wave& wave : move.waves) {
      clears += wave.cleared;
      reveals += wave.revealed;
    }
    if (!state.game_over && collect_examples) {
      snapshots.push_back({canonicalPublic(constructive::publicState(state)),
                           moves, state.score, clears, reveals, moves});
    }
  }
  result.moves = moves;
  result.score = state.score;
  result.clears = clears;
  result.reveals = reveals;
  result.survived = !state.game_over && moves == kLabelHorizon;
  result.value = static_cast<double>(moves) +
                 static_cast<double>(state.score) / 17'000.0;
  if (collect_examples) {
    result.examples.reserve(snapshots.size());
    for (const Snapshot& snapshot : snapshots) {
      const int remaining_moves = moves - snapshot.moves;
      const std::int64_t remaining_score = state.score - snapshot.score;
      const float remaining_return = static_cast<float>(
          remaining_moves + static_cast<double>(remaining_score) / 17'000.0);
      result.examples.push_back(
          {snapshot.state,
           remaining_return,
           result.survived ? 1.0f : 0.0f,
           static_cast<float>(clears - snapshot.clears),
           static_cast<float>(reveals - snapshot.reveals),
           remaining_return,
           reservoirPriority(root, action, scenario, snapshot.step),
           root.origin_seed});
    }
  }
  return result;
}

struct ReservoirCompare {
  bool operator()(const ValueExample& left,
                  const ValueExample& right) const {
    return left.priority < right.priority;
  }
};

Panel labelPanel(const Root& root, bool collect_examples,
                 const Deadline& deadline) {
  Panel result;
  result.root = root;
  State canonical = constructive::materialize(root.state);
  std::priority_queue<ValueExample, std::vector<ValueExample>,
                      ReservoirCompare>
      reservoir;
  for (const int action : constructive::kColumnOrder) {
    if (!isLegal(canonical.board, action)) continue;
    ActionLabel label;
    label.action = action;
    std::array<double, kLabelScenarios> returns{};
    for (int scenario = 0; scenario < kLabelScenarios; ++scenario) {
      ScenarioOutcome outcome = replayAction(
          root, action, scenario, collect_examples, deadline);
      returns[scenario] = outcome.value;
      label.mean_return += outcome.value / kLabelScenarios;
      label.survival += static_cast<double>(outcome.survived) /
                        kLabelScenarios;
      label.clears += static_cast<double>(outcome.clears) / kLabelScenarios;
      label.reveals += static_cast<double>(outcome.reveals) / kLabelScenarios;
      result.transitions += outcome.transitions;
      result.d1_work += outcome.d1_work;
      if (collect_examples) {
        for (ValueExample& example : outcome.examples) {
          if (reservoir.size() < kReservoirPerTrainingRoot) {
            reservoir.push(std::move(example));
          } else if (example.priority < reservoir.top().priority) {
            reservoir.pop();
            reservoir.push(std::move(example));
          }
        }
      }
    }
    std::sort(returns.begin(), returns.end());
    label.downside_return =
        (returns[0] + returns[1] + returns[2]) / 3.0;
    result.actions.push_back(label);
  }
  while (!reservoir.empty()) {
    result.reservoir.push_back(std::move(
        const_cast<ValueExample&>(reservoir.top())));
    reservoir.pop();
  }
  std::sort(result.reservoir.begin(), result.reservoir.end(),
            [](const ValueExample& a, const ValueExample& b) {
              return a.priority < b.priority;
            });
  return result;
}

struct Preflight {
  std::vector<std::uint32_t> opened_seeds;
  int roots = 0;
  std::uint64_t transitions = 0;
  double seconds = 0.0;
  double projected_seconds = 0.0;
  int roots_per_game = kMaximumRootsPerGame;
  std::uint64_t peak_rss_bytes = 0;
  bool passed = false;
};

Preflight runPreflight(const SplitTable& split, int threads,
                       const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  std::vector<Root> roots;
  for (int offset = 0; roots.size() < kPreflightRoots && offset < 64;
       ++offset) {
    for (const int base : {0, kD1OriginGames}) {
      if (roots.size() >= kPreflightRoots) break;
      const int game = base + offset;
      const std::uint32_t seed =
          kOriginSeedStart + static_cast<std::uint32_t>(game);
      std::vector<Root> game_roots = collectGameRoots(
          seed, kMaximumRootsPerGame, split.heldout[game], deadline);
      roots.insert(roots.end(), game_roots.begin(), game_roots.end());
    }
  }
  if (roots.size() < kPreflightRoots) {
    throw std::runtime_error("could not collect 64 preflight roots");
  }
  roots.resize(kPreflightRoots);
  Preflight result;
  std::unordered_set<std::uint32_t> seeds;
  for (const Root& root : roots) seeds.insert(root.origin_seed);
  result.opened_seeds.assign(seeds.begin(), seeds.end());
  std::sort(result.opened_seeds.begin(), result.opened_seeds.end());
  std::vector<Panel> panels(roots.size());
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min<int>(threads, roots.size());
       ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= static_cast<int>(roots.size())) return;
        panels[index] = labelPanel(roots[index], false, deadline);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.roots = static_cast<int>(roots.size());
  for (const Panel& panel : panels) result.transitions += panel.transitions;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  result.peak_rss_bytes = peakRssBytes();
  const double seconds_per_root = result.seconds / result.roots;
  result.projected_seconds =
      kProjectionSafety * seconds_per_root * kOriginGames *
      kMaximumRootsPerGame;
  if (peakRssBytes() > kRssLimitBytes) {
    result.passed = false;
    return result;
  }
  if (result.projected_seconds <= kProjectionLimitSeconds) {
    result.passed = true;
    return result;
  }
  const int affordable = static_cast<int>(std::floor(
      kProjectionLimitSeconds /
      (kProjectionSafety * seconds_per_root * kOriginGames)));
  result.roots_per_game = std::clamp(affordable, 1,
                                     kMaximumRootsPerGame);
  result.projected_seconds = kProjectionSafety * seconds_per_root *
                             kOriginGames * result.roots_per_game;
  result.passed = result.projected_seconds <= kProjectionLimitSeconds;
  return result;
}

struct RootCollection {
  std::vector<Root> training;
  std::vector<Root> heldout;
  int d1_training_games = 0;
  int d1_heldout_games = 0;
  int constructive_training_games = 0;
  int constructive_heldout_games = 0;
  int duplicate_training_roots = 0;
  int duplicate_heldout_roots = 0;
  int heldout_overlap_purged = 0;
};

RootCollection collectAllRoots(const SplitTable& split, int roots_per_game,
                               int threads, const Deadline& deadline) {
  std::vector<std::vector<Root>> by_game(kOriginGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, kOriginGames); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kOriginGames) return;
        const std::uint32_t seed =
            kOriginSeedStart + static_cast<std::uint32_t>(game);
        by_game[game] = collectGameRoots(seed, roots_per_game,
                                         split.heldout[game], deadline);
        if ((game & 63) == 63) {
          std::cerr << "panel roots collected " << game + 1 << '/'
                    << kOriginGames << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();

  RootCollection result;
  std::unordered_set<std::string> training_keys;
  std::unordered_set<std::string> heldout_keys;
  for (int game = 0; game < kOriginGames; ++game) {
    const bool heldout = split.heldout[game];
    const bool d1 = game < kD1OriginGames;
    if (d1 && heldout) ++result.d1_heldout_games;
    if (d1 && !heldout) ++result.d1_training_games;
    if (!d1 && heldout) ++result.constructive_heldout_games;
    if (!d1 && !heldout) ++result.constructive_training_games;
    for (Root& root : by_game[game]) {
      const std::string key = publicKey(root.state);
      if (heldout) {
        if (!heldout_keys.insert(key).second) {
          ++result.duplicate_heldout_roots;
          continue;
        }
        result.heldout.push_back(std::move(root));
      } else {
        if (!training_keys.insert(key).second) {
          ++result.duplicate_training_roots;
          continue;
        }
        result.training.push_back(std::move(root));
      }
    }
  }
  std::vector<Root> clean_heldout;
  clean_heldout.reserve(result.heldout.size());
  for (Root& root : result.heldout) {
    if (training_keys.contains(publicKey(root.state))) {
      ++result.heldout_overlap_purged;
    } else {
      clean_heldout.push_back(std::move(root));
    }
  }
  result.heldout = std::move(clean_heldout);
  if (result.d1_training_games != kTrainingPerPolicy ||
      result.d1_heldout_games != kHeldoutPerPolicy ||
      result.constructive_training_games != kTrainingPerPolicy ||
      result.constructive_heldout_games != kHeldoutPerPolicy ||
      result.training.empty() || result.heldout.empty()) {
    throw std::runtime_error("whole-origin 75/25 split failed");
  }
  enforceRss();
  return result;
}

struct Dataset {
  std::vector<Panel> training_panels;
  std::vector<Panel> heldout_panels;
  std::vector<ValueExample> training_examples;
  std::uint64_t transitions = 0;
  std::uint64_t d1_work = 0;
};

Dataset labelRoots(const RootCollection& roots, int threads,
                   const Deadline& deadline) {
  Dataset result;
  result.training_panels.resize(roots.training.size());
  result.heldout_panels.resize(roots.heldout.size());
  const int training_count = static_cast<int>(roots.training.size());
  const int total = training_count + static_cast<int>(roots.heldout.size());
  std::atomic<int> next{0};
  std::atomic<int> completed{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, total); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= total) return;
        if (index < training_count) {
          if (roots.training[index].heldout) {
            throw std::runtime_error("heldout origin entered reservoir");
          }
          result.training_panels[index] =
              labelPanel(roots.training[index], true, deadline);
        } else {
          const int heldout_index = index - training_count;
          if (!roots.heldout[heldout_index].heldout) {
            throw std::runtime_error("training origin entered heldout panel");
          }
          result.heldout_panels[heldout_index] =
              labelPanel(roots.heldout[heldout_index], false, deadline);
        }
        const int count = completed.fetch_add(1) + 1;
        if ((count & 127) == 0 || count == total) {
          std::cerr << "panel labels completed " << count << '/' << total
                    << '\n';
        }
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  for (Panel& panel : result.training_panels) {
    result.transitions += panel.transitions;
    result.d1_work += panel.d1_work;
    for (ValueExample& example : panel.reservoir) {
      if (example.origin_seed != panel.root.origin_seed ||
          panel.root.heldout) {
        throw std::runtime_error("reservoir origin-integrity failure");
      }
      result.training_examples.push_back(std::move(example));
    }
    panel.reservoir.clear();
    panel.reservoir.shrink_to_fit();
  }
  for (const Panel& panel : result.heldout_panels) {
    if (!panel.reservoir.empty()) {
      throw std::runtime_error("heldout internal state entered reservoir");
    }
    result.transitions += panel.transitions;
    result.d1_work += panel.d1_work;
  }
  if (result.training_examples.empty() ||
      result.training_examples.size() > kReservoirCap) {
    throw std::runtime_error("training reservoir violated fixed cap");
  }
  enforceRss();
  return result;
}

constexpr int kBoardTokens = 10;
constexpr int kBoardCategories = kCellCount * kBoardTokens;
constexpr int kNextCategories = kBoardSize;
constexpr int kPhaseCategories = kMovesPerLevel;
constexpr int kCategoryCount =
    kBoardCategories + kNextCategories + kPhaseCategories;
constexpr int kActiveCategories = kCellCount + 2;
constexpr int kMetricCount = constructive::kMetricCount;
static_assert(kCategoryCount == 502);
static_assert(kMetricCount == 29);

struct Normalizer {
  std::array<float, kMetricCount> metric_mean{};
  std::array<float, kMetricCount> metric_scale{};
  float return_mean = 0.0f;
  float return_scale = 1.0f;
  float clear_mean = 0.0f;
  float clear_scale = 1.0f;
  float reveal_mean = 0.0f;
  float reveal_scale = 1.0f;

  std::array<float, kMetricCount> metrics(const PublicState& state) const {
    const constructive::Metrics raw = constructive::extractMetrics(state);
    std::array<float, kMetricCount> result{};
    for (int index = 0; index < kMetricCount; ++index) {
      result[index] = std::clamp(
          (static_cast<float>(raw[index]) - metric_mean[index]) *
              metric_scale[index],
          -5.0f, 5.0f);
    }
    return result;
  }
};

Normalizer fitNormalizer(const std::vector<ValueExample>& examples) {
  if (examples.empty()) throw std::invalid_argument("empty normalizer corpus");
  Normalizer result;
  std::array<double, kMetricCount> sum{};
  std::array<double, kMetricCount> squares{};
  double return_sum = 0.0;
  double return_squares = 0.0;
  double clear_sum = 0.0;
  double clear_squares = 0.0;
  double reveal_sum = 0.0;
  double reveal_squares = 0.0;
  for (const ValueExample& example : examples) {
    const constructive::Metrics metrics =
        constructive::extractMetrics(example.state);
    for (int index = 0; index < kMetricCount; ++index) {
      sum[index] += metrics[index];
      squares[index] += metrics[index] * metrics[index];
    }
    return_sum += example.mean_return;
    return_squares += example.mean_return * example.mean_return;
    clear_sum += example.clears;
    clear_squares += example.clears * example.clears;
    reveal_sum += example.reveals;
    reveal_squares += example.reveals * example.reveals;
  }
  const double count = static_cast<double>(examples.size());
  for (int index = 0; index < kMetricCount; ++index) {
    const double mean = sum[index] / count;
    const double variance =
        std::max(1.0e-6, squares[index] / count - mean * mean);
    result.metric_mean[index] = static_cast<float>(mean);
    result.metric_scale[index] =
        static_cast<float>(1.0 / std::sqrt(variance));
  }
  const auto set_target = [count](double sum_value, double square_value,
                                  float& mean_output,
                                  float& scale_output) {
    const double mean = sum_value / count;
    const double variance =
        std::max(1.0e-4, square_value / count - mean * mean);
    mean_output = static_cast<float>(mean);
    scale_output = static_cast<float>(1.0 / std::sqrt(variance));
  };
  set_target(return_sum, return_squares, result.return_mean,
             result.return_scale);
  set_target(clear_sum, clear_squares, result.clear_mean,
             result.clear_scale);
  set_target(reveal_sum, reveal_squares, result.reveal_mean,
             result.reveal_scale);
  return result;
}

struct PreparedExample {
  PublicState state{};
  std::array<float, kMetricCount> metrics{};
  std::array<float, kHeads> targets{};
};

std::vector<PreparedExample> prepareExamples(
    const std::vector<ValueExample>& source, const Normalizer& normalizer) {
  std::vector<PreparedExample> result;
  result.reserve(source.size());
  for (const ValueExample& example : source) {
    PreparedExample prepared;
    prepared.state = example.state;
    prepared.metrics = normalizer.metrics(example.state);
    prepared.targets[0] =
        (example.mean_return - normalizer.return_mean) *
        normalizer.return_scale;
    prepared.targets[1] = example.survival;
    prepared.targets[2] =
        (example.clears - normalizer.clear_mean) * normalizer.clear_scale;
    prepared.targets[3] =
        (example.reveals - normalizer.reveal_mean) * normalizer.reveal_scale;
    prepared.targets[4] = prepared.targets[0];
    result.push_back(prepared);
  }
  return result;
}

struct Layout {
  static constexpr int embedding = 0;
  static constexpr int metric_weight =
      embedding + kCategoryCount * kHidden;
  static constexpr int bias = metric_weight + kMetricCount * kHidden;
  static constexpr int output_weight = bias + kHidden;
  static constexpr int output_bias = output_weight + kHeads * kHidden;
  static constexpr int count = output_bias + kHeads;
};
static_assert(Layout::count == 51'557);
static_assert(Layout::count * sizeof(float) + sizeof(Normalizer) <
              512ull * 1024ull);

struct OrientationCache {
  std::array<int, kActiveCategories> categories{};
  std::array<float, kHidden> pre{};
  std::array<float, kHidden> hidden{};
  std::array<float, kHeads> output{};
};

struct RawPrediction {
  std::array<float, kHeads> values{};
};

struct Prediction {
  double mean_return = 0.0;
  double survival = 0.0;
  double clears = 0.0;
  double reveals = 0.0;
  double downside_return = 0.0;

  bool operator==(const Prediction&) const = default;
};

class Network {
 public:
  explicit Network(std::uint32_t seed = kNetworkSeed)
      : parameters_(Layout::count, 0.0f), first_(Layout::count, 0.0f),
        second_(Layout::count, 0.0f) {
    Mulberry32 random(seed);
    const float embedding_radius = 0.035f;
    for (int index = Layout::embedding; index < Layout::metric_weight;
         ++index) {
      parameters_[index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * embedding_radius);
    }
    const float metric_radius =
        std::sqrt(6.0f / static_cast<float>(kMetricCount + kHidden));
    for (int index = Layout::metric_weight; index < Layout::bias; ++index) {
      parameters_[index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * metric_radius);
    }
    const float output_radius =
        std::sqrt(6.0f / static_cast<float>(kHidden + kHeads));
    for (int index = Layout::output_weight; index < Layout::output_bias;
         ++index) {
      parameters_[index] = static_cast<float>(
          (2.0 * random.nextUnit() - 1.0) * output_radius);
    }
  }

  OrientationCache forwardOrientation(
      const PublicState& state,
      const std::array<float, kMetricCount>& metrics) const {
    if (state.terminal) {
      throw std::invalid_argument("cannot evaluate terminal public state");
    }
    OrientationCache cache;
    int active = 0;
    for (int cell = 0; cell < kCellCount; ++cell) {
      const int token = state.board[cell];
      if (token < 0 || token >= kBoardTokens) {
        throw std::invalid_argument("invalid NNUE board token");
      }
      cache.categories[active++] = cell * kBoardTokens + token;
    }
    cache.categories[active++] =
        kBoardCategories + static_cast<int>(state.next_disc) - 1;
    cache.categories[active++] =
        kBoardCategories + kNextCategories +
        static_cast<int>(state.moves_remaining) - 1;
    if (active != kActiveCategories) {
      throw std::runtime_error("NNUE active category mismatch");
    }
    const float category_scale =
        1.0f / std::sqrt(static_cast<float>(kActiveCategories));
    const float metric_scale =
        1.0f / std::sqrt(static_cast<float>(kMetricCount));
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      float value = parameters_[Layout::bias + hidden];
      for (const int category : cache.categories) {
        value += category_scale *
                 parameters_[Layout::embedding + category * kHidden + hidden];
      }
      for (int metric = 0; metric < kMetricCount; ++metric) {
        value += metric_scale * metrics[metric] *
                 parameters_[Layout::metric_weight + metric * kHidden +
                             hidden];
      }
      cache.pre[hidden] = value;
      cache.hidden[hidden] = std::clamp(value, 0.0f, 1.0f);
    }
    for (int head = 0; head < kHeads; ++head) {
      float value = parameters_[Layout::output_bias + head];
      for (int hidden = 0; hidden < kHidden; ++hidden) {
        value += parameters_[Layout::output_weight + head * kHidden + hidden] *
                 cache.hidden[hidden];
      }
      cache.output[head] = value;
    }
    return cache;
  }

  RawPrediction raw(const PublicState& source,
                    const std::array<float, kMetricCount>& metrics) const {
    const PublicState state = canonicalPublic(source);
    const OrientationCache direct = forwardOrientation(state, metrics);
    const OrientationCache mirrored =
        forwardOrientation(constructive::mirror(state), metrics);
    RawPrediction result;
    for (int head = 0; head < kHeads; ++head) {
      result.values[head] =
          0.5f * (direct.output[head] + mirrored.output[head]);
    }
    return result;
  }

  Prediction predict(const PublicState& state,
                     const Normalizer& normalizer) const {
    const RawPrediction output = raw(state, normalizer.metrics(state));
    const auto sigmoid = [](float value) {
      if (value >= 0.0f) {
        const float exponential = std::exp(-value);
        return 1.0f / (1.0f + exponential);
      }
      const float exponential = std::exp(value);
      return exponential / (1.0f + exponential);
    };
    return {
        output.values[0] / normalizer.return_scale +
            normalizer.return_mean,
        sigmoid(output.values[1]),
        output.values[2] / normalizer.clear_scale + normalizer.clear_mean,
        output.values[3] / normalizer.reveal_scale + normalizer.reveal_mean,
        output.values[4] / normalizer.return_scale +
            normalizer.return_mean,
    };
  }

  std::vector<float> gradient() const {
    return std::vector<float>(Layout::count, 0.0f);
  }

  void accumulateOrientation(
      const PublicState& state,
      const std::array<float, kMetricCount>& metrics,
      const OrientationCache& cache,
      const std::array<float, kHeads>& output_derivative,
      std::vector<float>& gradient) const {
    std::array<float, kHidden> hidden_derivative{};
    for (int head = 0; head < kHeads; ++head) {
      gradient[Layout::output_bias + head] += output_derivative[head];
      for (int hidden = 0; hidden < kHidden; ++hidden) {
        const int index =
            Layout::output_weight + head * kHidden + hidden;
        gradient[index] += output_derivative[head] * cache.hidden[hidden];
        hidden_derivative[hidden] +=
            output_derivative[head] * parameters_[index];
      }
    }
    const float category_scale =
        1.0f / std::sqrt(static_cast<float>(kActiveCategories));
    const float metric_scale =
        1.0f / std::sqrt(static_cast<float>(kMetricCount));
    for (int hidden = 0; hidden < kHidden; ++hidden) {
      const float derivative =
          cache.pre[hidden] > 0.0f && cache.pre[hidden] < 1.0f
              ? hidden_derivative[hidden]
              : 0.0f;
      gradient[Layout::bias + hidden] += derivative;
      for (const int category : cache.categories) {
        gradient[Layout::embedding + category * kHidden + hidden] +=
            category_scale * derivative;
      }
      for (int metric = 0; metric < kMetricCount; ++metric) {
        gradient[Layout::metric_weight + metric * kHidden + hidden] +=
            metric_scale * metrics[metric] * derivative;
      }
    }
    static_cast<void>(state);
  }

  void accumulate(const PreparedExample& example, float inverse_batch,
                  std::vector<float>& gradient, double& loss) const {
    const PublicState state = canonicalPublic(example.state);
    const OrientationCache direct =
        forwardOrientation(state, example.metrics);
    const OrientationCache mirrored =
        forwardOrientation(constructive::mirror(state), example.metrics);
    std::array<float, kHeads> prediction{};
    for (int head = 0; head < kHeads; ++head) {
      prediction[head] =
          0.5f * (direct.output[head] + mirrored.output[head]);
    }
    std::array<float, kHeads> derivative{};
    for (const int head : {0, 2, 3}) {
      const float difference = prediction[head] - example.targets[head];
      loss += 0.5 * difference * difference;
      derivative[head] = difference * inverse_batch;
    }
    const float survival = 1.0f / (1.0f + std::exp(-prediction[1]));
    loss += -(example.targets[1] *
                  std::log(std::max(1.0e-6f, survival)) +
              (1.0f - example.targets[1]) *
                  std::log(std::max(1.0e-6f, 1.0f - survival)));
    derivative[1] = (survival - example.targets[1]) * inverse_batch;
    const float downside_error = prediction[4] - example.targets[4];
    loss += downside_error >= 0.0f
                ? (1.0f - kDownsideQuantile) * downside_error
                : -kDownsideQuantile * downside_error;
    derivative[4] =
        (downside_error >= 0.0f ? 1.0f - kDownsideQuantile
                                : -kDownsideQuantile) *
        inverse_batch;
    std::array<float, kHeads> half{};
    for (int head = 0; head < kHeads; ++head) {
      half[head] = 0.5f * derivative[head];
    }
    accumulateOrientation(state, example.metrics, direct, half, gradient);
    accumulateOrientation(constructive::mirror(state), example.metrics,
                          mirrored, half, gradient);
  }

  void apply(std::vector<float>& gradient) {
    double squared_norm = 0.0;
    for (int index = 0; index < Layout::count; ++index) {
      const bool decay = index < Layout::bias ||
                         (index >= Layout::output_weight &&
                          index < Layout::output_bias);
      if (decay) gradient[index] += kWeightDecay * parameters_[index];
      squared_norm += gradient[index] * gradient[index];
    }
    const double norm = std::sqrt(squared_norm);
    const float scale = norm > kGradientNorm
                            ? static_cast<float>(kGradientNorm / norm)
                            : 1.0f;
    ++step_;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1.0e-8f;
    const float correction1 = 1.0f - std::pow(beta1, static_cast<float>(step_));
    const float correction2 = 1.0f - std::pow(beta2, static_cast<float>(step_));
    for (int index = 0; index < Layout::count; ++index) {
      const float value = gradient[index] * scale;
      first_[index] = beta1 * first_[index] + (1.0f - beta1) * value;
      second_[index] =
          beta2 * second_[index] + (1.0f - beta2) * value * value;
      parameters_[index] -=
          kLearningRate * (first_[index] / correction1) /
          (std::sqrt(second_[index] / correction2) + epsilon);
      if (!std::isfinite(parameters_[index])) {
        throw std::runtime_error("non-finite panel NNUE parameter");
      }
    }
  }

  const std::vector<float>& parameters() const { return parameters_; }
  void setParameters(const std::vector<float>& source) {
    if (source.size() != parameters_.size()) {
      throw std::invalid_argument("panel NNUE parameter-count mismatch");
    }
    parameters_ = source;
    std::fill(first_.begin(), first_.end(), 0.0f);
    std::fill(second_.begin(), second_.end(), 0.0f);
    step_ = 0;
  }

 private:
  std::vector<float> parameters_;
  std::vector<float> first_;
  std::vector<float> second_;
  std::uint64_t step_ = 0;
};

using PublicEvaluator = Prediction (Network::*)(const PublicState&,
                                                const Normalizer&) const;
static_assert(std::is_same_v<decltype(&Network::predict), PublicEvaluator>);
static_assert(!std::is_invocable_v<PublicEvaluator, const Network&,
                                   const State&, const Normalizer&>);

struct TrainingRecord {
  int epoch = 0;
  double loss = 0.0;
};

struct TrainingResult {
  Network network{};
  std::array<TrainingRecord, kEpochs> records{};
};

TrainingResult train(const std::vector<PreparedExample>& examples,
                     const Deadline& deadline) {
  if (examples.empty()) throw std::invalid_argument("empty NNUE training set");
  TrainingResult result;
  std::vector<std::size_t> order(examples.size());
  std::iota(order.begin(), order.end(), 0u);
  for (int epoch = 0; epoch < kEpochs; ++epoch) {
    Mulberry32 random(mix32(kShuffleDomain ^
                           static_cast<std::uint32_t>(epoch + 1)));
    for (std::size_t cursor = order.size(); cursor > 1; --cursor) {
      const std::size_t selected = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * cursor) >> 32u);
      std::swap(order[cursor - 1], order[selected]);
    }
    double loss = 0.0;
    for (std::size_t begin = 0; begin < order.size(); begin += kBatchSize) {
      if ((begin & 8'191u) == 0) {
        deadline.check();
        enforceRss();
      }
      const std::size_t end = std::min(order.size(), begin + kBatchSize);
      const float inverse = 1.0f / static_cast<float>(end - begin);
      std::vector<float> gradient = result.network.gradient();
      for (std::size_t offset = begin; offset < end; ++offset) {
        result.network.accumulate(examples[order[offset]], inverse, gradient,
                                  loss);
      }
      result.network.apply(gradient);
    }
    result.records[epoch] =
        {epoch + 1, loss / static_cast<double>(examples.size())};
    std::cerr << "panel NNUE epoch " << epoch + 1 << '/' << kEpochs
              << " loss " << result.records[epoch].loss << " rss "
              << peakRssBytes() << '\n';
  }
  return result;
}

void fingerprintFloat(std::uint64_t& hash, float value) {
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  for (int shift = 0; shift < 32; shift += 8) {
    hash ^= static_cast<std::uint8_t>(bits >> shift);
    hash *= 0x0000'0100'0000'01b3ull;
  }
}

std::uint64_t modelFingerprint(const Network& network,
                               const Normalizer& normalizer) {
  std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
  for (const float value : network.parameters()) fingerprintFloat(hash, value);
  for (const float value : normalizer.metric_mean) fingerprintFloat(hash, value);
  for (const float value : normalizer.metric_scale) fingerprintFloat(hash, value);
  for (const float value : {normalizer.return_mean, normalizer.return_scale,
                            normalizer.clear_mean, normalizer.clear_scale,
                            normalizer.reveal_mean,
                            normalizer.reveal_scale}) {
    fingerprintFloat(hash, value);
  }
  return hash;
}

void saveCheckpoint(const std::string& path, const Network& network,
                    const Normalizer& normalizer) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open panel checkpoint");
  const std::uint32_t count = Layout::count;
  const std::uint32_t normalizer_size = sizeof(Normalizer);
  const std::uint64_t fingerprint = modelFingerprint(network, normalizer);
  output.write(reinterpret_cast<const char*>(&kCheckpointMagic),
               sizeof(kCheckpointMagic));
  output.write(reinterpret_cast<const char*>(&kCheckpointVersion),
               sizeof(kCheckpointVersion));
  output.write(reinterpret_cast<const char*>(&count), sizeof(count));
  output.write(reinterpret_cast<const char*>(&normalizer_size),
               sizeof(normalizer_size));
  output.write(reinterpret_cast<const char*>(&fingerprint),
               sizeof(fingerprint));
  output.write(reinterpret_cast<const char*>(&normalizer), sizeof(normalizer));
  output.write(reinterpret_cast<const char*>(network.parameters().data()),
               static_cast<std::streamsize>(network.parameters().size() *
                                            sizeof(float)));
  if (!output) throw std::runtime_error("failed writing panel checkpoint");
}

struct FrozenModel {
  Network network{};
  Normalizer normalizer{};
};

FrozenModel loadCheckpoint(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open panel checkpoint");
  std::uint64_t magic = 0;
  std::uint32_t version = 0;
  std::uint32_t count = 0;
  std::uint32_t normalizer_size = 0;
  std::uint64_t expected = 0;
  input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
  input.read(reinterpret_cast<char*>(&version), sizeof(version));
  input.read(reinterpret_cast<char*>(&count), sizeof(count));
  input.read(reinterpret_cast<char*>(&normalizer_size),
             sizeof(normalizer_size));
  input.read(reinterpret_cast<char*>(&expected), sizeof(expected));
  if (magic != kCheckpointMagic || version != kCheckpointVersion ||
      count != Layout::count || normalizer_size != sizeof(Normalizer)) {
    throw std::runtime_error("invalid panel checkpoint header");
  }
  FrozenModel result;
  input.read(reinterpret_cast<char*>(&result.normalizer),
             sizeof(result.normalizer));
  std::vector<float> parameters(count);
  input.read(reinterpret_cast<char*>(parameters.data()),
             static_cast<std::streamsize>(parameters.size() * sizeof(float)));
  char trailing = 0;
  if (!input || input.read(&trailing, 1)) {
    throw std::runtime_error("invalid panel checkpoint payload");
  }
  result.network.setParameters(parameters);
  if (modelFingerprint(result.network, result.normalizer) != expected) {
    throw std::runtime_error("panel checkpoint fingerprint mismatch");
  }
  return result;
}

struct RootBranch {
  PublicState state{};
  double immediate_return = 0.0;
  bool terminal = false;
};

RootBranch rootSuccessor(const PublicState& canonical, int action,
                         int scenario) {
  if (scenario < 0 || scenario >= kRootScenarios ||
      !isLegal(canonical.board, action)) {
    throw std::invalid_argument("invalid NNUE root successor");
  }
  const State root = constructive::materialize(canonical);
  const std::uint32_t seed = detail::scenarioSeedForState(
      root, kRootSuccessorDomain, 0);
  detail::StratifiedRandom random{seed, scenario, kRootScenarios, 0};
  MoveResult move;
  if (!detail::playMoveSampled(root, action, random, move)) {
    throw std::runtime_error("NNUE root successor transition failed");
  }
  RootBranch result;
  result.immediate_return =
      1.0 + static_cast<double>(move.score_delta) / 17'000.0;
  result.terminal = move.state.game_over;
  if (!result.terminal) {
    move.state.score = 0;
    move.state.level = 1;
    move.state.moves_played = 0;
    move.state.next_disc =
        detail::sampledNextDisc(seed, scenario, kRootScenarios);
    result.state = canonicalPublic(constructive::publicState(move.state));
  }
  return result;
}

double nnueActionValue(const PublicState& root, int action,
                       const FrozenModel& model) {
  bool mirrored = false;
  const PublicState canonical = constructive::canonicalPublic(root, mirrored);
  const int canonical_action = mirrored ? kBoardSize - 1 - action : action;
  double total = 0.0;
  for (int scenario = 0; scenario < kRootScenarios; ++scenario) {
    const RootBranch branch =
        rootSuccessor(canonical, canonical_action, scenario);
    total += branch.immediate_return;
    if (!branch.terminal) {
      total += model.network.predict(branch.state,
                                     model.normalizer).mean_return;
    }
  }
  return total / kRootScenarios;
}

double fairLeafActionValue(const PublicState& root, int action) {
  bool mirrored = false;
  const PublicState canonical = constructive::canonicalPublic(root, mirrored);
  const int canonical_action = mirrored ? kBoardSize - 1 - action : action;
  double total = 0.0;
  for (int scenario = 0; scenario < kRootScenarios; ++scenario) {
    const RootBranch branch =
        rootSuccessor(canonical, canonical_action, scenario);
    total += branch.immediate_return +
             (branch.terminal
                  ? -100.0
                  : fair::fairLeaf(
                        constructive::materialize(branch.state)) /
                        17'000.0);
  }
  return total / kRootScenarios;
}

int bestIndex(const std::vector<double>& values) {
  if (values.empty()) return -1;
  int result = 0;
  for (std::size_t index = 1; index < values.size(); ++index) {
    if (values[index] > values[static_cast<std::size_t>(result)]) {
      result = static_cast<int>(index);
    }
  }
  return result;
}

struct RankingMetrics {
  int roots = 0;
  int pairs = 0;
  double top1 = 0.0;
  double pairwise = 0.0;
  double normalized_regret = 0.0;
};

void observeRanking(RankingMetrics& result,
                    const std::vector<double>& predictions,
                    const std::vector<double>& targets) {
  if (predictions.size() != targets.size() || targets.size() < 2) {
    throw std::invalid_argument("invalid action ranking panel");
  }
  const int predicted = bestIndex(predictions);
  const int target = bestIndex(targets);
  result.top1 += predicted == target;
  const auto [minimum, maximum] =
      std::minmax_element(targets.begin(), targets.end());
  const double range = std::max(1.0e-9, *maximum - *minimum);
  result.normalized_regret +=
      (targets[target] - targets[predicted]) / range;
  for (std::size_t first = 0; first < targets.size(); ++first) {
    for (std::size_t second = first + 1; second < targets.size(); ++second) {
      const double target_difference = targets[first] - targets[second];
      if (std::abs(target_difference) <= 1.0e-9) continue;
      const double predicted_difference =
          predictions[first] - predictions[second];
      result.pairwise += target_difference * predicted_difference > 0.0;
      ++result.pairs;
    }
  }
  ++result.roots;
}

void finishRanking(RankingMetrics& result) {
  if (result.roots > 0) {
    result.top1 /= result.roots;
    result.normalized_regret /= result.roots;
  }
  if (result.pairs > 0) result.pairwise /= result.pairs;
}

struct HeldoutMetrics {
  RankingMetrics overall_nnue;
  RankingMetrics overall_fair;
  RankingMetrics d1_nnue;
  RankingMetrics d1_fair;
  RankingMetrics constructive_nnue;
  RankingMetrics constructive_fair;
};

HeldoutMetrics evaluateHeldout(const std::vector<Panel>& panels,
                               const FrozenModel& model,
                               const Deadline& deadline) {
  HeldoutMetrics result;
  int completed = 0;
  for (const Panel& panel : panels) {
    if ((completed & 63) == 0) deadline.check();
    std::vector<double> targets;
    std::vector<double> nnue;
    std::vector<double> baseline;
    for (const ActionLabel& action : panel.actions) {
      targets.push_back(action.mean_return);
      nnue.push_back(nnueActionValue(panel.root.state, action.action, model));
      baseline.push_back(
          fairLeafActionValue(panel.root.state, action.action));
    }
    observeRanking(result.overall_nnue, nnue, targets);
    observeRanking(result.overall_fair, baseline, targets);
    if (panel.root.policy == OriginPolicy::kFairD1) {
      observeRanking(result.d1_nnue, nnue, targets);
      observeRanking(result.d1_fair, baseline, targets);
    } else {
      observeRanking(result.constructive_nnue, nnue, targets);
      observeRanking(result.constructive_fair, baseline, targets);
    }
    ++completed;
  }
  finishRanking(result.overall_nnue);
  finishRanking(result.overall_fair);
  finishRanking(result.d1_nnue);
  finishRanking(result.d1_fair);
  finishRanking(result.constructive_nnue);
  finishRanking(result.constructive_fair);
  return result;
}

bool halfDoesNotRegressBoth(const RankingMetrics& candidate,
                            const RankingMetrics& baseline) {
  return !(candidate.pairwise < baseline.pairwise &&
           candidate.normalized_regret > baseline.normalized_regret);
}

bool passesHeldout(const HeldoutMetrics& metrics) {
  const RankingMetrics& candidate = metrics.overall_nnue;
  const RankingMetrics& baseline = metrics.overall_fair;
  return candidate.top1 >= kRequiredTopOne &&
         candidate.pairwise >= kRequiredPairwise &&
         candidate.normalized_regret <= kMaximumNormalizedRegret &&
         candidate.pairwise >= baseline.pairwise + kRequiredPairwiseGain &&
         candidate.normalized_regret <=
             kRequiredRegretRatio * baseline.normalized_regret &&
         halfDoesNotRegressBoth(metrics.d1_nnue, metrics.d1_fair) &&
         halfDoesNotRegressBoth(metrics.constructive_nnue,
                                metrics.constructive_fair);
}

struct Decision {
  int action = -1;
  int d1_action = -1;
  int admitted = 0;
  bool changed = false;

  bool operator==(const Decision&) const = default;
};

std::vector<int> d1Shortlist(const fair::RootEvaluation& root,
                             const Board& board) {
  if (root.action < 0 || !isLegal(board, root.action)) {
    throw std::invalid_argument("invalid D1 anchor");
  }
  std::vector<int> ranked;
  for (const int column : constructive::kColumnOrder) {
    if (isLegal(board, column)) ranked.push_back(column);
  }
  std::stable_sort(ranked.begin(), ranked.end(), [&](int left, int right) {
    if (left == root.action) return true;
    if (right == root.action) return false;
    return root.values[left] > root.values[right];
  });
  std::vector<int> result{root.action};
  for (const int action : ranked) {
    if (action == root.action) continue;
    if (root.values[action] >= root.value - kD1RootWindow) {
      result.push_back(action);
      break;
    }
  }
  if (result.size() > kMaximumScreenActions) {
    throw std::runtime_error("D1 shortlist exceeded two actions");
  }
  return result;
}

Decision chooseAction(const PublicState& source, const FrozenModel& model) {
  if (source.terminal) return {};
  bool mirrored = false;
  const PublicState canonical = constructive::canonicalPublic(source, mirrored);
  fair::SearchContext context;
  const fair::RootEvaluation d1 = fair::rootDecision(
      constructive::materialize(canonical), 1, context);
  if (d1.action < 0 || context.work > 70 || !context.cache.empty()) {
    throw std::runtime_error("screen D1 anchor incomplete");
  }
  const std::vector<int> admitted = d1Shortlist(d1, canonical.board);
  int selected = d1.action;
  double best = nnueActionValue(canonical, d1.action, model);
  for (std::size_t index = 1; index < admitted.size(); ++index) {
    const double value = nnueActionValue(canonical, admitted[index], model);
    if (value > best) {
      best = value;
      selected = admitted[index];
    }
  }
  return {
      mirrored ? kBoardSize - 1 - selected : selected,
      mirrored ? kBoardSize - 1 - d1.action : d1.action,
      static_cast<int>(admitted.size()),
      selected != d1.action,
  };
}

using PublicPolicy = Decision (*)(const PublicState&, const FrozenModel&);
static_assert(std::is_same_v<decltype(&chooseAction), PublicPolicy>);
static_assert(!std::is_invocable_v<PublicPolicy, const State&,
                                   const FrozenModel&>);

enum class ScreenPolicy : std::uint8_t { kCandidate, kFairD1 };

struct GameResult {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  int clears = 0;
  int reveals = 0;
  int maximum_chain = 0;
  int changed = 0;
  int admitted_two = 0;
  bool censored = false;
};

GameResult playScreenGame(const FrozenModel& model, std::uint32_t seed,
                          ScreenPolicy policy, const Deadline& deadline) {
  requireSeed(seed, SeedUse::kScreen);
  State state = initialHeadlessState(seed);
  GameResult result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kScreenMaximumMoves) {
    if ((state.moves_played & 31) == 0) deadline.check();
    const PublicState public_state = constructive::publicState(state);
    int action = -1;
    if (policy == ScreenPolicy::kCandidate) {
      const Decision decision = chooseAction(public_state, model);
      action = decision.action;
      result.changed += decision.changed;
      result.admitted_two += decision.admitted == 2;
    } else {
      action = constructive::chooseFairD1(public_state);
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("screen policy selected illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("screen transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += wave.cleared;
      result.reveals += wave.revealed;
      result.maximum_chain = std::max(result.maximum_chain, wave.depth);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

struct ScreenCohort {
  std::vector<GameResult> candidate;
  std::vector<GameResult> baseline;
  double seconds = 0.0;
};

ScreenCohort runScreen(const FrozenModel& model, int threads,
                       const Deadline& deadline) {
  const Clock::time_point started = Clock::now();
  ScreenCohort result;
  result.candidate.resize(kScreenGames);
  result.baseline.resize(kScreenGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  for (int worker = 0; worker < std::min(threads, kScreenGames); ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int game = next.fetch_add(1);
        if (game >= kScreenGames) return;
        const std::uint32_t seed =
            kScreenSeedStart + static_cast<std::uint32_t>(game);
        result.candidate[game] = playScreenGame(
            model, seed, ScreenPolicy::kCandidate, deadline);
        result.baseline[game] =
            playScreenGame(model, seed, ScreenPolicy::kFairD1, deadline);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct GameSummary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double changed_per_move = 0.0;
  double admitted_two_per_move = 0.0;
  int maximum_chain = 0;
  int censored = 0;
};

GameSummary summarizeGames(const std::vector<GameResult>& games) {
  GameSummary result;
  std::int64_t score = 0;
  std::int64_t moves = 0;
  std::int64_t clears = 0;
  std::int64_t reveals = 0;
  std::int64_t changed = 0;
  std::int64_t admitted = 0;
  for (const GameResult& game : games) {
    score += game.score;
    moves += game.moves;
    clears += game.clears;
    reveals += game.reveals;
    changed += game.changed;
    admitted += game.admitted_two;
    result.maximum_chain = std::max(result.maximum_chain, game.maximum_chain);
    result.censored += game.censored;
  }
  result.mean_score = static_cast<double>(score) / games.size();
  result.mean_moves = static_cast<double>(moves) / games.size();
  result.clears_per_move = static_cast<double>(clears) / moves;
  result.reveals_per_move = static_cast<double>(reveals) / moves;
  result.changed_per_move = static_cast<double>(changed) / moves;
  result.admitted_two_per_move = static_cast<double>(admitted) / moves;
  return result;
}

struct PairedGames {
  int score_wins = 0;
  int move_wins = 0;
  int joint_wins = 0;
  double score_delta = 0.0;
  double move_delta = 0.0;
};

PairedGames pairGames(const ScreenCohort& cohort) {
  PairedGames result;
  for (int game = 0; game < kScreenGames; ++game) {
    const GameResult& candidate = cohort.candidate[game];
    const GameResult& baseline = cohort.baseline[game];
    if (candidate.seed != baseline.seed) {
      throw std::runtime_error("screen seed mismatch");
    }
    const bool score_win = candidate.score > baseline.score;
    const bool move_win = candidate.moves > baseline.moves;
    result.score_wins += score_win;
    result.move_wins += move_win;
    result.joint_wins += score_win && move_win;
    result.score_delta += candidate.score - baseline.score;
    result.move_delta += candidate.moves - baseline.moves;
  }
  result.score_delta /= kScreenGames;
  result.move_delta /= kScreenGames;
  return result;
}

bool passesScreen(const GameSummary& candidate, const GameSummary& baseline,
                  const PairedGames& paired) {
  return candidate.mean_score >= kScreenScoreRatio * baseline.mean_score &&
         candidate.mean_moves >= kScreenMoveRatio * baseline.mean_moves &&
         candidate.clears_per_move >=
             baseline.clears_per_move + kScreenFlowGain &&
         candidate.reveals_per_move >=
             baseline.reveals_per_move + kScreenFlowGain &&
         paired.joint_wins >= kScreenJointWins;
}

struct Options {
  std::string checkpoint = "/tmp/drop7-panel-value-nnue.bin";
  std::string golden = "/tmp/drop7-panel-value-nnue-golden.json";
  std::string output = "/tmp/drop7-panel-value-nnue.json";
  int threads = 4;
};

Options parseOptions(int argc, char** argv, int begin) {
  Options result;
  for (int index = begin; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--checkpoint" && index + 1 < argc) {
      result.checkpoint = argv[++index];
    } else if (argument == "--golden" && index + 1 < argc) {
      result.golden = argv[++index];
    } else if (argument == "--output" && index + 1 < argc) {
      result.output = argv[++index];
    } else if (argument == "--threads" && index + 1 < argc) {
      result.threads = std::stoi(argv[++index]);
    } else {
      throw std::invalid_argument("unknown or incomplete panel-NNUE option");
    }
  }
  if (result.checkpoint.empty() || result.golden.empty() ||
      result.output.empty() || result.threads < 1 ||
      result.threads > kMaximumThreads) {
    throw std::invalid_argument("invalid panel-NNUE options");
  }
  return result;
}

void writeRanking(std::ostream& output, const RankingMetrics& metrics) {
  output << "{\"roots\":" << metrics.roots << ",\"pairs\":"
         << metrics.pairs << ",\"top1\":" << metrics.top1
         << ",\"pairwise\":" << metrics.pairwise
         << ",\"normalizedRegret\":" << metrics.normalized_regret << '}';
}

void writeGameSummary(std::ostream& output, const GameSummary& summary) {
  output << "{\"meanScore\":" << summary.mean_score
         << ",\"meanMoves\":" << summary.mean_moves
         << ",\"clearsPerMove\":" << summary.clears_per_move
         << ",\"revealsPerMove\":" << summary.reveals_per_move
         << ",\"changedPerMove\":" << summary.changed_per_move
         << ",\"admittedTwoPerMove\":"
         << summary.admitted_two_per_move << ",\"maximumChain\":"
         << summary.maximum_chain << ",\"censored\":" << summary.censored
         << '}';
}

void writePreflight(std::ostream& output, const Preflight& preflight) {
  output << "{\"roots\":" << preflight.roots
         << ",\"transitions\":" << preflight.transitions
         << ",\"seconds\":" << preflight.seconds
         << ",\"safetyFactor\":" << kProjectionSafety
         << ",\"projectedSeconds\":" << preflight.projected_seconds
         << ",\"rootsPerGame\":" << preflight.roots_per_game
         << ",\"peakRssBytes\":" << preflight.peak_rss_bytes
         << ",\"openedSeeds\":[";
  for (std::size_t index = 0; index < preflight.opened_seeds.size(); ++index) {
    if (index != 0) output << ',';
    output << "\"0x" << std::hex << preflight.opened_seeds[index] << std::dec
           << "\"";
  }
  output << "],\"passed\":" << (preflight.passed ? "true" : "false")
         << '}';
}

void writeGolden(const std::string& path, const FrozenModel& model,
                 const std::vector<Panel>& heldout) {
  if (heldout.size() < 4) {
    throw std::runtime_error("not enough heldout roots for golden fixture");
  }
  std::ofstream output(path);
  if (!output) throw std::runtime_error("could not open golden fixture");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-panel-value-nnue-golden-v1\","
         << "\n  \"modelFingerprint\":\"0x" << std::hex
         << modelFingerprint(model.network, model.normalizer) << std::dec
         << "\",\n  \"cases\":[";
  for (int index = 0; index < 4; ++index) {
    if (index != 0) output << ',';
    const Panel& panel = heldout[index];
    const Prediction prediction =
        model.network.predict(panel.root.state, model.normalizer);
    output << "{\"publicHash\":\"0x" << std::hex
           << publicHash(panel.root.state) << std::dec
           << "\",\"meanReturn\":" << prediction.mean_return
           << ",\"survival\":" << prediction.survival
           << ",\"clears\":" << prediction.clears
           << ",\"reveals\":" << prediction.reveals
           << ",\"downsideReturn\":" << prediction.downside_return
           << ",\"actionValues\":[";
    for (std::size_t action = 0; action < panel.actions.size(); ++action) {
      if (action != 0) output << ',';
      output << nnueActionValue(panel.root.state,
                                panel.actions[action].action, model);
    }
    output << "]}";
  }
  output << "]\n}\n";
  if (!output) throw std::runtime_error("failed writing golden fixture");
}

void writeFailureArtifact(const Options& options,
                          const Preflight& preflight,
                          double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open preflight artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-panel-value-nnue-v1\","
         << "\n  \"status\":\"preflight-rejected\",\n  \"preflight\":";
  writePreflight(output, preflight);
  output << ",\n  \"originRange\":\"0x3d6c1000..0x3d6c13ff\","
         << "\n  \"screen\":\"unopened\",\n  \"passed\":false,"
         << "\n  \"wallSeconds\":" << wall_seconds
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
}

void writeArtifact(const Options& options, const Preflight& preflight,
                   const RootCollection& roots, const Dataset& dataset,
                   const Normalizer& normalizer,
                   const TrainingResult& training,
                   const FrozenModel& model, const HeldoutMetrics& heldout,
                   bool heldout_passed,
                   const std::optional<ScreenCohort>& screen,
                   const std::optional<GameSummary>& screen_candidate,
                   const std::optional<GameSummary>& screen_baseline,
                   const std::optional<PairedGames>& screen_paired,
                   bool screen_passed, double collection_seconds,
                   double label_seconds, double training_seconds,
                   double wall_seconds) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open panel artifact");
  output << std::setprecision(12)
         << "{\n  \"format\":\"drop7-panel-value-nnue-v1\","
         << "\n  \"status\":\"complete\",\n  \"preflight\":";
  writePreflight(output, preflight);
  output << ",\n  \"protocol\":{\"originRange\":\"0x3d6c1000..0x3d6c13ff\","
         << "\"originGames\":" << kOriginGames
         << ",\"firstPolicy\":\"fair-D1\",\"secondPolicy\":\"constructive-spectrum\","
         << "\"milestones\":[5,10,15,20,25,30,40,50],\"rootsPerGame\":"
         << preflight.roots_per_game
         << ",\"split\":\"whole-origin hash-ranked 75/25 separately per policy\","
         << "\"labelScenarios\":" << kLabelScenarios
         << ",\"labelHorizon\":" << kLabelHorizon
         << ",\"continuation\":\"exact fair-D1\",\"reward\":\"moves survived + score/17000\","
         << "\"trainingInternalReservoirCap\":" << kReservoirCap << "},"
         << "\n  \"roots\":{\"training\":" << roots.training.size()
         << ",\"heldout\":" << roots.heldout.size()
         << ",\"duplicateTraining\":" << roots.duplicate_training_roots
         << ",\"duplicateHeldout\":" << roots.duplicate_heldout_roots
         << ",\"heldoutOverlapPurged\":"
         << roots.heldout_overlap_purged
         << ",\"d1TrainingGames\":" << roots.d1_training_games
         << ",\"d1HeldoutGames\":" << roots.d1_heldout_games
         << ",\"constructiveTrainingGames\":"
         << roots.constructive_training_games
         << ",\"constructiveHeldoutGames\":"
         << roots.constructive_heldout_games << "},"
         << "\n  \"labels\":{\"trainingExamples\":"
         << dataset.training_examples.size() << ",\"transitions\":"
         << dataset.transitions << ",\"d1Work\":" << dataset.d1_work
         << ",\"heldoutInternalStatesUsed\":false},"
         << "\n  \"model\":{\"kind\":\"reflection-exact additive NNUE\","
         << "\"boardCategories\":" << kBoardCategories
         << ",\"visibleCategories\":"
         << kNextCategories + kPhaseCategories
         << ",\"structuralMetrics\":" << kMetricCount
         << ",\"hidden\":" << kHidden << ",\"activation\":\"clipped-ReLU[0,1]\","
         << "\"heads\":[\"meanReturn\",\"survival\",\"clears\",\"reveals\",\"downsideReturnQ20\"],"
         << "\"parameters\":" << Layout::count
         << ",\"serializedBytes\":"
         << Layout::count * sizeof(float) + sizeof(Normalizer) + 28
         << ",\"fingerprint\":\"0x" << std::hex
         << modelFingerprint(model.network, normalizer) << std::dec
         << "\",\"epochs\":" << kEpochs << ",\"batch\":"
         << kBatchSize << ",\"learningRate\":" << kLearningRate
         << ",\"firstLoss\":" << training.records.front().loss
         << ",\"finalLoss\":" << training.records.back().loss << "},"
         << "\n  \"heldout\":{\"overallNNUE\":";
  writeRanking(output, heldout.overall_nnue);
  output << ",\"overallFairLeaf\":";
  writeRanking(output, heldout.overall_fair);
  output << ",\"d1NNUE\":";
  writeRanking(output, heldout.d1_nnue);
  output << ",\"d1FairLeaf\":";
  writeRanking(output, heldout.d1_fair);
  output << ",\"constructiveNNUE\":";
  writeRanking(output, heldout.constructive_nnue);
  output << ",\"constructiveFairLeaf\":";
  writeRanking(output, heldout.constructive_fair);
  output << ",\"gate\":{\"top1\":" << kRequiredTopOne
         << ",\"pairwise\":" << kRequiredPairwise
         << ",\"maximumNormalizedRegret\":"
         << kMaximumNormalizedRegret << ",\"pairwiseGain\":"
         << kRequiredPairwiseGain << ",\"regretRatio\":"
         << kRequiredRegretRatio
         << ",\"neitherPolicyHalfRegressesBoth\":true},\"passed\":"
         << (heldout_passed ? "true" : "false") << "},"
         << "\n  \"screenGate\":{\"scoreRatio\":"
         << kScreenScoreRatio << ",\"moveRatio\":" << kScreenMoveRatio
         << ",\"flowGain\":" << kScreenFlowGain
         << ",\"jointWins\":" << kScreenJointWins << "},"
         << "\n  \"screen\":";
  if (screen) {
    output << "{\"seeds\":\"0x3d6c8000..0x3d6c801f\",\"candidate\":";
    writeGameSummary(output, *screen_candidate);
    output << ",\"fairD1\":";
    writeGameSummary(output, *screen_baseline);
    output << ",\"paired\":{\"scoreWins\":"
           << screen_paired->score_wins << ",\"moveWins\":"
           << screen_paired->move_wins << ",\"jointWins\":"
           << screen_paired->joint_wins << ",\"meanScoreDelta\":"
           << screen_paired->score_delta << ",\"meanMoveDelta\":"
           << screen_paired->move_delta << "},\"seconds\":"
           << screen->seconds << ",\"passed\":"
           << (screen_passed ? "true" : "false") << '}';
  } else {
    output << "null";
  }
  output << ",\n  \"checkpoint\":\"" << options.checkpoint
         << "\",\"golden\":\"" << options.golden << "\","
         << "\n  \"timing\":{\"collectionSeconds\":"
         << collection_seconds << ",\"labelSeconds\":" << label_seconds
         << ",\"trainingSeconds\":" << training_seconds
         << ",\"wallSeconds\":" << wall_seconds << "},"
         << "\n  \"passed\":"
         << (heldout_passed && screen_passed ? "true" : "false")
         << ",\n  \"peakRssBytes\":" << peakRssBytes() << "\n}\n";
  if (!output) throw std::runtime_error("failed writing panel artifact");
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

template <typename Function>
bool throwsInvalid(Function&& function) {
  try {
    function();
  } catch (const std::invalid_argument&) {
    return true;
  }
  return false;
}

PublicState fixtureState() {
  PublicState fixture;
  fixture.board.fill(kEmpty);
  fixture.board[indexOf(6, 0)] = kSolid;
  fixture.board[indexOf(5, 0)] = 6;
  fixture.board[indexOf(6, 1)] = kCracked;
  fixture.board[indexOf(6, 2)] = 5;
  fixture.board[indexOf(5, 2)] = 4;
  fixture.board[indexOf(6, 3)] = kSolid;
  fixture.board[indexOf(6, 4)] = 7;
  fixture.next_disc = 3;
  fixture.moves_remaining = 4;
  return fixture;
}

bool selfTest(const Options& options, std::ostream& output) {
  const PublicState fixture = fixtureState();
  const SplitTable split = buildSplit();
  int d1_heldout = 0;
  int constructive_heldout = 0;
  for (int game = 0; game < kOriginGames; ++game) {
    if (!split.heldout[game]) continue;
    if (game < kD1OriginGames) ++d1_heldout;
    else ++constructive_heldout;
  }
  expect(d1_heldout == kHeldoutPerPolicy &&
             constructive_heldout == kHeldoutPerPolicy,
         "whole-origin split self-test failed");

  Normalizer normalizer;
  normalizer.metric_scale.fill(1.0f);
  Network network;
  const Prediction prediction = network.predict(fixture, normalizer);
  const Prediction reflected =
      network.predict(constructive::mirror(fixture), normalizer);
  expect(prediction == reflected, "NNUE reflection self-test failed");
  FrozenModel model{network, normalizer};
  const double action_value = nnueActionValue(fixture, 1, model);
  const double reflected_action_value = nnueActionValue(
      constructive::mirror(fixture), kBoardSize - 1 - 1, model);
  expect(action_value == reflected_action_value,
         "root inference reflection self-test failed");

  State metadata = constructive::materialize(fixture);
  metadata.score = 9'999'999;
  metadata.level = 777;
  metadata.moves_played = 888;
  expect(constructive::publicState(metadata) == fixture &&
             network.predict(constructive::publicState(metadata), normalizer) ==
                 prediction,
         "NNUE retained private metadata");

  const std::string checkpoint = options.checkpoint + ".self-test";
  saveCheckpoint(checkpoint, network, normalizer);
  const FrozenModel restored = loadCheckpoint(checkpoint);
  expect(restored.network.parameters() == network.parameters() &&
             modelFingerprint(restored.network, restored.normalizer) ==
                 modelFingerprint(network, normalizer) &&
             restored.network.predict(fixture, restored.normalizer) ==
                 prediction,
         "checkpoint/golden inference self-test failed");

  PreparedExample example;
  example.state = fixture;
  example.metrics = normalizer.metrics(fixture);
  example.targets = {{0.25f, 1.0f, -0.3f, 0.2f, -0.1f}};
  std::vector<float> gradient = network.gradient();
  double loss = 0.0;
  network.accumulate(example, 1.0f, gradient, loss);
  double gradient_norm = 0.0;
  for (const float value : gradient) gradient_norm += value * value;
  Network first = network;
  Network second = network;
  std::vector<float> repeated = gradient;
  first.apply(gradient);
  second.apply(repeated);
  expect(std::isfinite(loss) && gradient_norm > 0.0 &&
             first.parameters() == second.parameters(),
         "NNUE gradient/determinism self-test failed");

  const std::uint32_t first_tape = tapeSeed(fixture, 0);
  const std::uint32_t repeated_tape = tapeSeed(fixture, 0);
  const std::uint32_t other_tape = tapeSeed(fixture, 1);
  expect(first_tape == repeated_tape && first_tape != other_tape,
         "public common-tape self-test failed");
  expect(1.0 + 34'000.0 / 17'000.0 == 3.0,
         "corrected reward math self-test failed");
  expect(sizeof(Normalizer) + Layout::count * sizeof(float) <
             512ull * 1024ull,
         "serialized model size self-test failed");

  HeldoutMetrics positive;
  positive.overall_nnue = {10, 100, 0.31, 0.60, 0.20};
  positive.overall_fair = {10, 100, 0.20, 0.56, 0.25};
  positive.d1_nnue = {5, 50, 0.30, 0.59, 0.21};
  positive.d1_fair = {5, 50, 0.20, 0.57, 0.23};
  positive.constructive_nnue = {5, 50, 0.32, 0.61, 0.19};
  positive.constructive_fair = {5, 50, 0.20, 0.55, 0.26};
  expect(passesHeldout(positive), "positive heldout gate self-test failed");
  HeldoutMetrics negative = positive;
  negative.overall_nnue.pairwise = 0.57;
  expect(!passesHeldout(negative), "negative heldout gate self-test failed");

  GameSummary screen_candidate;
  screen_candidate.mean_score = 121;
  screen_candidate.mean_moves = 121;
  screen_candidate.clears_per_move = 2.051;
  screen_candidate.reveals_per_move = 1.151;
  GameSummary screen_baseline;
  screen_baseline.mean_score = 100;
  screen_baseline.mean_moves = 100;
  screen_baseline.clears_per_move = 2.0;
  screen_baseline.reveals_per_move = 1.1;
  PairedGames paired;
  paired.joint_wins = 20;
  expect(passesScreen(screen_candidate, screen_baseline, paired),
         "positive screen gate self-test failed");
  screen_candidate.mean_moves = 119;
  expect(!passesScreen(screen_candidate, screen_baseline, paired),
         "negative screen gate self-test failed");

  expect(allowedSeed(kOriginSeedStart, SeedUse::kOrigin) &&
             allowedSeed(kOriginSeedEndExclusive - 1, SeedUse::kOrigin) &&
             allowedSeed(kScreenSeedStart, SeedUse::kScreen) &&
             allowedSeed(kScreenSeedEndExclusive - 1, SeedUse::kScreen) &&
             throwsInvalid([] {
               requireSeed(0x3d6c'1400u, SeedUse::kOrigin);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d6c'8020u, SeedUse::kScreen);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d68'0000u, SeedUse::kOrigin);
             }) &&
             throwsInvalid([] {
               requireSeed(0x3d69'0000u, SeedUse::kScreen);
             }) &&
             throwsInvalid([] {
               requireSeed(0x4d6c'1000u, SeedUse::kOrigin);
             }) &&
             throwsInvalid([] {
               requireSeed(0x7d6c'1000u, SeedUse::kOrigin);
             }) &&
             throwsInvalid([] {
               requireSeed(0xd76c'8000u, SeedUse::kScreen);
             }),
         "seed guards self-test failed");
  enforceRss();
  output << std::setprecision(12)
         << "PANEL_VALUE_NNUE_SELF_TEST {\"passed\":true,"
         << "\"correctedScoring\":true,\"split384x128PerPolicy\":true,"
         << "\"publicCommonTapes\":true,\"reflectionExact\":true,"
         << "\"metadataBlind\":true,\"checkpointGolden\":true,"
         << "\"gradientDeterministic\":true,\"gateWiring\":true,"
         << "\"seedGuards\":true,\"parameters\":" << Layout::count
         << ",\"serializedBytes\":"
         << sizeof(Normalizer) + Layout::count * sizeof(float) + 28
         << ",\"peakRssBytes\":" << peakRssBytes() << "}\n";
  return true;
}

int preflightOnly(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const SplitTable split = buildSplit();
  const Preflight preflight = runPreflight(split, options.threads, deadline);
  output << std::setprecision(12) << "PANEL_VALUE_NNUE_PREFLIGHT ";
  writePreflight(output, preflight);
  output << "\n";
  if (!preflight.passed) {
    writeFailureArtifact(options, preflight, deadline.seconds());
  }
  return preflight.passed ? EXIT_SUCCESS : 2;
}

int run(const Options& options, std::ostream& output) {
  const Deadline deadline;
  const SplitTable split = buildSplit();
  const Preflight preflight = runPreflight(split, options.threads, deadline);
  output << std::setprecision(12) << "PANEL_VALUE_NNUE_PREFLIGHT ";
  writePreflight(output, preflight);
  output << "\n" << std::flush;
  if (!preflight.passed) {
    writeFailureArtifact(options, preflight, deadline.seconds());
    return 2;
  }

  const Clock::time_point collection_started = Clock::now();
  const RootCollection roots = collectAllRoots(
      split, preflight.roots_per_game, options.threads, deadline);
  const double collection_seconds =
      std::chrono::duration<double>(Clock::now() - collection_started).count();
  output << "PANEL_VALUE_NNUE_ROOTS {\"training\":"
         << roots.training.size() << ",\"heldout\":"
         << roots.heldout.size() << ",\"rootsPerGame\":"
         << preflight.roots_per_game << ",\"seconds\":"
         << collection_seconds << "}\n" << std::flush;

  const Clock::time_point label_started = Clock::now();
  Dataset dataset = labelRoots(roots, options.threads, deadline);
  const double label_seconds =
      std::chrono::duration<double>(Clock::now() - label_started).count();
  output << "PANEL_VALUE_NNUE_LABELS {\"trainingExamples\":"
         << dataset.training_examples.size() << ",\"transitions\":"
         << dataset.transitions << ",\"d1Work\":" << dataset.d1_work
         << ",\"seconds\":" << label_seconds << "}\n" << std::flush;

  const Normalizer normalizer = fitNormalizer(dataset.training_examples);
  std::vector<PreparedExample> prepared =
      prepareExamples(dataset.training_examples, normalizer);
  const Clock::time_point training_started = Clock::now();
  TrainingResult training = train(prepared, deadline);
  const double training_seconds =
      std::chrono::duration<double>(Clock::now() - training_started).count();
  saveCheckpoint(options.checkpoint, training.network, normalizer);
  const FrozenModel model = loadCheckpoint(options.checkpoint);
  if (modelFingerprint(model.network, model.normalizer) !=
      modelFingerprint(training.network, normalizer)) {
    throw std::runtime_error("frozen panel checkpoint mismatch");
  }
  writeGolden(options.golden, model, dataset.heldout_panels);
  output << "PANEL_VALUE_NNUE_MODEL {\"fingerprint\":\"0x" << std::hex
         << modelFingerprint(model.network, model.normalizer) << std::dec
         << "\",\"firstLoss\":" << training.records.front().loss
         << ",\"finalLoss\":" << training.records.back().loss
         << ",\"seconds\":" << training_seconds << "}\n" << std::flush;

  const HeldoutMetrics heldout =
      evaluateHeldout(dataset.heldout_panels, model, deadline);
  const bool heldout_passed = passesHeldout(heldout);
  output << "PANEL_VALUE_NNUE_HELDOUT {\"nnueTop1\":"
         << heldout.overall_nnue.top1 << ",\"nnuePairwise\":"
         << heldout.overall_nnue.pairwise << ",\"nnueRegret\":"
         << heldout.overall_nnue.normalized_regret
         << ",\"fairPairwise\":" << heldout.overall_fair.pairwise
         << ",\"fairRegret\":"
         << heldout.overall_fair.normalized_regret << ",\"passed\":"
         << (heldout_passed ? "true" : "false") << "}\n" << std::flush;

  std::optional<ScreenCohort> screen;
  std::optional<GameSummary> screen_candidate;
  std::optional<GameSummary> screen_baseline;
  std::optional<PairedGames> screen_paired;
  bool screen_passed = false;
  if (heldout_passed) {
    screen = runScreen(model, options.threads, deadline);
    screen_candidate = summarizeGames(screen->candidate);
    screen_baseline = summarizeGames(screen->baseline);
    screen_paired = pairGames(*screen);
    screen_passed = passesScreen(*screen_candidate, *screen_baseline,
                                 *screen_paired);
  }
  deadline.check();
  enforceRss();
  writeArtifact(options, preflight, roots, dataset, normalizer, training,
                model, heldout, heldout_passed, screen, screen_candidate,
                screen_baseline, screen_paired, screen_passed,
                collection_seconds, label_seconds, training_seconds,
                deadline.seconds());
  output << "PANEL_VALUE_NNUE_RESULT {\"heldoutPassed\":"
         << (heldout_passed ? "true" : "false")
         << ",\"screenOpened\":" << (screen ? "true" : "false")
         << ",\"screenPassed\":"
         << (screen_passed ? "true" : "false")
         << ",\"wallSeconds\":" << deadline.seconds()
         << ",\"peakRssBytes\":" << peakRssBytes()
         << ",\"artifact\":\"" << options.output << "\"}\n";
  return heldout_passed && screen_passed ? EXIT_SUCCESS : 2;
}

}  // namespace drop7::panel_value_nnue

int main(int argc, char** argv) {
  try {
    if (argc < 2) throw std::invalid_argument("missing mode");
    const std::string_view mode(argv[1]);
    const drop7::panel_value_nnue::Options options =
        drop7::panel_value_nnue::parseOptions(argc, argv, 2);
    if (mode == "--self-test") {
      return drop7::panel_value_nnue::selfTest(options, std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (mode == "--preflight") {
      return drop7::panel_value_nnue::preflightOnly(options, std::cout);
    }
    if (mode == "--run") {
      return drop7::panel_value_nnue::run(options, std::cout);
    }
    throw std::invalid_argument(
        "usage: drop7_panel_value_nnue --self-test | --preflight | --run [--checkpoint PATH] [--golden PATH] [--output PATH] [--threads 1..8]");
  } catch (const std::exception& error) {
    std::cerr << "drop7_panel_value_nnue: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
