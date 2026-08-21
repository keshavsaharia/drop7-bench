#pragma once

// A receding-horizon planner that may NOT read the hidden board.
//
// Why this exists
// ---------------
// `docs/exploratory/finding-06-flow-ceiling.md` measured a clairvoyant
// receding-horizon planner sustaining the disc-conservation requirement, and
// described the gap to fair depth 4 as "a control gap, not an information gap".
// That measurement cannot support the claim, because the clairvoyant planner is
// privileged twice over: it plans exactly over a window *and* it knows which
// covered disc holds which number, so it knows exactly which covers are worth
// cracking.  An unknown share of its advantage is hidden information that no
// legal policy can ever recover.
//
// This header separates the two privileges by removing them one at a time.
//
//   * `latent_known = false`  - the hidden values under covered cells are
//     unknown.  The planner draws `samples` independent completions of the
//     hidden board from the correct i.i.d. uniform 1..7 marginal, solves the
//     window exactly against each, and plays the move with the best mean value.
//   * `tape_known = false`    - the future numbered discs and the hidden values
//     of future risen rows are unknown too, and are drawn from the same
//     marginal.  Only the next visible disc, which is public state, is used.
//
// With both false the planner reads exactly what `docs/methodology.md` permits
// a deployable policy to read: the visible board, the visible next disc, and
// the moves remaining until the next rise.  The resulting policy is legal.  The
// planner is a determinized / hindsight-optimization (PIMC) controller: within
// a sample it assumes the drawn hidden values will turn out to be true, which
// makes its *value estimates* optimistically biased, but the action it emits is
// a function of public state alone, so the flow rate it achieves in play is an
// achievable-by-a-legal-policy rate.  That is the quantity in question.
//
// Common random numbers: every candidate column inside one decision is scored
// against the same `samples` hidden boards, via `solveWindowRoot`, which
// returns the exact value of every legal root move under one completion.  The
// comparison between columns is therefore paired at the sample level.

#include "flow-common.hpp"
#include "flow-solver.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace drop7::flowceiling {

struct FairPlannerConfig {
  Objective objective = Objective::kClears;
  int horizon = 7;
  int samples = 8;
  bool latent_known = false;
  bool tape_known = false;
  // Solves the K sampled windows on a thread pool.  The K scenarios are still
  // drawn *serially* from the sampler stream before any of them is solved, so
  // the sequence of sampled worlds — and therefore the decision — is bit
  // identical to a single-threaded run at the same seed.  Only the solving is
  // parallel.  This keeps the K series in finding-07 directly comparable.
  int sample_threads = 1;
  WindowLimits limits{};
};

// Builds one sampled window from public state plus a draw of everything hidden.
// `truth` supplies the hidden quantities that this configuration is allowed to
// know; the rest come from `random`.
inline bool sampleWindow(const FairPlannerConfig& config, const MasterTape& tape,
                         const State& state, const LatentBoard& truth,
                         int move_index, int rises_consumed, Mulberry32& random,
                         Scenario& out, std::string& reason) {
  out = Scenario{};
  out.board = state.board;
  out.moves_remaining = static_cast<std::uint8_t>(state.moves_remaining);
  out.horizon = static_cast<std::uint8_t>(config.horizon);

  out.latent.fill(0);
  for (int index = 0; index < kCellCount; ++index) {
    const std::uint8_t cell = state.board[index];
    if (cell != kSolid && cell != kCracked) continue;
    out.latent[index] =
        config.latent_known ? truth[index] : random.nextDisc();
  }

  // The disc already in hand is public state in every configuration.
  out.disc_tape.clear();
  out.disc_tape.push_back(state.next_disc);
  for (int step = 1; step < config.horizon; ++step) {
    if (config.tape_known) {
      const std::size_t at = static_cast<std::size_t>(move_index + step);
      if (at >= tape.discs.size()) {
        reason = "master tape too short for the window";
        return false;
      }
      out.disc_tape.push_back(tape.discs[at]);
    } else {
      out.disc_tape.push_back(random.nextDisc());
    }
  }

  const int rows = riseRowCount(config.horizon, state.moves_remaining);
  out.rise_latent.clear();
  for (int row = 0; row < rows; ++row) {
    if (config.tape_known) {
      const std::size_t at = static_cast<std::size_t>(rises_consumed + row);
      if (at >= tape.rises.size()) {
        reason = "master rise rows exhausted";
        return false;
      }
      out.rise_latent.push_back(tape.rises[at]);
    } else {
      RiseRow values{};
      for (int column = 0; column < kBoardSize; ++column) {
        values[column] = random.nextDisc();
      }
      out.rise_latent.push_back(values);
    }
  }
  assignScenarioId(out);
  return validateScenario(out, reason);
}

