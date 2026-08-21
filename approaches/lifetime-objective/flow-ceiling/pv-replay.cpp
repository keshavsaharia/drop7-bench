// pv-replay - replay a clairvoyant principal variation and measure its flow.
//
//   pv-replay --input <solved.jsonl> [--jsonl <per-move.jsonl>]
//             [--also-fair] [--verbose <scenario-id>]
//
// `<solved.jsonl>` is the output of
// `build/scenario/solve --input <suite.jsonl> --jsonl <solved.jsonl>`: every
// line carries the full scenario record plus `principalVariation`, `optimum`,
// `optimalMovesSurvived`, `optimalClearCount` and `optimalMaxChainDepth`.
//
// The replay re-runs the stored line through the scenario engine and reports,
// per move and aggregated:
//
//   * numbered discs cleared and covered cells revealed  (the flow rates that
//     finding-01 shows must reach 2.400 and 1.400 per move for a policy to
//     survive indefinitely),
//   * wave depths,
//   * score split into row-rise bonus, board-clear bonus and chain waves,
//   * board occupancy before and after the line.
//
// The replayed score is checked against the stored `optimum` on every scenario;
// a mismatch is reported rather than absorbed.
//
// `--also-fair` plays the frozen fair depth-4 comparator over the same
// scenario, the same tape and the same latent board, so the clairvoyant flow
// rate has a paired public-information control measured by the same code.

#include "fair-only-depth4-noentry.cpp"

#include "flow-common.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::scenario;
using namespace drop7::flowceiling;
namespace d4 = drop7::fair_only_depth4;

// Identical to `mint.cpp`'s `fairAction` for depth four: the repository
// comparator, unmodified, reading only public state.
int fairDepth4Action(const State& state) {
  if (state.game_over) return -1;
  const d4::SearchDecision decision = d4::chooseDepth4Action(state);
  return decision.action;
}

struct SolvedLine {
  Scenario scenario;
  std::vector<int> pv;
  std::int64_t optimum = 0;
  bool complete = false;
};

bool parseSolvedLine(const std::string& line, SolvedLine& out,
                     std::string& error) {
  if (!deserializeScenario(line, out.scenario, error)) return false;
  std::vector<std::uint8_t> columns;
  if (!io_detail::parseFlatArrayKey(line, "principalVariation", columns)) {
    error = "principalVariation missing";
    return false;
  }
  out.pv.clear();
  for (std::uint8_t column : columns) out.pv.push_back(column);
  long long optimum = 0;
  if (!io_detail::parseInt(line, "optimum", optimum)) {
    error = "optimum missing";
    return false;
  }
  out.optimum = optimum;
  out.complete = line.find("\"complete\":true") != std::string::npos;
  return true;
}

// One replayed line, accumulated with exactly the accounting `flow-common.hpp`
// uses for whole games.
struct LineStat {
  GameStat game;
  int start_occupied = 0;
  int start_covered = 0;
  int end_occupied = 0;
  int end_covered = 0;
  std::int64_t replayed = 0;
  bool matched_optimum = false;
};

template <typename Chooser>
LineStat replayLine(const Scenario& scenario, Chooser& chooser,
                    std::vector<MoveStat>* per_move) {
  LineStat out;
  out.start_occupied = occupiedCellCount(scenario.board);
  out.start_covered = coveredCellCount(scenario.board);
  out.end_occupied = out.start_occupied;
  out.end_covered = out.start_covered;
  auto engine = makeScenarioEngine(scenario);
  for (int move = 0; move < scenario.horizon; ++move) {
    if (engine.state().game_over) break;
    const int column = chooser(engine.state(), move);
    if (column < 0 || !isLegal(engine.state().board, column)) break;
    const int disc = engine.state().next_disc;
    MoveResult result;
    if (!engine.play(column, result)) break;
    const MoveStat stat = describeMove(move + 1, column, disc, result);
    out.game.absorb(stat, result);
    if (per_move != nullptr) per_move->push_back(stat);
    out.replayed += stat.delta;
    out.end_occupied = stat.occupied_after;
    out.end_covered = stat.covered_after;
    if (engine.state().game_over) {
      out.game.died = true;
      break;
    }
  }
  return out;
}

