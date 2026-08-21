// Successor-closed search-value labeler for the scale-out stage-1 experiment
// (EX-20260821-afterstate-d4q-stage1-40136e9e).
//
// For every corpus root and every legal sibling, this reproduces EXACTLY what
// the pinned fair-D4 reference does at its root: the same five-stratum chance
// quadrature (same scenario seeds, same sampled next discs), resolving each
// placement into its canonical public afterstate. Each afterstate is then
// labeled with the value the reference's own search assigns it:
// bestFutureValue(afterstate, depth 3) - i.e. the value one ply below the D4
// root. The result is a successor-closed state-value corpus: every legal
// sibling's successor states with the search's own values attached.
//
// Reads roots.tsv written by generate-corpus; writes d4q-labels.tsv:
//   root_uid  fold  action  stratum  afterstate_board  afterstate_next_disc
//   afterstate_moves_remaining  value
//
// The pinned reference is included with its entry point renamed by build.sh,
// so the labeling path is byte-identical to the baseline's search.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "fair-only-depth4-noentry.cpp"

namespace d4 = drop7::fair_only_depth4;
using drop7::Board;
using drop7::MoveResult;
using drop7::State;

namespace {

constexpr int kStrata = d4::kChanceSamples;  // 5, the reference's root quadrature

struct RootRow {
  std::string uid;
  std::string fold;
  Board board{};
  int next_disc = 1;
  int moves_remaining = drop7::kMovesPerLevel;
  std::vector<int> legal_actions;
};

Board parseBoard(const std::string& text) {
  if (text.size() != drop7::kCellCount) {
    throw std::runtime_error("bad board string length");
  }
  Board board{};
  for (int i = 0; i < drop7::kCellCount; ++i) {
    board[static_cast<std::size_t>(i)] =
        static_cast<std::uint8_t>(text[static_cast<std::size_t>(i)] - '0');
  }
  return board;
}

std::vector<RootRow> readRoots(const std::string& path,
                               const std::vector<std::string>& folds) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot open " + path);
  std::vector<RootRow> rows;
  std::unordered_map<std::string, bool> seen;
  std::string line;
  std::getline(in, line);
  while (std::getline(in, line)) {
    std::istringstream fields(line);
    std::string uid, fold, seed, move_index, board, next, moves, actions;
    if (!std::getline(fields, uid, '\t')) continue;
    std::getline(fields, fold, '\t');
    std::getline(fields, seed, '\t');
    std::getline(fields, move_index, '\t');
    std::getline(fields, board, '\t');
    std::getline(fields, next, '\t');
    std::getline(fields, moves, '\t');
    std::getline(fields, actions, '\t');
    bool wanted = false;
    for (const auto& f : folds) wanted = wanted || fold == f;
    if (!wanted || seen.count(uid)) continue;
    seen[uid] = true;
    RootRow row;
    row.uid = uid;
    row.fold = fold;
    row.board = parseBoard(board);
    row.next_disc = std::stoi(next);
    row.moves_remaining = std::stoi(moves);
    std::istringstream list(actions);
    std::string item;
    while (std::getline(list, item, ','))
      row.legal_actions.push_back(std::stoi(item));
    rows.push_back(std::move(row));
  }
  return rows;
}

struct AfterstateRow {
  std::string uid;
  int action = -1;
  int stratum = -1;
  Board afterstate{};
  int next_disc = 1;
  int moves_remaining = drop7::kMovesPerLevel;
  double value = 0.0;
  bool terminal = false;
  double score_delta = 0.0;
};

