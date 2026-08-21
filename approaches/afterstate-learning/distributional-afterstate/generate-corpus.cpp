// Corpus generator for the distributional afterstate ranker pilot.
//
// Harvests decision roots from complete fair-D1 games played on the leased
// public-development seed range, then labels every legal sibling of every
// root under aligned chance scenarios with a fixed public continuation
// (phase-greedy D1, kHorizon moves). Output is deterministic NDJSON sorted by
// (origin_seed, move_index, action, scenario) regardless of thread count.
//
// This tool reads game seeds to produce training data. It is not a policy and
// never runs at deployment. The model trained on this data sees only public
// afterstates.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../../src/core/native/engine.hpp"
#include "../../../src/core/native/public-behavior.hpp"
#include "common.hpp"

namespace afterstate = drop7::afterstate;
using drop7::Board;
using drop7::MoveResult;
using drop7::State;

namespace {

struct Options {
  std::uint32_t seed_start = 0x5da7'0000u;
  int max_games = afterstate::kMaxHarvestGames;
  int max_roots = afterstate::kMaxRoots;
  int scenarios = afterstate::kScenarios;
  int threads = 8;
  double wall_seconds = 4.0 * 3600.0;
  std::string out_dir;
  std::string run_id;
  std::string fold_force;  // if set, every root gets this fold
  afterstate::Continuation continuation = afterstate::Continuation::kD1;
};

Options parseArgs(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
      return argv[++i];
    };
    if (arg == "--seed-start") {
      options.seed_start = static_cast<std::uint32_t>(std::stoul(next(), nullptr, 0));
    } else if (arg == "--games") {
      options.max_games = std::stoi(next());
    } else if (arg == "--roots") {
      options.max_roots = std::stoi(next());
    } else if (arg == "--scenarios") {
      options.scenarios = std::stoi(next());
      if (options.scenarios < 2) {
        throw std::runtime_error("--scenarios must be >= 2");
      }
    } else if (arg == "--fold-force") {
      options.fold_force = next();
    } else if (arg == "--continuation") {
      const std::string value = next();
      if (value == "d1") {
        options.continuation = afterstate::Continuation::kD1;
      } else if (value == "d2") {
        options.continuation = afterstate::Continuation::kD2;
      } else {
        throw std::runtime_error("unknown --continuation: " + value);
      }
    } else if (arg == "--threads") {
      options.threads = std::stoi(next());
    } else if (arg == "--wall-seconds") {
      options.wall_seconds = std::stod(next());
    } else if (arg == "--out") {
      options.out_dir = next();
    } else if (arg == "--run-id") {
      options.run_id = next();
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (options.out_dir.empty()) throw std::runtime_error("--out is required");
  if (options.threads < 1) throw std::runtime_error("--threads must be >= 1");
  return options;
}

std::string boardString(const Board& board) {
  return drop7::serializeBoard(board);
}

// Plays one complete fair-D1 game and returns its decision roots in move
// order. Deterministic for a given seed.
std::vector<afterstate::RootRecord> harvestGame(std::uint32_t seed,
                                                int move_cap) {
  std::vector<afterstate::RootRecord> roots;
  State state = drop7::initialHeadlessState(seed);
  while (!state.game_over && state.moves_played < move_cap) {
    afterstate::RootRecord root;
    if (afterstate::makeRoot(state, seed, root)) roots.push_back(root);
    const int action = drop7::cfpi::choosePhaseGreedyAction(state, 1);
    if (action < 0) break;
    MoveResult move;
    if (!drop7::playHeadlessMove(state, seed, action, move)) break;
  }
  return roots;
}

std::string labelRow(const afterstate::RootRecord& root,
                     const afterstate::SiblingLabel& label) {
  std::ostringstream row;
  row << "{\"originSeed\":\"0x" << std::hex << root.origin_seed << std::dec
      << "\",\"moveIndex\":" << root.move_index
      << ",\"fold\":\"" << root.fold << "\""
      << ",\"rootBoard\":\"" << boardString(root.board) << "\""
      << ",\"rootNextDisc\":" << root.next_disc
      << ",\"rootMovesRemaining\":" << root.moves_remaining
      << ",\"mirrored\":" << (root.mirrored ? "true" : "false")
      << ",\"action\":" << label.action << ",\"scenario\":" << label.scenario
      << ",\"afterstateBoard\":\"" << boardString(label.afterstate) << "\""
      << ",\"afterstateNextDisc\":" << label.afterstate_next_disc
      << ",\"afterstateMovesRemaining\":" << label.afterstate_moves_remaining
      << ",\"terminal\":" << (label.terminal ? "true" : "false")
      << ",\"scoreGained\":" << label.score_gained
      << ",\"movesSurvived\":" << label.moves_survived
      << ",\"clears\":" << label.clears << ",\"reveals\":" << label.reveals
      << ",\"maxChain\":" << label.max_chain << "}";
  return row.str();
}

}  // namespace