struct Aggregate {
  int lines = 0;
  int died = 0;
  std::int64_t moves = 0;
  std::int64_t cleared = 0;
  std::int64_t revealed = 0;
  std::int64_t score = 0;
  std::int64_t chain_points = 0;
  std::int64_t rise_points = 0;
  std::int64_t clear_points = 0;
  int rises = 0;
  int clear_moves = 0;
  int clear_awards = 0;
  int double_clear_awards = 0;
  int fifth_drop_clears = 0;
  int identity_violations = 0;
  int max_wave_depth = 0;
  std::int64_t start_occupied = 0;
  std::int64_t end_occupied = 0;
  std::int64_t start_covered = 0;
  std::int64_t end_covered = 0;
  std::array<std::int64_t, kMaxWaveDepth> wave_depth_count{};
  std::array<std::int64_t, kMaxWaveDepth> wave_depth_cleared{};
  std::vector<double> per_line_clears;
  std::vector<double> per_line_reveals;

  void absorb(const LineStat& line) {
    ++lines;
    if (line.game.died) ++died;
    moves += line.game.moves;
    cleared += line.game.cleared;
    revealed += line.game.revealed;
    score += line.game.score;
    chain_points += line.game.chain_points;
    rise_points += line.game.rise_points;
    clear_points += line.game.clear_points;
    rises += line.game.rises;
    clear_moves += line.game.clear_moves;
    clear_awards += line.game.clear_awards;
    double_clear_awards += line.game.double_clear_awards;
    fifth_drop_clears += line.game.fifth_drop_clears;
    identity_violations += line.game.identity_violations;
    max_wave_depth = std::max(max_wave_depth, line.game.max_wave_depth);
    start_occupied += line.start_occupied;
    end_occupied += line.end_occupied;
    start_covered += line.start_covered;
    end_covered += line.end_covered;
    for (int depth = 0; depth < kMaxWaveDepth; ++depth) {
      wave_depth_count[static_cast<std::size_t>(depth)] +=
          line.game.wave_depth_count[static_cast<std::size_t>(depth)];
      wave_depth_cleared[static_cast<std::size_t>(depth)] +=
          line.game.wave_depth_cleared[static_cast<std::size_t>(depth)];
    }
    if (line.game.moves > 0) {
      per_line_clears.push_back(line.game.clearsPerMove());
      per_line_reveals.push_back(line.game.revealsPerMove());
    }
  }
};

double mean(const std::vector<double>& values) {
  if (values.empty()) return 0.0;
  double total = 0.0;
  for (double value : values) total += value;
  return total / static_cast<double>(values.size());
}

