// Parity gate for the scenario engine.
//
// Proves that `ScenarioEngine<StreamRevealSource>` is trajectory-identical to
// `drop7::playHeadlessMove`.  Nothing downstream of this file is trustworthy
// unless this reports zero mismatches: a mismatch means the scenario engine is
// not the same game.
//
// Also checks the two paired transforms (gravity, row rise) directly, and
// reports the marginal reveal distribution of both reveal sources so the claim
// "same marginal, different dynamics" is measured rather than asserted.

#include "scenario.hpp"
#include "scenario-io.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::scenario;

constexpr std::uint32_t kLeaseStart = 0xa51d'c000u;
constexpr std::uint32_t kLeaseEnd = 0xa51d'ffffu;

std::uint32_t leaseSeed(std::uint32_t offset) {
  const std::uint32_t seed = kLeaseStart + offset;
  if (seed > kLeaseEnd) {
    std::cerr << "seed lease SEEDLEASE-A51D-SCEN exhausted\n";
    std::exit(2);
  }
  return seed;
}

int lowestColumnMove(const Board& board) {
  int best = -1;
  int best_height = kBoardSize + 1;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!isLegal(board, column)) continue;
    int height = 0;
    for (int row = 0; row < kBoardSize; ++row) {
      if (board[indexOf(row, column)] != kEmpty) ++height;
    }
    if (height < best_height) {
      best_height = height;
      best = column;
    }
  }
  return best;
}

int centerPolicy(const Board& board) { return centerFirstMove(board); }

