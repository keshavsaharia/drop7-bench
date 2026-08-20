#define DROP7_FAIR_ONLY_DEPTH4_LIBRARY
#include "../reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_LIBRARY

#include <array>
#include <atomic>
#include <future>
#include <numeric>

// Training-only falsification lab for a literal form of stored chain energy.
// A vertical ladder is a sequence of numbered discs that will pop on
// successive column heights after a future quiet addition activates the first
// disc.  The evaluator ignores horizontal help and covered-disc reveals, so it
// is a conservative description of energy already stored in the visible
// column rather than a clairvoyant cascade simulation.
namespace drop7::d2_vertical_ladder_probe {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr int kDepth = 2;
constexpr int kChanceSamples = d4::kChanceSamples;
constexpr int kDefaultGames = 256;
constexpr int kDefaultThreads = 8;
constexpr int kDefaultMaximumMoves = 1'000;
constexpr std::uint32_t kDefaultSeedStart = 0x3d9e'0000u;
constexpr std::uint32_t kTrainingStart = 0x3d00'0000u;
constexpr std::uint32_t kTrainingEnd = 0x3e00'0000u;
constexpr double kTerminalUtility = fair::kTerminalUtility;

static_assert(kDepth == 2 && kChanceSamples == 5);
static_assert(kDefaultSeedStart >= kTrainingStart &&
              kDefaultSeedStart < kTrainingEnd);

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

LadderFeatures verticalLadderColumnFeatures(const Board& board, int column) {
  if (column < 0 || column >= kBoardSize) {
    throw std::invalid_argument("vertical ladder column out of range");
  }
  std::array<std::uint8_t, kBoardSize> stored{};
  int height = 0;
  for (int row = kBoardSize - 1; row >= 0; --row) {
    const std::uint8_t cell = board[indexOf(row, column)];
    if (cell != kEmpty) stored[height++] = cell;
  }
  LadderFeatures best;
  for (int additions = 1; additions <= kBoardSize - height; ++additions) {
    std::array<std::uint8_t, kBoardSize> live = stored;
    int live_count = height;
    // Solid is an inert token for this vertical-only abstraction.  Its
    // location is irrelevant because every live cell shares column height.
    for (int offset = 0; offset < additions; ++offset) {
      live[live_count++] = kSolid;
    }
    int waves = 0;
    int clears = 0;
    double energy = 0.0;
    for (;;) {
      const int vertical_length = live_count;
      int popping = 0;
      for (int index = 0; index < live_count; ++index) {
        popping +=
            isNumbered(live[index]) && live[index] == vertical_length;
      }
      if (popping == 0) break;
      ++waves;
      clears += popping;
      int retained = 0;
      for (int index = 0; index < live_count; ++index) {
        const std::uint8_t cell = live[index];
        if (isNumbered(cell) && cell == vertical_length) continue;
        live[retained++] = cell;
      }
      live_count = retained;
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
  return best;
}

LadderFeatures verticalLadderFeatures(const Board& board) {
  LadderFeatures result;
  for (int column = 0; column < kBoardSize; ++column) {
    const LadderFeatures best = verticalLadderColumnFeatures(board, column);
    result.activation += best.activation;
    result.chain_clears += best.chain_clears;
    result.extra_waves += best.extra_waves;
    result.energy += best.energy;
    result.best_waves = std::max(result.best_waves, best.best_waves);
  }
  return result;
}

struct Policy {
  const char* name;
  double energy_weight;
};

constexpr std::array<Policy, 7> kPolicies{{
    {"stock", 0.0},
    {"ladder-125", 125.0},
    {"ladder-250", 250.0},
    {"ladder-500", 500.0},
    {"ladder-1000", 1'000.0},
    {"ladder-2000", 2'000.0},
    {"ladder-4000", 4'000.0},
}};

double leafValue(const State& state, const Policy& policy,
                 std::uint64_t& work) {
  ++work;
  const double base = fair::fairLeaf(state);
  if (policy.energy_weight == 0.0 || state.game_over) return base;
  return base +
         policy.energy_weight * verticalLadderFeatures(state.board).energy;
}

double bestFutureValue(const State& state, int depth, const Policy& policy,
                       std::uint64_t& work);

struct ActionValue {
  double value = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           const Policy& policy, std::uint64_t& work) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, column, random, move);
    ++work;
    if (!played) {
      result.value += kTerminalUtility;
      continue;
    }
    const double score_delta = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      result.value += score_delta + kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value += score_delta +
                    bestFutureValue(next, depth - 1, policy, work);
  }
  result.value /= kChanceSamples;
  return result;
}

double bestFutureValue(const State& state, int depth, const Policy& policy,
                       std::uint64_t& work) {
  if (state.game_over) return kTerminalUtility;
  if (depth == 0) return leafValue(state, policy, work);
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(
        best, evaluateAction(state, column, depth, policy, work).value);
  }
  return std::isfinite(best) ? best : kTerminalUtility;
}

struct Decision {
  int action = -1;
  std::uint64_t work = 0;
};

Decision chooseAction(const State& source, const Policy& policy) {
  if (source.game_over) return {};
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  Decision result;
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const double value =
        evaluateAction(canonical, column, kDepth, policy, result.work).value;
    if (value > best) {
      best = value;
      result.action = column;
    }
  }
  if (result.action < 0) throw std::runtime_error("ladder D2 found no action");
  if (mirrored) result.action = kBoardSize - 1 - result.action;
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

Game runGame(std::uint32_t seed, const Policy& policy, int maximum_moves) {
  State state = initialHeadlessState(seed);
  Game result;
  while (!state.game_over && state.moves_played < maximum_moves) {
    const Decision decision = chooseAction(state, policy);
    if (!isLegal(state.board, decision.action)) {
      throw std::runtime_error("ladder D2 returned illegal action");
    }
    result.work += decision.work;
    MoveResult move;
    if (!playHeadlessMove(state, seed, decision.action, move)) {
      throw std::runtime_error("ladder D2 transition failed");
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
  double paired_score_delta = 0.0;
  double paired_move_delta = 0.0;
  double paired_score_lower95 = 0.0;
  double paired_move_lower95 = 0.0;
  double first_half_score_delta = 0.0;
  double first_half_move_delta = 0.0;
  double second_half_score_delta = 0.0;
  double second_half_move_delta = 0.0;
  double lower_quartile_score = 0.0;
  double lower_quartile_moves = 0.0;
  int score_wins = 0;
  int move_wins = 0;
  int censored = 0;
};

double quantile(std::vector<double> values, double probability) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double position = probability * static_cast<double>(values.size() - 1);
  const std::size_t lower = static_cast<std::size_t>(std::floor(position));
  const std::size_t upper = static_cast<std::size_t>(std::ceil(position));
  const double fraction = position - static_cast<double>(lower);
  return values[lower] * (1.0 - fraction) + values[upper] * fraction;
}

double lower95(const std::vector<double>& differences) {
  if (differences.size() < 2) return differences.empty() ? 0.0 : differences[0];
  const double mean =
      std::accumulate(differences.begin(), differences.end(), 0.0) /
      differences.size();
  double squares = 0.0;
  for (const double value : differences) {
    squares += (value - mean) * (value - mean);
  }
  const double deviation = std::sqrt(
      squares / static_cast<double>(differences.size() - 1));
  // The probe's default n=256 has t(255,.975)=1.96931.  Using 1.97 is
  // slightly conservative there and remains a descriptive diagnostic for
  // smaller smoke cohorts, which are never acceptance evidence.
  return mean - 1.97 * deviation / std::sqrt(differences.size());
}

Summary summarize(const std::vector<Game>& games,
                  const std::vector<Game>& stock) {
  Summary result;
  std::uint64_t moves = 0;
  std::vector<double> scores;
  std::vector<double> move_counts;
  std::vector<double> score_differences;
  std::vector<double> move_differences;
  scores.reserve(games.size());
  move_counts.reserve(games.size());
  score_differences.reserve(games.size());
  move_differences.reserve(games.size());
  for (std::size_t index = 0; index < games.size(); ++index) {
    const Game& game = games[index];
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.paired_score_delta +=
        static_cast<double>(game.score - stock[index].score) / games.size();
    result.paired_move_delta +=
        static_cast<double>(game.moves - stock[index].moves) / games.size();
    result.score_wins += game.score > stock[index].score;
    result.move_wins += game.moves > stock[index].moves;
    result.censored += game.censored;
    moves += static_cast<std::uint64_t>(game.moves);
    result.clears_per_move += game.clears;
    result.reveals_per_move += game.reveals;
    result.work_per_move += game.work;
    scores.push_back(static_cast<double>(game.score));
    move_counts.push_back(static_cast<double>(game.moves));
    score_differences.push_back(
        static_cast<double>(game.score - stock[index].score));
    move_differences.push_back(
        static_cast<double>(game.moves - stock[index].moves));
  }
  if (moves > 0) {
    result.clears_per_move /= static_cast<double>(moves);
    result.reveals_per_move /= static_cast<double>(moves);
    result.work_per_move /= static_cast<double>(moves);
  }
  result.paired_score_lower95 = lower95(score_differences);
  result.paired_move_lower95 = lower95(move_differences);
  result.lower_quartile_score = quantile(scores, 0.25);
  result.lower_quartile_moves = quantile(move_counts, 0.25);
  const std::size_t middle = games.size() / 2;
  if (middle > 0 && middle < games.size()) {
    for (std::size_t index = 0; index < games.size(); ++index) {
      const double score_delta =
          static_cast<double>(games[index].score - stock[index].score);
      const double move_delta =
          static_cast<double>(games[index].moves - stock[index].moves);
      if (index < middle) {
        result.first_half_score_delta += score_delta / middle;
        result.first_half_move_delta += move_delta / middle;
      } else {
        const double denominator = static_cast<double>(games.size() - middle);
        result.second_half_score_delta += score_delta / denominator;
        result.second_half_move_delta += move_delta / denominator;
      }
    }
  }
  return result;
}

struct Options {
  std::uint32_t seed_start = kDefaultSeedStart;
  int games = kDefaultGames;
  int maximum_moves = kDefaultMaximumMoves;
  int threads = kDefaultThreads;
  bool self_test = false;
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
  if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
    result.self_test = true;
    return result;
  }
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
        "vertical ladder probe is restricted to the 0x3d training family");
  }
  return result;
}