void reportAggregate(const char* label, const Aggregate& aggregate) {
  std::printf("\n=== %s ===\n", label);
  std::printf("lines %d, died inside the horizon %d, moves played %lld\n",
              aggregate.lines, aggregate.died,
              static_cast<long long>(aggregate.moves));
  const double moves = static_cast<double>(aggregate.moves);
  std::printf(
      "clears/move %.4f   (requirement 2.4000)   reveals/move %.4f   "
      "(requirement 1.4000)\n",
      moves > 0 ? aggregate.cleared / moves : 0.0,
      moves > 0 ? aggregate.revealed / moves : 0.0);
  std::printf("per-line mean clears/move %.4f   reveals/move %.4f\n",
              mean(aggregate.per_line_clears), mean(aggregate.per_line_reveals));
  std::printf("score %lld = rise %lld + clear %lld + chain %lld\n",
              static_cast<long long>(aggregate.score),
              static_cast<long long>(aggregate.rise_points),
              static_cast<long long>(aggregate.clear_points),
              static_cast<long long>(aggregate.chain_points));
  if (aggregate.score > 0) {
    const double total = static_cast<double>(aggregate.score);
    std::printf("share  rise %.2f%%  clear %.2f%%  chain %.2f%%\n",
                100.0 * aggregate.rise_points / total,
                100.0 * aggregate.clear_points / total,
                100.0 * aggregate.chain_points / total);
  }
  std::printf(
      "rises %d, board-clear moves %d, 70k awards %d, double awards %d, "
      "fifth-drop clears %d, identity violations %d\n",
      aggregate.rises, aggregate.clear_moves, aggregate.clear_awards,
      aggregate.double_clear_awards, aggregate.fifth_drop_clears,
      aggregate.identity_violations);
  std::printf("occupancy mean start %.2f -> end %.2f (covered %.2f -> %.2f)\n",
              aggregate.lines > 0
                  ? static_cast<double>(aggregate.start_occupied) /
                        aggregate.lines
                  : 0.0,
              aggregate.lines > 0
                  ? static_cast<double>(aggregate.end_occupied) / aggregate.lines
                  : 0.0,
              aggregate.lines > 0
                  ? static_cast<double>(aggregate.start_covered) /
                        aggregate.lines
                  : 0.0,
              aggregate.lines > 0
                  ? static_cast<double>(aggregate.end_covered) / aggregate.lines
                  : 0.0);
  std::int64_t waves = 0;
  for (std::int64_t count : aggregate.wave_depth_count) waves += count;
  std::printf("waves %lld, deepest %d\n", static_cast<long long>(waves),
              aggregate.max_wave_depth);
  std::printf("depth  waves    share   discs cleared\n");
  for (int depth = 1; depth < kMaxWaveDepth; ++depth) {
    const std::int64_t count =
        aggregate.wave_depth_count[static_cast<std::size_t>(depth)];
    if (count == 0) continue;
    std::printf("%5d  %6lld  %6.2f%%  %10lld\n", depth,
                static_cast<long long>(count),
                waves > 0 ? 100.0 * static_cast<double>(count) / waves : 0.0,
                static_cast<long long>(
                    aggregate.wave_depth_cleared[static_cast<std::size_t>(
                        depth)]));
  }
}

struct Options {
  std::string input;
  std::string jsonl_output;
  std::string verbose_id;
  bool also_fair = false;
};