// Replicates the reference's evaluateAction chance loop exactly, but instead of
// recursing, records each resolved canonical afterstate and its depth-3 value.
void labelRootD4Q(const RootRow& row, std::vector<AfterstateRow>& out) {
  State state;
  state.board = row.board;
  state.next_disc = static_cast<std::uint8_t>(row.next_disc);
  state.moves_remaining = row.moves_remaining;

  for (const int action : row.legal_actions) {
    const std::uint32_t state_seed =
        drop7::cfpi::detail::scenarioSeedForState(state, d4::frozen::kPolicySeed,
                                                  4);
    for (int sample = 0; sample < kStrata; ++sample) {
      drop7::cfpi::detail::StratifiedRandom random{state_seed, sample, kStrata,
                                                   0};
      MoveResult move;
      if (!drop7::cfpi::detail::playMoveSampled(state, action, random, move)) {
        continue;  // illegal under the reference's own rules: no label
      }
      AfterstateRow record;
      record.uid = row.uid;
      record.action = action;
      record.stratum = sample;
      record.score_delta = static_cast<double>(move.score_delta);
      if (move.state.game_over) {
        record.terminal = true;
        record.value = d4::frozen::kTerminalUtility;
        record.afterstate = move.state.board;
        record.next_disc = move.state.next_disc;
        record.moves_remaining = move.state.moves_remaining;
      } else {
        move.state.score = 0;
        move.state.next_disc = drop7::cfpi::detail::sampledNextDisc(
            state_seed, sample, kStrata);
        bool ignored = false;
        const State canonical =
            drop7::cfpi::detail::canonicalState(move.state, ignored);
        d4::SearchContext context;
        record.value = d4::bestFutureValue(canonical, 3, context);
        record.afterstate = canonical.board;
        record.next_disc = canonical.next_disc;
        record.moves_remaining = canonical.moves_remaining;
      }
      out.push_back(record);
    }
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string roots_path;
  std::string out_path;
  std::string folds_arg = "train,calibration";
  int threads = 8;
  double wall_seconds = 4.0 * 3600.0;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) throw std::runtime_error("missing value for " + arg);
      return argv[++i];
    };
    if (arg == "--roots") {
      roots_path = next();
    } else if (arg == "--out") {
      out_path = next();
    } else if (arg == "--folds") {
      folds_arg = next();
    } else if (arg == "--threads") {
      threads = std::stoi(next());
    } else if (arg == "--wall-seconds") {
      wall_seconds = std::stod(next());
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (roots_path.empty() || out_path.empty()) {
    std::cerr << "usage: label-d4q --roots roots.tsv --out d4q-labels.tsv "
                 "[--folds train,calibration]\n";
    return 2;
  }
  const auto started = std::chrono::steady_clock::now();
  const std::filesystem::path parent =
      std::filesystem::path(out_path).parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);

  std::vector<std::string> folds;
  {
    std::istringstream stream(folds_arg);
    std::string item;
    while (std::getline(stream, item, ',')) folds.push_back(item);
  }
  const std::vector<RootRow> rows = readRoots(roots_path, folds);
  std::cerr << "d4q roots " << rows.size() << " folds " << folds_arg << "\n";

  std::vector<std::vector<AfterstateRow>> per_root(rows.size());
  std::atomic<std::size_t> next_index{0};
  std::atomic<bool> stop{false};
  std::mutex log_mutex;
  auto worker = [&]() {
    for (;;) {
      const std::size_t index = next_index.fetch_add(1);
      if (index >= rows.size() || stop.load()) return;
      const double elapsed =
          std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                        started)
              .count();
      if (elapsed > wall_seconds) {
        stop.store(true);
        return;
      }
      labelRootD4Q(rows[index], per_root[index]);
      if ((index + 1) % 200 == 0) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cerr << "d4q-labeled " << (index + 1) << "/" << rows.size()
                  << "\n";
      }
    }
  };
  {
    std::vector<std::future<void>> workers;
    for (int t = 0; t < threads; ++t) {
      workers.push_back(std::async(std::launch::async, worker));
    }
    for (auto& w : workers) w.get();
  }

  std::ofstream out(out_path);
  out << "root_uid\tfold\taction\tstratum\tafterstate_board\tafterstate_next_"
         "disc\tafterstate_moves_remaining\tterminal\tscore_delta\tvalue\n";
  std::int64_t written = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    for (const auto& record : per_root[i]) {
      out << record.uid << "\t" << rows[i].fold << "\t" << record.action << "\t"
          << record.stratum << "\t" << drop7::serializeBoard(record.afterstate)
          << "\t" << record.next_disc << "\t" << record.moves_remaining << "\t"
          << (record.terminal ? 1 : 0) << "\t" << record.score_delta << "\t"
          << record.value << "\n";
      ++written;
    }
  }
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  std::cout << "d4q rows " << written << " roots " << rows.size()
            << " stopped_early " << (stop.load() ? "true" : "false")
            << " wall " << elapsed << "s\n";
  return stop.load() ? 2 : 0;
}