void expect(bool condition, std::string_view message) {
  if (!condition) throw std::runtime_error(std::string(message));
}

bool selfTest(std::ostream& output) {
  Board board{};
  // Two stored discs become a two-wave vertical ladder after three inert
  // additions: height five pops 5, then height four pops 4.
  board[indexOf(6, 2)] = 5;
  board[indexOf(5, 2)] = 4;
  const LadderFeatures ladder = verticalLadderFeatures(board);
  expect(std::abs(ladder.energy - 1.0) < 1.0e-12,
         "vertical ladder energy mismatch");
  expect(std::abs(ladder.chain_clears - 0.25) < 1.0e-12,
         "vertical ladder clear mismatch");
  expect(ladder.best_waves == 2, "vertical ladder wave mismatch");

  const Board reflected = cfpi::detail::mirrorBoard(board);
  expect(verticalLadderFeatures(reflected).energy == ladder.energy,
         "vertical ladder reflection mismatch");
  Board broken = board;
  broken[indexOf(5, 2)] = 5;
  expect(verticalLadderFeatures(broken).energy == 0.0,
         "broken ladder retained chain energy");

  State fixture;
  fixture.board = board;
  fixture.next_disc = 2;
  fixture.moves_remaining = 3;
  const Decision direct = chooseAction(fixture, kPolicies[0]);
  State mirrored = fixture;
  mirrored.board = reflected;
  const Decision mirror_decision = chooseAction(mirrored, kPolicies[0]);
  expect(mirror_decision.action == kBoardSize - 1 - direct.action,
         "D2 ladder reflection mismatch");
  expect(direct.work <= 2'485, "D2 ladder work bound exceeded");
  output << "vertical ladder self-tests passed (energy " << ladder.energy
         << ", D2 work " << direct.work << ")\n";
  return true;
}

int run(const Options& options) {
  const auto started = Clock::now();
  std::array<std::vector<Game>, kPolicies.size()> results;
  for (auto& games : results) games.resize(options.games);
  const std::size_t jobs =
      kPolicies.size() * static_cast<std::size_t>(options.games);
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
            kPolicies[policy], options.maximum_moves);
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  const double seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  std::cout << std::fixed << std::setprecision(6)
            << "D2_VERTICAL_LADDER {\"seedStart\":" << options.seed_start
            << ",\"games\":" << options.games
            << ",\"trainingOnly\":true,\"seconds\":" << seconds
            << ",\"policies\":[";
  for (std::size_t policy = 0; policy < kPolicies.size(); ++policy) {
    if (policy > 0) std::cout << ',';
    const Summary value = summarize(results[policy], results[0]);
    std::cout << "{\"name\":\"" << kPolicies[policy].name
              << "\",\"energyWeight\":" << kPolicies[policy].energy_weight
              << ",\"meanScore\":" << value.mean_score
              << ",\"meanMoves\":" << value.mean_moves
              << ",\"pairedScoreDelta\":" << value.paired_score_delta
              << ",\"pairedMoveDelta\":" << value.paired_move_delta
              << ",\"pairedScoreLower95\":" << value.paired_score_lower95
              << ",\"pairedMoveLower95\":" << value.paired_move_lower95
              << ",\"firstHalfScoreDelta\":"
              << value.first_half_score_delta
              << ",\"firstHalfMoveDelta\":" << value.first_half_move_delta
              << ",\"secondHalfScoreDelta\":"
              << value.second_half_score_delta
              << ",\"secondHalfMoveDelta\":"
              << value.second_half_move_delta
              << ",\"lowerQuartileScore\":" << value.lower_quartile_score
              << ",\"lowerQuartileMoves\":" << value.lower_quartile_moves
              << ",\"scoreWins\":" << value.score_wins
              << ",\"moveWins\":" << value.move_wins
              << ",\"clearsPerMove\":" << value.clears_per_move
              << ",\"revealsPerMove\":" << value.reveals_per_move
              << ",\"workPerMove\":" << value.work_per_move
              << ",\"censored\":" << value.censored << '}';
  }
  std::cout << "]}\n";
  return 0;
}

}  // namespace drop7::d2_vertical_ladder_probe

#ifndef DROP7_D2_VERTICAL_LADDER_PROBE_LIBRARY
int main(int argc, char** argv) {
  try {
    const auto options =
        drop7::d2_vertical_ladder_probe::parseOptions(argc, argv);
    if (options.self_test) {
      return drop7::d2_vertical_ladder_probe::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    return drop7::d2_vertical_ladder_probe::run(options);
  } catch (const std::exception& error) {
    std::cerr << "drop7_d2_vertical_ladder_probe: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
#endif