int run(const Options& options) {
  std::ifstream input(options.input);
  if (!input) {
    std::cerr << "cannot open " << options.input << "\n";
    return 2;
  }
  std::ofstream jsonl;
  if (!options.jsonl_output.empty()) {
    jsonl.open(options.jsonl_output);
    if (!jsonl) {
      std::cerr << "cannot write " << options.jsonl_output << "\n";
      return 2;
    }
  }

  Aggregate optimal;
  Aggregate fair;
  int mismatches = 0;
  int incomplete = 0;
  std::string line;
  int line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.find_first_not_of(" \t\r\n") == std::string::npos) continue;
    SolvedLine solved;
    std::string error;
    if (!parseSolvedLine(line, solved, error)) {
      std::cerr << options.input << ":" << line_number << ": " << error << "\n";
      return 2;
    }
    if (!solved.complete) ++incomplete;

    std::vector<MoveStat> per_move;
    auto pv_chooser = [&solved](const State&, int move) {
      return move < static_cast<int>(solved.pv.size()) ? solved.pv[move] : -1;
    };
    LineStat stat = replayLine(solved.scenario, pv_chooser, &per_move);
    stat.matched_optimum = stat.replayed == solved.optimum;
    if (!stat.matched_optimum) ++mismatches;
    optimal.absorb(stat);

    LineStat fair_stat;
    if (options.also_fair) {
      auto fair_chooser = [](const State& state, int) {
        return fairDepth4Action(state);
      };
      fair_stat = replayLine(solved.scenario, fair_chooser, nullptr);
      fair.absorb(fair_stat);
    }

    if (jsonl) {
      jsonl << "{\"schema\":\"drop7-pv-replay-v1\",\"id\":\""
            << solved.scenario.id << "\""
            << ",\"horizon\":" << static_cast<int>(solved.scenario.horizon)
            << ",\"optimum\":" << solved.optimum
            << ",\"replayed\":" << stat.replayed
            << ",\"matchedOptimum\":" << (stat.matched_optimum ? "true" : "false")
            << ",\"startOccupied\":" << stat.start_occupied
            << ",\"startCovered\":" << stat.start_covered
            << ",\"endOccupied\":" << stat.end_occupied
            << ",\"endCovered\":" << stat.end_covered
            << ",\"moves\":" << stat.game.moves
            << ",\"cleared\":" << stat.game.cleared
            << ",\"revealed\":" << stat.game.revealed
            << ",\"clearsPerMove\":" << stat.game.clearsPerMove()
            << ",\"revealsPerMove\":" << stat.game.revealsPerMove()
            << ",\"risePoints\":" << stat.game.rise_points
            << ",\"clearPoints\":" << stat.game.clear_points
            << ",\"chainPoints\":" << stat.game.chain_points
            << ",\"clearAwards\":" << stat.game.clear_awards
            << ",\"doubleClearAwards\":" << stat.game.double_clear_awards
            << ",\"fifthDropClears\":" << stat.game.fifth_drop_clears
            << ",\"maxWaveDepth\":" << stat.game.max_wave_depth
            << ",\"died\":" << (stat.game.died ? "true" : "false");
      if (options.also_fair) {
        jsonl << ",\"fairMoves\":" << fair_stat.game.moves
              << ",\"fairCleared\":" << fair_stat.game.cleared
              << ",\"fairRevealed\":" << fair_stat.game.revealed
              << ",\"fairScore\":" << fair_stat.game.score
              << ",\"fairMaxWaveDepth\":" << fair_stat.game.max_wave_depth
              << ",\"fairDied\":" << (fair_stat.game.died ? "true" : "false");
      }
      jsonl << ",\"perMove\":[";
      for (std::size_t index = 0; index < per_move.size(); ++index) {
        const MoveStat& move = per_move[index];
        if (index != 0) jsonl << ',';
        jsonl << "{\"move\":" << move.move_index << ",\"col\":" << move.column
              << ",\"disc\":" << move.disc << ",\"cleared\":" << move.cleared
              << ",\"revealed\":" << move.revealed
              << ",\"waves\":" << move.wave_count
              << ",\"maxDepth\":" << move.max_wave_depth
              << ",\"delta\":" << move.delta
              << ",\"chain\":" << move.chain_points
              << ",\"rise\":" << move.rise_points
              << ",\"clearBonus\":" << move.clear_points
              << ",\"clearAwards\":" << move.clear_awards
              << ",\"occupied\":" << move.occupied_after
              << ",\"covered\":" << move.covered_after << "}";
      }
      jsonl << "]}\n";
    }

    if (!options.verbose_id.empty() &&
        options.verbose_id == std::string(solved.scenario.id)) {
      std::printf("scenario %s H=%d optimum=%lld replayed=%lld\n",
                  solved.scenario.id,
                  static_cast<int>(solved.scenario.horizon),
                  static_cast<long long>(solved.optimum),
                  static_cast<long long>(stat.replayed));
      for (const MoveStat& move : per_move) {
        std::printf(
            "  move %2d col %d disc %d cleared %2d revealed %2d waves %2d "
            "maxdepth %2d delta %8lld (chain %7lld rise %6lld clear %6lld) "
            "occupied %2d covered %2d\n",
            move.move_index, move.column, move.disc, move.cleared,
            move.revealed, move.wave_count, move.max_wave_depth,
            static_cast<long long>(move.delta),
            static_cast<long long>(move.chain_points),
            static_cast<long long>(move.rise_points),
            static_cast<long long>(move.clear_points), move.occupied_after,
            move.covered_after);
      }
    }
  }

  std::printf("replayed %d line(s); optimum mismatches %d; solver-incomplete "
              "lines %d\n",
              optimal.lines, mismatches, incomplete);
  reportAggregate("clairvoyant optimal line", optimal);
  if (options.also_fair) {
    reportAggregate("fair depth-4 on the same scenarios", fair);
  }
  return mismatches == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--input" && index + 1 < argc) {
      options.input = argv[++index];
    } else if (flag == "--jsonl" && index + 1 < argc) {
      options.jsonl_output = argv[++index];
    } else if (flag == "--verbose" && index + 1 < argc) {
      options.verbose_id = argv[++index];
    } else if (flag == "--also-fair") {
      options.also_fair = true;
    } else {
      std::cerr << "unknown argument " << flag << "\n";
      return 2;
    }
  }
  if (options.input.empty()) {
    std::cerr << "usage: pv-replay --input <solved.jsonl> [--jsonl out.jsonl] "
                 "[--also-fair] [--verbose <id>]\n";
    return 2;
  }
  return run(options);
}
