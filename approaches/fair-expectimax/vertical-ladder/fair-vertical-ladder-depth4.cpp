#define DROP7_D2_VERTICAL_LADDER_PROBE_LIBRARY
#include "d2-vertical-ladder-probe.cpp"
#undef DROP7_D2_VERTICAL_LADDER_PROBE_LIBRARY

#include <atomic>
#include <fstream>
#include <future>
#include <mutex>

// Uses a fixed vertical-ladder coefficient of 500 in depth-four search. Nothing
// is retuned here: the leaf is the reference fair leaf plus 500 times the
// conservative vertical-ladder
// energy, and every legal action/chance branch is completed at depth four.
namespace drop7::fair_vertical_ladder_depth4 {

namespace ladder = drop7::d2_vertical_ladder_probe;
namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;
using Clock = std::chrono::steady_clock;

constexpr int kDepth = 4;
constexpr int kChanceSamples = d4::kChanceSamples;
constexpr double kEnergyWeight = 500.0;
constexpr double kTerminalUtility = fair::kTerminalUtility;
constexpr std::uint32_t kTransferStart = 0x3d9f'0000u;
constexpr int kTransferGames = 8;
constexpr int kMaximumMoves = 1'000;
constexpr int kParallelism = 4;
constexpr std::uint64_t kMaximumWork = 3'100'000;
constexpr std::uint64_t kMaximumRssBytes = 128u * 1024u * 1024u;

constexpr std::uint64_t power(std::uint64_t base, int exponent) {
  std::uint64_t result = 1;
  for (int index = 0; index < exponent; ++index) result *= base;
  return result;
}

constexpr std::uint64_t completeWork(int depth) {
  constexpr std::uint64_t branches = kBoardSize * kChanceSamples;
  std::uint64_t transitions = 0;
  for (int ply = 1; ply <= depth; ++ply) transitions += power(branches, ply);
  return transitions + power(branches, depth);
}

constexpr std::uint64_t kWorstCaseWork = completeWork(kDepth);
static_assert(kWorstCaseWork == 3'045'385);
static_assert(kMaximumWork > kWorstCaseWork);
static_assert(kDepth == d4::kCandidateDepth && kChanceSamples == 5);
static_assert(kTransferStart + kTransferGames < 0x3e00'0000u);
static_assert((kTransferStart >> 24u) == 0x3du);

constexpr std::array<int, kBoardSize + 1> kColumnOffsets{{
    0, 1, 10, 91, 820, 7'381, 66'430, 597'871,
}};
constexpr int kColumnStateCount = 5'380'840;
constexpr std::size_t kColumnCacheBytes =
    static_cast<std::size_t>(kColumnStateCount) * sizeof(float);
static_assert(kColumnOffsets.back() + 4'782'969 == kColumnStateCount);
static_assert(kColumnCacheBytes == 21'523'360);

std::mutex progress_mutex;

int columnToken(std::uint8_t cell) {
  if (cell >= 1 && cell <= 7) return static_cast<int>(cell) - 1;
  if (cell == kSolid) return 7;
  if (cell == kCracked) return 8;
  throw std::invalid_argument("invalid occupied ladder token");
}

int columnStateIndex(const Board& board, int column) {
  int height = 0;
  int code = 0;
  for (int row = kBoardSize - 1; row >= 0; --row) {
    const std::uint8_t cell = board[indexOf(row, column)];
    if (cell == kEmpty) continue;
    code = code * 9 + columnToken(cell);
    ++height;
  }
  return kColumnOffsets[height] + code;
}

double cachedColumnEnergy(const Board& board, int column) {
  static std::vector<float> cache(kColumnStateCount, -1.0f);
  const int index = columnStateIndex(board, column);
  std::atomic_ref<float> slot(cache[static_cast<std::size_t>(index)]);
  const float prior = slot.load(std::memory_order_relaxed);
  if (prior >= 0.0f) return prior;
  const float computed = static_cast<float>(
      ladder::verticalLadderColumnFeatures(board, column).energy);
  slot.store(computed, std::memory_order_relaxed);
  return computed;
}

double fastVerticalLadderEnergy(const Board& board) {
  double result = 0.0;
  for (int column = 0; column < kBoardSize; ++column) {
    result += cachedColumnEnergy(board, column);
  }
  return result;
}

struct SearchContext {
  std::uint64_t work = 0;
  std::uint64_t nodes = 0;
};

double leafValue(const State& state, double energy_weight,
                 SearchContext& context) {
  ++context.work;
  const double base = fair::fairLeaf(state);
  if (energy_weight == 0.0 || state.game_over) return base;
  const double result = base + energy_weight *
                                   fastVerticalLadderEnergy(state.board);
  if (!std::isfinite(result)) {
    throw std::runtime_error("vertical ladder D4 leaf was non-finite");
  }
  return result;
}

double bestFutureValue(const State& state, int depth, double energy_weight,
                       SearchContext& context);

struct ActionValue {
  double value = 0.0;
};

ActionValue evaluateAction(const State& state, int column, int depth,
                           double energy_weight, SearchContext& context) {
  const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
      state, fair::kPolicySeed, depth);
  ActionValue result;
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0};
    MoveResult move;
    const bool played =
        cfpi::detail::playMoveSampled(state, column, random, move);
    ++context.work;
    if (!played) {
      result.value += kTerminalUtility;
      continue;
    }
    const double immediate = static_cast<double>(move.score_delta);
    if (move.state.game_over) {
      result.value += immediate + kTerminalUtility;
      continue;
    }
    move.state.score = 0;
    move.state.next_disc = cfpi::detail::sampledNextDisc(
        state_seed, sample, kChanceSamples);
    bool ignored = false;
    const State next = cfpi::detail::canonicalState(move.state, ignored);
    result.value += immediate +
                    bestFutureValue(next, depth - 1, energy_weight, context);
  }
  result.value /= kChanceSamples;
  return result;
}