int main(int argc, char** argv) {
  const Options options = parseArgs(argc, argv);
  const auto started = std::chrono::steady_clock::now();
  std::filesystem::create_directories(options.out_dir);

  constexpr int kMoveCap = 2'000;

  // Phase 1: harvest roots, games parallel in seed order chunks.
  std::vector<std::vector<afterstate::RootRecord>> per_game_roots(
      static_cast<std::size_t>(options.max_games));
  std::atomic<int> next_game{0};
  std::atomic<bool> stop{false};
  std::mutex log_mutex;

  auto harvest_worker = [&]() {
    for (;;) {
      const int game = next_game.fetch_add(1);
      if (game >= options.max_games || stop.load()) return;
      const auto elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        started)
              .count();
      if (elapsed > options.wall_seconds) {
        stop.store(true);
        return;
      }
      const std::uint32_t seed = options.seed_start + static_cast<std::uint32_t>(game);
      per_game_roots[static_cast<std::size_t>(game)] = harvestGame(seed, kMoveCap);
      {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cerr << "harvested game " << game << " seed 0x" << std::hex << seed
                  << std::dec << " roots "
                  << per_game_roots[static_cast<std::size_t>(game)].size()
                  << "\n";
      }
    }
  };

  {
    std::vector<std::future<void>> workers;
    for (int t = 0; t < options.threads; ++t) {
      workers.push_back(std::async(std::launch::async, harvest_worker));
    }
    for (auto& worker : workers) worker.get();
  }

  // Merge in seed order; enforce whole-origin folds and drop exact duplicate
  // public roots whose first sighting was in a different fold.
  std::vector<afterstate::RootRecord> roots;
  std::unordered_map<std::string, std::string> root_fold;
  std::int64_t dropped_cross_fold = 0;
  for (int game = 0; game < options.max_games; ++game) {
    for (afterstate::RootRecord root :
         per_game_roots[static_cast<std::size_t>(game)]) {
      if (!options.fold_force.empty()) root.fold = options.fold_force;
      const std::string key = boardString(root.board) + ":" +
                              std::to_string(root.next_disc) + ":" +
                              std::to_string(root.moves_remaining);
      const auto [it, inserted] = root_fold.emplace(key, root.fold);
      if (!inserted && it->second != root.fold) {
        ++dropped_cross_fold;
        continue;
      }
      roots.push_back(std::move(root));
      if (static_cast<int>(roots.size()) >= options.max_roots) break;
    }
    if (static_cast<int>(roots.size()) >= options.max_roots) break;
  }

  // Phase 2: label roots in parallel; output stays in root order.
  std::vector<std::vector<afterstate::SiblingLabel>> per_root_labels(roots.size());
  std::atomic<std::size_t> next_root{0};
  auto label_worker = [&]() {
    for (;;) {
      const std::size_t index = next_root.fetch_add(1);
      if (index >= roots.size() || stop.load()) return;
      const auto elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        started)
              .count();
      if (elapsed > options.wall_seconds) {
        stop.store(true);
        return;
      }
      per_root_labels[index] =
          afterstate::labelRoot(roots[index], options.scenarios,
                                options.continuation);
      if ((index + 1) % 500 == 0) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cerr << "labeled " << (index + 1) << "/" << roots.size()
                  << " roots\n";
      }
    }
  };
  {
    std::vector<std::future<void>> workers;
    for (int t = 0; t < options.threads; ++t) {
      workers.push_back(std::async(std::launch::async, label_worker));
    }
    for (auto& worker : workers) worker.get();
  }

  const bool complete = !stop.load() &&
                        next_root.load() >= roots.size() &&
                        next_game.load() >= options.max_games;

  // Phase 3: write deterministic output.
  const std::string corpus_path = options.out_dir + "/corpus.ndjson";
  std::ofstream corpus(corpus_path);
  std::ofstream root_index(options.out_dir + "/roots.tsv");
  root_index << "root_uid\tfold\torigin_seed\tmove_index\tboard\tnext_disc\t"
                "moves_remaining\tlegal_actions\n";
  std::int64_t rows = 0;
  std::map<std::string, std::int64_t> fold_roots;
  for (std::size_t index = 0; index < roots.size(); ++index) {
    if (per_root_labels[index].empty()) continue;  // stopped mid-root
    const afterstate::RootRecord& root = roots[index];
    ++fold_roots[root.fold];
    const std::string board = boardString(root.board);
    root_index << board << ":" << root.next_disc << ":" << root.moves_remaining
               << "\t" << root.fold << "\t0x" << std::hex << root.origin_seed
               << std::dec << "\t" << root.move_index << "\t" << board << "\t"
               << root.next_disc << "\t" << root.moves_remaining << "\t";
    for (std::size_t a = 0; a < root.legal_actions.size(); ++a) {
      root_index << (a == 0 ? "" : ",") << root.legal_actions[a];
    }
    root_index << "\n";
    for (const auto& label : per_root_labels[index]) {
      corpus << labelRow(root, label) << "\n";
      ++rows;
    }
  }
  corpus.close();
  root_index.close();

  std::ofstream manifest(options.out_dir + "/manifest.json");
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  manifest << "{\n"
           << "  \"format\": \"drop7-afterstate-corpus-v1\",\n"
           << "  \"runId\": \"" << options.run_id << "\",\n"
           << "  \"seedStartHex\": \"0x" << std::hex << options.seed_start
           << std::dec << "\",\n"
           << "  \"gamesScheduled\": " << options.max_games << ",\n"
           << "  \"rootsLabeled\": " << per_root_labels.size() << ",\n"
           << "  \"rows\": " << rows << ",\n"
           << "  \"scenarios\": " << options.scenarios << ",\n"
           << "  \"horizon\": " << afterstate::kHorizon << ",\n"
           << "  \"continuationPolicy\": \""
           << (options.continuation == afterstate::Continuation::kD2
                   ? "fair-d2-s5"
                   : "phase-greedy-d1")
           << "\",\n"
           << "  \"harvestPolicy\": \"phase-greedy-d1\",\n"
           << "  \"droppedCrossFoldDuplicateRoots\": " << dropped_cross_fold
           << ",\n"
           << "  \"complete\": " << (complete ? "true" : "false") << ",\n"
           << "  \"wallSeconds\": " << elapsed << ",\n"
           << "  \"foldRoots\": {";
  bool first = true;
  for (const auto& [fold, count] : fold_roots) {
    manifest << (first ? "" : ",") << "\n    \"" << fold << "\": " << count;
    first = false;
  }
  manifest << "\n  }\n}\n";

  std::cout << "corpus rows " << rows << " roots " << roots.size()
            << " complete " << (complete ? "true" : "false") << " wall "
            << elapsed << "s\n";
  return complete ? 0 : 2;
}
