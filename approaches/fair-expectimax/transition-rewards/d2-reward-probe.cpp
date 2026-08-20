#define DROP7_FAIR_PHASE_ENERGY_RELEASE_LIBRARY
#include "fair-phase-energy-release.cpp"
#undef DROP7_FAIR_PHASE_ENERGY_RELEASE_LIBRARY

#include <atomic>
#include <future>

// Fast, training-range-only landscape probe for transition rewards.  It uses
// the same public fair leaf, five stratified chance outcomes, and transition
// semantics as the reference D4 policy, but completes only depth two.  The
// executable is deliberately barred from fresh 0x3e, validation, and final
// seed families; its output can generate hypotheses but cannot qualify one.
namespace drop7::d2_reward_probe {

namespace phase = fair_phase_energy_release;
using Clock = std::chrono::steady_clock;

constexpr int kDepth = 2;
constexpr int kDefaultGames = 256;
constexpr int kDefaultThreads = 8;
constexpr int kDefaultMaximumMoves = 1'000;
constexpr std::uint32_t kDefaultSeedStart = 0x3d98'0000u;
constexpr std::uint32_t kTrainingStart = 0x3d00'0000u;
constexpr std::uint32_t kTrainingEnd = 0x3e00'0000u;

struct Reward {
  const char* name;
  double clears;
  double reveals;
};

constexpr std::array<Reward, 12> kMenu{{
    {"stock", 0.0, 0.0},
    {"c150-r300", 150.0, 300.0},
    {"c150-r600", 150.0, 600.0},
    {"c150-r900", 150.0, 900.0},
    {"c300-r300", 300.0, 300.0},
    {"c300-r600", 300.0, 600.0},
    {"c300-r900", 300.0, 900.0},
    {"c600-r300", 600.0, 300.0},
    {"c600-r600", 600.0, 600.0},
    {"c600-r900", 600.0, 900.0},
    {"c0-r600", 0.0, 600.0},
    {"c300-r1200", 300.0, 1'200.0},
}};

static_assert(kDepth < phase::kDepth);
static_assert(phase::kChanceSamples == 5);
static_assert(kDefaultSeedStart >= kTrainingStart &&
              kDefaultSeedStart < kTrainingEnd);

struct Options {
  std::uint32_t seed_start = kDefaultSeedStart;
  int games = kDefaultGames;
  int maximum_moves = kDefaultMaximumMoves;
  int threads = kDefaultThreads;
};

int parsePositive(std::string_view text, std::string_view flag) {
  std::size_t consumed = 0;
  const long long value = std::stoll(std::string(text), &consumed, 0);
  if (consumed != text.size() || value < 1 ||
      value > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(flag) + " must be positive");
  }
  return static_cast<int>(value);
}

std::uint32_t parseSeed(std::string_view text) {
  std::size_t consumed = 0;
  const unsigned long long value =
      std::stoull(std::string(text), &consumed, 0);
  if (consumed != text.size() || value > 0xffff'ffffull) {
    throw std::invalid_argument("--seed-start must be a uint32");
  }
  return static_cast<std::uint32_t>(value);
}

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) throw std::invalid_argument("missing option value");
    const std::string_view flag = argv[index];
    const std::string_view value = argv[index + 1];
    if (flag == "--seed-start") result.seed_start = parseSeed(value);
    else if (flag == "--games") result.games = parsePositive(value, flag);
    else if (flag == "--max-moves") {
      result.maximum_moves = parsePositive(value, flag);
    } else if (flag == "--threads") {
      result.threads = parsePositive(value, flag);
    } else {
      throw std::invalid_argument("unknown option " + std::string(flag));
    }
  }
  const std::uint64_t end =
      static_cast<std::uint64_t>(result.seed_start) + result.games;
  if (result.seed_start < kTrainingStart || end > kTrainingEnd) {
    throw std::invalid_argument(
        "reward probe is restricted to the 0x3d training family");
  }
  return result;
}

phase::Config configFor(const Reward& reward) {
  return {reward.name, reward.clears, 0.0, reward.reveals};
}

