#define DROP7_FAIR_ONLY_DEPTH4_NO_MAIN
#include "../../fair-expectimax/reference/fair-only-depth4.cpp"
#undef DROP7_FAIR_ONLY_DEPTH4_NO_MAIN

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

// Read-only trajectory instrumentation for previously evaluated development
// seeds.
// It does not define a candidate policy or read any validation/final cohort.
namespace drop7::d4_flow_audit {

namespace d4 = drop7::fair_only_depth4;
namespace fair = drop7::fair_only_horizon;

constexpr std::uint32_t kAllowedSeedStart = 0x3d6e'4000u;
constexpr std::uint32_t kAllowedSeedEnd = 0x3d6e'4003u;
constexpr int kDefaultMaximumMoves = 500;

struct Options {
  std::uint32_t seed = 0x3d6e'4001u;
  int maximum_moves = kDefaultMaximumMoves;
  std::string output = "/tmp/drop7-d4-flow-audit.jsonl";
};

std::uint32_t parseSeed(std::string_view text) {
  std::size_t consumed = 0;
  const unsigned long value = std::stoul(std::string(text), &consumed, 0);
  if (consumed != text.size() || value > 0xffff'fffful) {
    throw std::invalid_argument("invalid seed");
  }
  return static_cast<std::uint32_t>(value);
}

Options parseOptions(int argc, char** argv) {
  Options result;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index];
    if (argument == "--seed" && index + 1 < argc) {
      result.seed = parseSeed(argv[++index]);
    } else if (argument == "--max-moves" && index + 1 < argc) {
      result.maximum_moves = std::stoi(argv[++index]);
    } else if (argument == "--output" && index + 1 < argc) {
      result.output = argv[++index];
    } else {
      throw std::invalid_argument("unknown or incomplete argument");
    }
  }
  if (result.seed < kAllowedSeedStart || result.seed > kAllowedSeedEnd) {
    throw std::invalid_argument(
        "flow audit accepts only previously consumed 0x3d fitting seeds");
  }
  if (result.maximum_moves < 1 || result.maximum_moves > 1'000) {
    throw std::invalid_argument("max moves must be in [1, 1000]");
  }
  return result;
}

int occupiedCells(const Board& board) {
  return static_cast<int>(std::count_if(
      board.begin(), board.end(), [](std::uint8_t cell) {
        return cell != kEmpty;
      }));
}

int coveredCells(const Board& board) {
  return static_cast<int>(std::count_if(
      board.begin(), board.end(), [](std::uint8_t cell) {
        return cell == kSolid || cell == kCracked;
      }));
}

int maximumHeight(const Board& board) {
  const auto heights = cfpi::detail::columnHeights(board);
  return *std::max_element(heights.begin(), heights.end());
}

void writeStateFeatures(std::ostream& output, const State& state) {
  const fair::FairFeatures fair_features = fair::extractFairFeatures(state);
  const cfpi::detail::PhaseFeatures& phase = fair_features.heuristic;
  output << "\"board\":\"" << serializeBoard(state.board) << "\""
         << ",\"nextDisc\":" << static_cast<int>(state.next_disc)
         << ",\"movesRemaining\":" << state.moves_remaining
         << ",\"occupied\":" << occupiedCells(state.board)
         << ",\"covers\":" << coveredCells(state.board)
         << ",\"maximumHeight\":" << maximumHeight(state.board)
         << ",\"fairLeaf\":" << fair::fairLeaf(state)
         << ",\"directPotential\":" << phase.direct_potential
         << ",\"latentPotential\":" << phase.latent_chain_potential
         << ",\"crackedExposure\":" << phase.cracked_exposure
         << ",\"solidExposure\":" << phase.solid_exposure
         << ",\"adjacentOnes\":" << phase.adjacent_ones
         << ",\"tripleTwos\":" << phase.triple_twos
         << ",\"deadLowNumbers\":" << phase.dead_low_numbers
         << ",\"quietBuildOptions\":" << phase.quiet_build_options
         << ",\"quietDirectGain\":" << phase.quiet_direct_gain
         << ",\"triggerReadiness\":" << phase.trigger_readiness
         << ",\"riseTriggerReadiness\":" << phase.rise_trigger_readiness
         << ",\"projectedOccupancyDebt\":"
         << phase.projected_occupancy_debt
         << ",\"coverAltitudeDebt\":" << phase.cover_altitude_debt
         << ",\"peakHeightRisk\":" << phase.peak_height_risk
         << ",\"lowCapLoad\":" << phase.low_cap_load
         << ",\"adjacentLowCapLoad\":" << phase.adjacent_low_cap_load;
}

int run(const Options& options) {
  std::ofstream output(options.output);
  if (!output) throw std::runtime_error("could not open output");
  output << std::setprecision(12);

  State state = initialHeadlessState(options.seed);
  std::uint64_t cleared = 0;
  std::uint64_t revealed = 0;
  while (!state.game_over && state.moves_played < options.maximum_moves) {
    const State before = state;
    const d4::SearchDecision decision = d4::chooseDepth4Action(before);
    if (!decision.complete || decision.completed_depth != d4::kCandidateDepth) {
      throw std::runtime_error("D4 did not complete");
    }
    if (!isLegal(before.board, decision.action)) {
      throw std::runtime_error("D4 chose an illegal action");
    }
    MoveResult move;
    if (!playHeadlessMove(state, options.seed, decision.action, move)) {
      throw std::runtime_error("headless transition failed");
    }
    int move_cleared = 0;
    int move_revealed = 0;
    for (const Wave& wave : move.waves) {
      move_cleared += wave.cleared;
      move_revealed += wave.revealed;
    }
    cleared += static_cast<std::uint64_t>(move_cleared);
    revealed += static_cast<std::uint64_t>(move_revealed);

    output << '{'
           << "\"seed\":" << options.seed
           << ",\"move\":" << before.moves_played
           << ",\"scoreBefore\":" << before.score
           << ",\"action\":" << decision.action
           << ",\"depth3Action\":" << decision.depth3_action
           << ",\"d4Value\":" << decision.root_values[decision.action]
           << ",\"expectedImmediateScore\":"
           << decision.root_expected_scores[decision.action] << ',';
    writeStateFeatures(output, before);
    output << ",\"scoreDelta\":" << move.score_delta
           << ",\"moveCleared\":" << move_cleared
           << ",\"moveRevealed\":" << move_revealed
           << ",\"waveCount\":" << move.waves.size()
           << ",\"levelAdvanced\":"
           << (move.level_advanced ? "true" : "false")
           << ",\"gameOverAfter\":"
           << (state.game_over ? "true" : "false") << "}\n";

    if (state.moves_played % 25 == 0 || state.game_over) {
      std::cerr << "seed 0x" << std::hex << options.seed << std::dec
                << " move " << state.moves_played << " score " << state.score
                << " clears/move "
                << static_cast<double>(cleared) / state.moves_played
                << " reveals/move "
                << static_cast<double>(revealed) / state.moves_played << '\n';
    }
  }
  std::cerr << "finished score " << state.score << " moves "
            << state.moves_played << (state.game_over ? " natural" : " capped")
            << '\n';
  return 0;
}

}  // namespace drop7::d4_flow_audit

int main(int argc, char** argv) {
  try {
    return drop7::d4_flow_audit::run(
        drop7::d4_flow_audit::parseOptions(argc, argv));
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