// The scenario-engine image of `drop7::playHeadlessMove`: same per-move reveal
// stream derivation, same visible-disc override.
bool playHeadlessScenarioMove(ScenarioEngine<StreamRevealSource>& engine,
                              std::uint32_t game_seed, int column,
                              MoveResult& result) {
  const std::uint32_t reveal_seed =
      mix32(game_seed ^
            (static_cast<std::uint32_t>(engine.state().moves_played + 1) *
             0x85eb'ca6bu) ^
            kRevealDomain);
  Mulberry32 random(reveal_seed);
  engine.source().random = &random;
  if (!engine.play(column, result)) return false;
  if (!engine.state().game_over) {
    engine.mutableState().next_disc =
        headlessDisc(game_seed, engine.state().moves_played);
  }
  return true;
}

struct Counters {
  long long seeds = 0;
  long long moves = 0;
  long long mismatches = 0;
  long long games_ending_naturally = 0;
  long long total_score_reference = 0;
  long long total_score_scenario = 0;
};

bool sameWaves(const std::vector<Wave>& left, const std::vector<Wave>& right) {
  if (left.size() != right.size()) return false;
  for (std::size_t index = 0; index < left.size(); ++index) {
    if (left[index].depth != right[index].depth) return false;
    if (left[index].cleared != right[index].cleared) return false;
    if (left[index].revealed != right[index].revealed) return false;
    if (left[index].points != right[index].points) return false;
  }
  return true;
}

std::string describeWaves(const std::vector<Wave>& waves) {
  std::string out = "[";
  for (std::size_t index = 0; index < waves.size(); ++index) {
    if (index != 0) out += ",";
    out += "{d=" + std::to_string(waves[index].depth) +
           ",c=" + std::to_string(waves[index].cleared) +
           ",r=" + std::to_string(waves[index].revealed) +
           ",p=" + std::to_string(waves[index].points) + "}";
  }
  return out + "]";
}

// Plays one seed under both engines in lockstep and compares every observable.
void compareSeed(std::uint32_t seed, int (*policy)(const Board&),
                 const char* policy_name, int max_moves, Counters& counters) {
  State reference = initialHeadlessState(seed);
  ScenarioEngine<StreamRevealSource> engine(initialHeadlessState(seed),
                                            LatentBoard{},
                                            StreamRevealSource{});
  ++counters.seeds;
  for (int move = 0; move < max_moves; ++move) {
    if (reference.game_over || engine.state().game_over) break;
    const int column = policy(reference.board);
    if (column < 0) break;
    const int scenario_column = policy(engine.state().board);
    MoveResult reference_result;
    MoveResult scenario_result;
    const bool reference_ok =
        playHeadlessMove(reference, seed, column, reference_result);
    const bool scenario_ok = playHeadlessScenarioMove(engine, seed,
                                                      scenario_column,
                                                      scenario_result);
    ++counters.moves;
    bool bad = false;
    std::string what;
    if (column != scenario_column) {
      bad = true;
      what = "policy chose different columns (boards already diverged)";
    }
    if (reference_ok != scenario_ok) {
      bad = true;
      what = "playMove acceptance differs";
    }
    if (!reference_ok) break;
    if (reference.board != engine.state().board) {
      bad = true;
      what = "board differs";
    }
    if (reference.next_disc != engine.state().next_disc) {
      bad = true;
      what = "next disc differs";
    }
    if (reference.score != engine.state().score) {
      bad = true;
      what = "score differs";
    }
    if (reference.moves_remaining != engine.state().moves_remaining) {
      bad = true;
      what = "moves remaining differs";
    }
    if (reference.level != engine.state().level) {
      bad = true;
      what = "level differs";
    }
    if (reference.moves_played != engine.state().moves_played) {
      bad = true;
      what = "moves played differs";
    }
    if (reference.game_over != engine.state().game_over) {
      bad = true;
      what = "game over differs";
    }
    if (reference_result.score_delta != scenario_result.score_delta) {
      bad = true;
      what = "score delta differs";
    }
    if (reference_result.cleared_board != scenario_result.cleared_board) {
      bad = true;
      what = "cleared board flag differs";
    }
    if (reference_result.level_advanced != scenario_result.level_advanced) {
      bad = true;
      what = "level advanced flag differs";
    }
    if (!sameWaves(reference_result.waves, scenario_result.waves)) {
      bad = true;
      what = "wave list differs";
    }
    if (bad) {
      ++counters.mismatches;
      if (counters.mismatches <= 5) {
        std::cerr << "MISMATCH policy=" << policy_name << " seed=0x" << std::hex
                  << seed << std::dec << " move=" << move << ": " << what
                  << "\n  reference board  " << serializeBoard(reference.board)
                  << "\n  scenario  board  "
                  << serializeBoard(engine.state().board)
                  << "\n  reference waves  "
                  << describeWaves(reference_result.waves)
                  << "\n  scenario  waves  "
                  << describeWaves(scenario_result.waves)
                  << "\n  reference score " << reference.score
                  << " scenario score " << engine.state().score << "\n";
      }
      return;
    }
  }
  if (reference.game_over) ++counters.games_ending_naturally;
  counters.total_score_reference += reference.score;
  counters.total_score_scenario += engine.state().score;
}

// Direct check that the paired gravity transform moves the latent array by the
// permutation the board actually experienced.
bool checkGravityPairing(int trials) {
  Mulberry32 random(0x9e37'79b9u);
  for (int trial = 0; trial < trials; ++trial) {
    Board board{};
    LatentBoard tags{};
    constexpr std::array<std::uint8_t, 11> alphabet{
        {kEmpty, kEmpty, 1, 2, 3, 4, 5, 6, 7, kSolid, kCracked}};
    for (int index = 0; index < kCellCount; ++index) {
      board[index] = alphabet[random.nextBits() % alphabet.size()];
      tags[index] = static_cast<std::uint8_t>(index + 1);
    }
    Board out_board{};
    LatentBoard out_tags{};
    applyGravityPaired(board, tags, out_board, out_tags);
    if (out_board != applyGravity(board)) {
      std::cerr << "gravity board result differs from drop7::applyGravity\n";
      return false;
    }
    for (int index = 0; index < kCellCount; ++index) {
      if (out_board[index] == kEmpty) {
        if (out_tags[index] != 0) {
          std::cerr << "gravity left a tag on an empty cell\n";
          return false;
        }
        continue;
      }
      const int source = out_tags[index] - 1;
      if (source < 0 || source >= kCellCount ||
          board[source] != out_board[index]) {
        std::cerr << "gravity latent permutation does not track the board\n";
        return false;
      }
    }
  }
  return true;
}

bool checkRisePairing(int trials) {
  Mulberry32 random(0x8523'11abu);
  for (int trial = 0; trial < trials; ++trial) {
    Board board{};
    LatentBoard tags{};
    for (int column = 0; column < kBoardSize; ++column) {
      const int height = static_cast<int>(random.nextBits() % 6u);
      for (int row = kBoardSize - 1; row >= kBoardSize - height; --row) {
        board[indexOf(row, column)] =
            static_cast<std::uint8_t>(1u + random.nextBits() % 9u);
      }
    }
    for (int index = 0; index < kCellCount; ++index) {
      tags[index] = static_cast<std::uint8_t>(index + 1);
    }
    RiseRow rise{};
    for (int column = 0; column < kBoardSize; ++column) rise[column] = 200;
    Board out_board{};
    LatentBoard out_tags{};
    Board expected{};
    const bool expected_ok = raiseCoveredRow(board, expected);
    const bool ok =
        raiseCoveredRowPaired(board, tags, rise, out_board, out_tags);
    if (ok != expected_ok) {
      std::cerr << "rise legality differs from drop7::raiseCoveredRow\n";
      return false;
    }
    if (!ok) continue;
    if (out_board != expected) {
      std::cerr << "rise board differs from drop7::raiseCoveredRow\n";
      return false;
    }
    for (int row = 0; row < kBoardSize - 1; ++row) {
      for (int column = 0; column < kBoardSize; ++column) {
        if (out_tags[indexOf(row, column)] != tags[indexOf(row + 1, column)]) {
          std::cerr << "rise latent shift does not match the board shift\n";
          return false;
        }
      }
    }
    for (int column = 0; column < kBoardSize; ++column) {
      if (out_tags[indexOf(kBoardSize - 1, column)] != 200 ||
          out_board[indexOf(kBoardSize - 1, column)] != kSolid) {
        std::cerr << "risen row did not take its specified latent values\n";
        return false;
      }
    }
  }
  return true;
}

// A latent-source game reveals exactly the values that were placed under the
// covered cells.  This checks the defining property of LatentRevealSource.
bool checkLatentConsumption(int seeds) {
  for (int index = 0; index < seeds; ++index) {
    const std::uint32_t seed = leaseSeed(0x1000u + static_cast<std::uint32_t>(index));
    Mulberry32 random(seed);
    Scenario scenario;
    scenario.board = initialBoard();
    for (int cell = 0; cell < kCellCount; ++cell) {
      scenario.latent[cell] =
          scenario.board[cell] == kSolid ? random.nextDisc() : 0;
    }
    scenario.moves_remaining = kMovesPerLevel;
    scenario.horizon = 12;
    for (int move = 0; move < scenario.horizon; ++move) {
      scenario.disc_tape.push_back(random.nextDisc());
    }
    const int rises = riseRowCount(scenario.horizon, scenario.moves_remaining);
    for (int row = 0; row < rises; ++row) {
      RiseRow values{};
      for (int column = 0; column < kBoardSize; ++column) {
        values[column] = random.nextDisc();
      }
      scenario.rise_latent.push_back(values);
    }
    assignScenarioId(scenario);
    std::string reason;
    if (!validateScenario(scenario, reason)) {
      std::cerr << "generated scenario invalid: " << reason << "\n";
      return false;
    }
    // Round-trip through JSONL.
    Scenario reloaded;
    std::string error;
    if (!deserializeScenario(serializeScenario(scenario), reloaded, error)) {
      std::cerr << "scenario JSONL round trip failed: " << error << "\n";
      return false;
    }
    if (reloaded.board != scenario.board ||
        reloaded.latent != scenario.latent ||
        reloaded.disc_tape != scenario.disc_tape ||
        reloaded.rise_latent != scenario.rise_latent ||
        reloaded.moves_remaining != scenario.moves_remaining ||
        reloaded.horizon != scenario.horizon ||
        std::string(reloaded.id) != std::string(scenario.id)) {
      std::cerr << "scenario JSONL round trip lost information\n";
      return false;
    }

    auto engine = makeScenarioEngine(scenario);
    for (int move = 0; move < scenario.horizon; ++move) {
      if (engine.state().game_over) break;
      const LatentBoard before = engine.latent();
      const Board before_board = engine.state().board;
      const int column = lowestColumnMove(engine.state().board);
      if (column < 0) break;
      MoveResult result;
      if (!engine.play(column, result)) break;
      // Every revealed value must have been one of the latent values that were
      // sitting under a covered cell beforehand.
      int revealed = 0;
      for (const Wave& wave : result.waves) revealed += wave.revealed;
      if (revealed > 0) {
        int covered_before = 0;
        for (int cell = 0; cell < kCellCount; ++cell) {
          if (before_board[cell] == kSolid || before_board[cell] == kCracked) {
            ++covered_before;
          }
          if ((before_board[cell] == kSolid ||
               before_board[cell] == kCracked) !=
              (before[cell] != 0)) {
            std::cerr << "latent array out of step with the board\n";
            return false;
          }
        }
        if (revealed > covered_before + kBoardSize) {
          std::cerr << "more reveals than covered cells\n";
          return false;
        }
      }
      if (engine.source().invalid_latent) {
        std::cerr << "revealed a covered cell with no latent value\n";
        return false;
      }
      for (int cell = 0; cell < kCellCount; ++cell) {
        const std::uint8_t board_cell = engine.state().board[cell];
        const bool covered = board_cell == kSolid || board_cell == kCracked;
        if (covered != (engine.latent()[cell] != 0)) {
          std::cerr << "latent invariant violated after move " << move << "\n";
          return false;
        }
      }
    }
  }
  return true;
}

void reportRevealMarginals() {
  std::array<long long, 8> stream{};
  Mulberry32 random(leaseSeed(0x1100u));
  for (int draw = 0; draw < 700000; ++draw) ++stream[random.nextDisc()];
  double chi = 0.0;
  const double expected = 700000.0 / 7.0;
  for (int value = 1; value <= 7; ++value) {
    const double diff = static_cast<double>(stream[value]) - expected;
    chi += diff * diff / expected;
  }
  std::printf("reveal marginal chi-square (6 df, 700k draws): %.3f\n", chi);
}

}  // namespace

int main(int argc, char** argv) {
  int seeds = 512;
  int max_moves = 2000;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--seeds" && index + 1 < argc) {
      seeds = std::atoi(argv[++index]);
    } else if (flag == "--max-moves" && index + 1 < argc) {
      max_moves = std::atoi(argv[++index]);
    }
  }

  std::printf("scenario parity gate\n");
  std::printf("lease SEEDLEASE-A51D-SCEN 0x%08x-0x%08x\n", kLeaseStart,
              kLeaseEnd);

  if (!checkGravityPairing(20000)) {
    std::printf("FAIL: paired gravity transform\n");
    return 1;
  }
  std::printf("paired gravity transform: 20000 random boards, OK\n");

  if (!checkRisePairing(20000)) {
    std::printf("FAIL: paired rise transform\n");
    return 1;
  }
  std::printf("paired rise transform: 20000 random boards, OK\n");

  Counters center;
  Counters lowest;
  for (int index = 0; index < seeds; ++index) {
    const std::uint32_t seed = leaseSeed(static_cast<std::uint32_t>(index));
    compareSeed(seed, centerPolicy, "center-first", max_moves, center);
    compareSeed(seed, lowestColumnMove, "lowest-column", max_moves, lowest);
  }

  std::printf(
      "center-first  : seeds=%lld moves=%lld mismatches=%lld "
      "reference_total_score=%lld scenario_total_score=%lld\n",
      center.seeds, center.moves, center.mismatches,
      center.total_score_reference, center.total_score_scenario);
  std::printf(
      "lowest-column : seeds=%lld moves=%lld mismatches=%lld "
      "reference_total_score=%lld scenario_total_score=%lld\n",
      lowest.seeds, lowest.moves, lowest.mismatches,
      lowest.total_score_reference, lowest.total_score_scenario);

  const long long mismatches = center.mismatches + lowest.mismatches;
  const long long moves = center.moves + lowest.moves;

  if (!checkLatentConsumption(64)) {
    std::printf("FAIL: latent reveal source invariants\n");
    return 1;
  }
  std::printf("latent source invariants + JSONL round trip: 64 games, OK\n");

  reportRevealMarginals();

  std::printf("TOTAL: seeds=%d policies=2 game-plays=%lld moves=%lld mismatches=%lld\n",
              seeds, center.seeds + lowest.seeds, moves, mismatches);
  if (mismatches != 0) {
    std::printf("PARITY GATE FAILED\n");
    return 1;
  }
  std::printf("PARITY GATE PASSED\n");
  return 0;
}