phase::SearchDecision chooseDepthTwo(const State& source,
                                     const phase::Config& config) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(
      phase::publicState(source), mirrored);
  phase::SearchContext context;
  const phase::RootEvaluation root =
      phase::rootDecision(canonical, kDepth, config, context);
  if (root.action < 0) throw std::runtime_error("D2 found no live action");
  phase::SearchDecision result;
  result.action = mirrored ? kBoardSize - 1 - root.action : root.action;
  result.completed_depth = kDepth;
  result.complete = true;
  result.nodes = context.nodes;
  result.work = context.work;
  result.cache_hits = context.cache_hits;
  result.cache_entries = context.cache.size();
  return result;
}

struct Game {
  std::int64_t score = 0;
  int moves = 0;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::uint64_t work = 0;
  bool censored = false;
};

Game runGame(std::uint32_t seed, const Reward& reward, int maximum_moves) {
  const phase::Config config = configFor(reward);
  State state = initialHeadlessState(seed);
  Game result;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const phase::SearchDecision decision = chooseDepthTwo(state, config);
    if (!decision.complete || !isLegal(state.board, decision.action)) {
      throw std::runtime_error("D2 reward policy returned invalid action");
    }
    result.work += decision.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("D2 reward transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += static_cast<std::uint64_t>(wave.cleared);
      result.reveals += static_cast<std::uint64_t>(wave.revealed);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  return result;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double work_per_move = 0.0;
  int censored = 0;
};

Summary summarize(const std::vector<Game>& games) {
  Summary result;
  std::uint64_t moves = 0;
  for (const Game& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    moves += static_cast<std::uint64_t>(game.moves);
    result.clears_per_move += game.clears;
    result.reveals_per_move += game.reveals;
    result.work_per_move += game.work;
  }
  if (moves > 0) {
    result.clears_per_move /= static_cast<double>(moves);
    result.reveals_per_move /= static_cast<double>(moves);
    result.work_per_move /= static_cast<double>(moves);
  }
  return result;
}

int run(const Options& options) {
  const auto started = Clock::now();
  std::array<std::vector<Game>, kMenu.size()> results;
  for (auto& games : results) games.resize(options.games);
  const std::size_t jobs = kMenu.size() * static_cast<std::size_t>(options.games);
  std::atomic<std::size_t> next{0};
  std::vector<std::future<void>> workers;
  const int worker_count =
      std::min(options.threads, static_cast<int>(jobs));
  for (int worker = 0; worker < worker_count; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const std::size_t job = next.fetch_add(1);
        if (job >= jobs) return;
        const std::size_t policy = job / static_cast<std::size_t>(options.games);
        const int game = static_cast<int>(job % options.games);
        results[policy][static_cast<std::size_t>(game)] = runGame(
            options.seed_start + static_cast<std::uint32_t>(game),
            kMenu[policy], options.maximum_moves);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::cout << std::fixed << std::setprecision(6)
            << "D2_REWARD_PROBE {\"seedStart\":" << options.seed_start
            << ",\"games\":" << options.games
            << ",\"trainingOnly\":true,\"seconds\":" << seconds
            << ",\"policies\":[";
  for (std::size_t policy = 0; policy < kMenu.size(); ++policy) {
    if (policy > 0) std::cout << ',';
    const Summary value = summarize(results[policy]);
    std::cout << "{\"name\":\"" << kMenu[policy].name
              << "\",\"clearReward\":" << kMenu[policy].clears
              << ",\"revealReward\":" << kMenu[policy].reveals
              << ",\"meanScore\":" << value.mean_score
              << ",\"meanMoves\":" << value.mean_moves
              << ",\"clearsPerMove\":" << value.clears_per_move
              << ",\"revealsPerMove\":" << value.reveals_per_move
              << ",\"workPerMove\":" << value.work_per_move
              << ",\"censored\":" << value.censored << '}';
  }
  std::cout << "]}\n";
  return 0;
}

}  // namespace drop7::d2_reward_probe

int main(int argc, char** argv) {
  try {
    return drop7::d2_reward_probe::run(
        drop7::d2_reward_probe::parseOptions(argc, argv));
  } catch (const std::exception& error) {
    std::cerr << "drop7_d2_reward_probe: " << error.what() << '\n';
    return 1;
  }
}