// One decision.  Returns the chosen column, or -1 if none could be scored.
// `truth` is passed only so that a `latent_known` arm can read it; when
// `config.latent_known` is false this function provably never touches it, and
// `flow-run --self-test` checks that by re-deciding the same public position
// against a different hidden board and requiring the same answer.
struct FairDecision {
  int column = -1;
  int samples_used = 0;
  int incomplete = 0;
  std::int64_t nodes = 0;
  double wall_seconds = 0.0;
};

inline FairDecision fairDecision(const FairPlannerConfig& config,
                                 const MasterTape& tape, const State& state,
                                 const LatentBoard& truth, int move_index,
                                 int rises_consumed, Mulberry32& random) {
  FairDecision decision;
  std::array<std::int64_t, kBoardSize> total{};
  std::array<int, kBoardSize> counted{};
  std::array<bool, kBoardSize> legal{};

  // Draw every sampled world first, serially, so the stream is deterministic.
  std::vector<Scenario> windows;
  windows.reserve(static_cast<std::size_t>(config.samples));
  for (int sample = 0; sample < config.samples; ++sample) {
    Scenario window;
    std::string reason;
    if (!sampleWindow(config, tape, state, truth, move_index, rises_consumed,
                      random, window, reason)) {
      break;
    }
    windows.push_back(std::move(window));
  }

  std::vector<RootResult> results(windows.size());
  const int threads = std::max(1, config.sample_threads);
  if (threads <= 1 || windows.size() < 2) {
    for (std::size_t index = 0; index < windows.size(); ++index) {
      results[index] =
          solveWindowRoot(windows[index], config.objective, config.limits);
    }
  } else {
    std::atomic<std::size_t> next{0};
    std::vector<std::thread> pool;
    const int workers =
        std::min(threads, static_cast<int>(windows.size()));
    for (int worker = 0; worker < workers; ++worker) {
      pool.emplace_back([&] {
        for (;;) {
          const std::size_t index = next.fetch_add(1);
          if (index >= windows.size()) return;
          results[index] =
              solveWindowRoot(windows[index], config.objective, config.limits);
        }
      });
    }
    for (std::thread& thread : pool) thread.join();
  }

  for (const RootResult& root : results) {
    decision.nodes += root.nodes;
    decision.wall_seconds += root.wall_seconds;
    if (!root.complete) {
      ++decision.incomplete;
      continue;
    }
    ++decision.samples_used;
    for (int column = 0; column < kBoardSize; ++column) {
      if (!root.legal[static_cast<std::size_t>(column)]) continue;
      legal[static_cast<std::size_t>(column)] = true;
      total[static_cast<std::size_t>(column)] +=
          root.value[static_cast<std::size_t>(column)];
      ++counted[static_cast<std::size_t>(column)];
    }
  }

  // Legality depends only on the visible board, so every sample offers the same
  // column set and the means are directly comparable.  Ties go to the lower
  // column index, deterministically.
  double best = 0.0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!legal[static_cast<std::size_t>(column)]) continue;
    if (counted[static_cast<std::size_t>(column)] == 0) continue;
    const double mean =
        static_cast<double>(total[static_cast<std::size_t>(column)]) /
        counted[static_cast<std::size_t>(column)];
    if (decision.column < 0 || mean > best) {
      best = mean;
      decision.column = column;
    }
  }
  return decision;
}

}  // namespace drop7::flowceiling