double bestFutureValue(const State& state, int depth, double energy_weight,
                       SearchContext& context) {
  ++context.nodes;
  if (state.game_over) return kTerminalUtility;
  if (depth == 0) return leafValue(state, energy_weight, context);
  double best = -std::numeric_limits<double>::infinity();
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    best = std::max(
        best,
        evaluateAction(state, column, depth, energy_weight, context).value);
  }
  return std::isfinite(best) ? best : kTerminalUtility;
}

struct Root {
  int action = -1;
  std::array<double, kBoardSize> values{};
  SearchContext context{};
};

Root rootDecision(const State& source, double energy_weight) {
  bool mirrored = false;
  const State state = cfpi::detail::canonicalState(source, mirrored);
  Root result;
  result.values.fill(-std::numeric_limits<double>::infinity());
  double best = -std::numeric_limits<double>::infinity();
  int canonical_action = -1;
  for (const int column : cfpi::detail::kColumnOrder) {
    if (!isLegal(state.board, column)) continue;
    const double value =
        evaluateAction(state, column, kDepth, energy_weight, result.context)
            .value;
    result.values[column] = value;
    if (value > best) {
      best = value;
      canonical_action = column;
    }
  }
  if (canonical_action < 0 || result.context.work > kMaximumWork) {
    throw std::runtime_error("vertical ladder D4 did not complete");
  }
  result.action = mirrored ? kBoardSize - 1 - canonical_action
                           : canonical_action;
  if (mirrored) {
    const auto canonical_values = result.values;
    for (int column = 0; column < kBoardSize; ++column) {
      result.values[kBoardSize - 1 - column] = canonical_values[column];
    }
  }
  return result;
}

struct Game {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::uint64_t clears = 0;
  std::uint64_t reveals = 0;
  std::uint64_t work = 0;
  double seconds = 0.0;
};

Game runGame(std::uint32_t seed, bool candidate) {
  const auto started = Clock::now();
  State state = initialHeadlessState(seed);
  Game result;
  result.seed = seed;
  while (!state.game_over && state.moves_played < kMaximumMoves) {
    int action = -1;
    if (candidate) {
      const Root root = rootDecision(state, kEnergyWeight);
      action = root.action;
      result.work += root.context.work;
    } else {
      const d4::SearchDecision decision = d4::chooseDepth4Action(state);
      if (!decision.complete || decision.completed_depth != kDepth) {
        throw std::runtime_error("stock D4 transfer baseline incomplete");
      }
      action = decision.action;
      result.work += decision.work;
    }
    if (!isLegal(state.board, action)) {
      throw std::runtime_error("vertical ladder D4 returned illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, seed, action, move)) {
      throw std::runtime_error("vertical ladder D4 transition failed");
    }
    for (const Wave& wave : move.waves) {
      result.clears += static_cast<std::uint64_t>(wave.cleared);
      result.reveals += static_cast<std::uint64_t>(wave.revealed);
    }
  }
  result.score = state.score;
  result.moves = state.moves_played;
  result.censored = !state.game_over;
  result.seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Cohort {
  std::array<std::vector<Game>, 2> games;
  double wall_seconds = 0.0;
};

Cohort runCohort() {
  const auto started = Clock::now();
  Cohort result;
  for (auto& values : result.games) values.resize(kTransferGames);
  std::atomic<int> next{0};
  std::vector<std::future<void>> workers;
  constexpr int jobs = 2 * kTransferGames;
  for (int worker = 0; worker < kParallelism; ++worker) {
    workers.push_back(std::async(std::launch::async, [&] {
      for (;;) {
        const int job = next.fetch_add(1);
        if (job >= jobs) return;
        const int game = job / 2;
        const int policy = job % 2;
        const std::uint32_t seed =
            kTransferStart + static_cast<std::uint32_t>(game);
        result.games[policy][game] = runGame(seed, policy == 1);
        const std::lock_guard<std::mutex> lock(progress_mutex);
        const Game& value = result.games[policy][game];
        std::cerr << "ladder-d4 " << (policy == 0 ? "stock" : "candidate")
                  << " seed 0x" << std::hex << seed << std::dec << ' '
                  << value.score << '/' << value.moves << " in "
                  << value.seconds << "s\n";
      }
    }));
  }
  for (auto& worker : workers) worker.get();
  result.wall_seconds =
      std::chrono::duration<double>(Clock::now() - started).count();
  return result;
}

struct Summary {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double lower_quartile_score = 0.0;
  double lower_quartile_moves = 0.0;
  double clears_per_move = 0.0;
  double reveals_per_move = 0.0;
  double work_per_move = 0.0;
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

Summary summarize(const std::vector<Game>& games) {
  Summary result;
  std::vector<double> scores;
  std::vector<double> moves;
  std::uint64_t total_moves = 0;
  for (const Game& game : games) {
    result.mean_score += static_cast<double>(game.score) / games.size();
    result.mean_moves += static_cast<double>(game.moves) / games.size();
    result.censored += game.censored;
    result.clears_per_move += game.clears;
    result.reveals_per_move += game.reveals;
    result.work_per_move += game.work;
    total_moves += static_cast<std::uint64_t>(game.moves);
    scores.push_back(static_cast<double>(game.score));
    moves.push_back(static_cast<double>(game.moves));
  }
  result.lower_quartile_score = quantile(scores, 0.25);
  result.lower_quartile_moves = quantile(moves, 0.25);
  if (total_moves > 0) {
    result.clears_per_move /= static_cast<double>(total_moves);
    result.reveals_per_move /= static_cast<double>(total_moves);
    result.work_per_move /= static_cast<double>(total_moves);
  }
  return result;
}

struct Difference {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  double lower95_score = 0.0;
  double lower95_moves = 0.0;
  double first_half_score = 0.0;
  double first_half_moves = 0.0;
  double second_half_score = 0.0;
  double second_half_moves = 0.0;
  int score_wins = 0;
  int move_wins = 0;
};

double pairedLower95(const std::vector<double>& values) {
  const double mean =
      std::accumulate(values.begin(), values.end(), 0.0) / values.size();
  double squares = 0.0;
  for (const double value : values) squares += (value - mean) * (value - mean);
  const double deviation =
      std::sqrt(squares / static_cast<double>(values.size() - 1));
  constexpr double kT975Df7 = 2.364624;
  return mean - kT975Df7 * deviation / std::sqrt(values.size());
}

Difference compare(const Cohort& cohort) {
  Difference result;
  std::vector<double> scores;
  std::vector<double> moves;
  for (int index = 0; index < kTransferGames; ++index) {
    const Game& stock = cohort.games[0][index];
    const Game& candidate = cohort.games[1][index];
    const double score = static_cast<double>(candidate.score - stock.score);
    const double move = static_cast<double>(candidate.moves - stock.moves);
    scores.push_back(score);
    moves.push_back(move);
    result.mean_score += score / kTransferGames;
    result.mean_moves += move / kTransferGames;
    result.score_wins += score > 0.0;
    result.move_wins += move > 0.0;
    if (index < kTransferGames / 2) {
      result.first_half_score += score / (kTransferGames / 2);
      result.first_half_moves += move / (kTransferGames / 2);
    } else {
      result.second_half_score += score / (kTransferGames / 2);
      result.second_half_moves += move / (kTransferGames / 2);
    }
  }
  result.lower95_score = pairedLower95(scores);
  result.lower95_moves = pairedLower95(moves);
  return result;
}

bool gate(const Summary& stock, const Summary& candidate,
          const Difference& difference) {
  return stock.censored == 0 && candidate.censored == 0 &&
         difference.mean_score > 0.0 && difference.mean_moves > 0.0 &&
         difference.first_half_score > 0.0 &&
         difference.first_half_moves > 0.0 &&
         difference.second_half_score > 0.0 &&
         difference.second_half_moves > 0.0 &&
         difference.score_wins >= 5 && difference.move_wins >= 5 &&
         candidate.clears_per_move >= stock.clears_per_move &&
         candidate.reveals_per_move >= stock.reveals_per_move &&
         candidate.lower_quartile_score >= 0.95 * stock.lower_quartile_score &&
         candidate.lower_quartile_moves >= 0.95 * stock.lower_quartile_moves;
}

void writeSummary(std::ostream& output, const Summary& value) {
  output << "{\"meanScore\":" << value.mean_score
         << ",\"meanMoves\":" << value.mean_moves
         << ",\"lowerQuartileScore\":" << value.lower_quartile_score
         << ",\"lowerQuartileMoves\":" << value.lower_quartile_moves
         << ",\"clearsPerMove\":" << value.clears_per_move
         << ",\"revealsPerMove\":" << value.reveals_per_move
         << ",\"workPerMove\":" << value.work_per_move
         << ",\"censored\":" << value.censored << '}';
}

bool selfTest(std::ostream& output) {
  ladder::selfTest(output);
  State fixture;
  fixture.board.fill(kEmpty);
  fixture.board[indexOf(6, 0)] = kSolid;
  fixture.board[indexOf(6, 1)] = 4;
  fixture.board[indexOf(6, 2)] = 5;
  fixture.board[indexOf(5, 2)] = 4;
  fixture.board[indexOf(6, 4)] = 6;
  fixture.next_disc = 3;
  fixture.moves_remaining = 3;
  const Root zero = rootDecision(fixture, 0.0);
  const d4::SearchDecision stock = d4::chooseDepth4Action(fixture);
  if (zero.action != stock.action || zero.context.work > kMaximumWork) {
    throw std::runtime_error("zero-weight ladder D4 parity failed");
  }
  State metadata = fixture;
  metadata.score = 999'999;
  metadata.level = 87;
  metadata.moves_played = 654;
  if (rootDecision(metadata, kEnergyWeight).action !=
      rootDecision(fixture, kEnergyWeight).action) {
    throw std::runtime_error("ladder D4 used hidden metadata");
  }
  State reflected = fixture;
  reflected.board = cfpi::detail::mirrorBoard(fixture.board);
  if (rootDecision(reflected, kEnergyWeight).action !=
      kBoardSize - 1 - rootDecision(fixture, kEnergyWeight).action) {
    throw std::runtime_error("ladder D4 reflection failed");
  }
  for (std::uint32_t sample = 0; sample < 1'000; ++sample) {
    Board probe{};
    const int column = static_cast<int>(sample % kBoardSize);
    const int height = static_cast<int>((sample / kBoardSize) % 8u);
    std::uint32_t bits = mix32(sample ^ 0x4c41'4444u);
    for (int offset = 0; offset < height; ++offset) {
      bits = mix32(bits + static_cast<std::uint32_t>(offset + 1));
      const int token = static_cast<int>(bits % 9u);
      const std::uint8_t cell =
          token < 7 ? static_cast<std::uint8_t>(token + 1)
                    : static_cast<std::uint8_t>(token == 7 ? kSolid
                                                           : kCracked);
      probe[indexOf(kBoardSize - 1 - offset, column)] = cell;
    }
    const double exact =
        ladder::verticalLadderColumnFeatures(probe, column).energy;
    const double cached = cachedColumnEnergy(probe, column);
    if (exact != cached) {
      throw std::runtime_error("ladder column cache changed exact energy");
    }
  }
  output << "vertical ladder D4 self-tests passed (worst work "
         << kWorstCaseWork << ", column cache " << kColumnCacheBytes
         << " bytes)\n";
  return true;
}

int run(std::string_view output_path, std::ostream& report) {
  const Cohort cohort = runCohort();
  const Summary stock = summarize(cohort.games[0]);
  const Summary candidate = summarize(cohort.games[1]);
  const Difference difference = compare(cohort);
  const bool passed = gate(stock, candidate, difference);
  std::ofstream artifact{std::string(output_path)};
  if (!artifact) throw std::runtime_error("could not open ladder D4 artifact");
  artifact << std::fixed << std::setprecision(9)
           << "{\"experiment\":\"fair-d4-vertical-ladder-transfer\""
           << ",\"seedStart\":" << kTransferStart
           << ",\"games\":" << kTransferGames
           << ",\"trainingOnly\":true,\"energyWeight\":" << kEnergyWeight
           << ",\"depth\":" << kDepth
           << ",\"chanceSamples\":" << kChanceSamples
           << ",\"maximumMoves\":" << kMaximumMoves
           << ",\"stock\":";
  writeSummary(artifact, stock);
  artifact << ",\"candidate\":";
  writeSummary(artifact, candidate);
  artifact << ",\"paired\":{\"scoreDelta\":" << difference.mean_score
           << ",\"moveDelta\":" << difference.mean_moves
           << ",\"scoreLower95\":" << difference.lower95_score
           << ",\"moveLower95\":" << difference.lower95_moves
           << ",\"firstHalfScoreDelta\":" << difference.first_half_score
           << ",\"firstHalfMoveDelta\":" << difference.first_half_moves
           << ",\"secondHalfScoreDelta\":" << difference.second_half_score
           << ",\"secondHalfMoveDelta\":" << difference.second_half_moves
           << ",\"scoreWins\":" << difference.score_wins
           << ",\"moveWins\":" << difference.move_wins
           << "},\"gatePassed\":" << (passed ? "true" : "false")
           << ",\"wallSeconds\":" << cohort.wall_seconds
           << ",\"peakRssBytes\":" << d4::peakRssBytes()
           << ",\"freshGameplaySeedsRead\":false"
           << ",\"protectedSeedsRead\":false}\n";
  artifact.close();
  report << std::fixed << std::setprecision(6)
         << "D4_VERTICAL_LADDER {\"scoreDelta\":" << difference.mean_score
         << ",\"moveDelta\":" << difference.mean_moves
         << ",\"scoreLower95\":" << difference.lower95_score
         << ",\"moveLower95\":" << difference.lower95_moves
         << ",\"passed\":" << (passed ? "true" : "false")
         << ",\"wallSeconds\":" << cohort.wall_seconds << "}\n";
  if (d4::peakRssBytes() > kMaximumRssBytes) {
    throw std::runtime_error("ladder D4 exceeded RSS bound");
  }
  return 0;
}

}  // namespace drop7::fair_vertical_ladder_depth4

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
      return drop7::fair_vertical_ladder_depth4::selfTest(std::cout)
                 ? EXIT_SUCCESS
                 : EXIT_FAILURE;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--run") {
      return drop7::fair_vertical_ladder_depth4::run(argv[2], std::cout);
    }
    std::cerr << "usage: drop7_fair_vertical_ladder_depth4 --self-test | "
                 "--run OUTPUT\n";
    return EXIT_FAILURE;
  } catch (const std::exception& error) {
    std::cerr << "drop7_fair_vertical_ladder_depth4: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
}
