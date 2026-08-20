// Comparator labeler for the afterstate pilot.
//
// Recomputes exact fair-D4 (and fair-D1) per-action values for unique corpus
// roots. The D4 values come from the pinned reference implementation itself
// (approaches/fair-expectimax/reference/fair-only-depth4.cpp, included with
// its entry point renamed by build.sh), so the comparator is byte-identical
// to the baseline manifest rather than a reimplementation.
//
// Reads roots.tsv written by generate-corpus; writes comparator-labels.tsv.
// Canonical columns are used throughout, matching the corpus action space.

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
#include <atomic>

#include "fair-only-depth4-noentry.cpp"

namespace d4 = drop7::fair_only_depth4;
using drop7::Board;
using drop7::State;

namespace {

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
  std::getline(in, line);  // header
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
    while (std::getline(list, item, ',')) row.legal_actions.push_back(std::stoi(item));
    rows.push_back(std::move(row));
  }
  return rows;
}

struct LabelRow {
  std::string uid;
  std::string fold;
  std::vector<double> d4;  // per legal action, aligned with legal_actions
  std::vector<double> d1;
  std::vector<int> actions;
  std::uint64_t work = 0;
  bool d4_complete = false;
};

LabelRow labelRoot(const RootRow& row) {
  State state;
  state.board = row.board;
  state.next_disc = static_cast<std::uint8_t>(row.next_disc);
  state.moves_remaining = row.moves_remaining;

  LabelRow out;
  out.uid = row.uid;
  out.fold = row.fold;
  out.actions = row.legal_actions;

  // Exact fair D4 per-action values from the pinned reference.
  d4::SearchContext d4_context;
  const d4::RootEvaluation d4_eval = d4::rootDecision(state, 4, d4_context);
  out.work = d4_context.work;
  out.d4_complete = true;
  for (const int action : row.legal_actions) {
    out.d4.push_back(d4_eval.values[static_cast<std::size_t>(action)]);
  }

  // Fair D1 per-action values with the same chance quadrature count.
  drop7::cfpi::BehaviorOptions d1_options;
  d1_options.max_depth = 1;
  d1_options.chance_samples = 5;
  d1_options.max_work = 20'000;
  d1_options.max_cache_entries = 512;
  drop7::cfpi::detail::SearchContext d1_context(d1_options);
  for (const int action : row.legal_actions) {
    out.d1.push_back(drop7::cfpi::detail::evaluateAction(state, action, 1,
                                                         d1_context));
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::string roots_path;
  std::string out_path;
  int threads = 8;
  double wall_seconds = 3.0 * 3600.0;
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
    } else if (arg == "--threads") {
      threads = std::stoi(next());
    } else if (arg == "--wall-seconds") {
      wall_seconds = std::stod(next());
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }
  if (roots_path.empty() || out_path.empty()) {
    std::cerr << "usage: label-d4 --roots roots.tsv --out labels.tsv\n";
    return 2;
  }
  const auto started = std::chrono::steady_clock::now();
  const std::filesystem::path out_parent =
      std::filesystem::path(out_path).parent_path();
  if (!out_parent.empty()) std::filesystem::create_directories(out_parent);

  const std::vector<RootRow> rows =
      readRoots(roots_path, {"calibration", "heldout"});
  std::cerr << "unique comparator roots " << rows.size() << "\n";

  std::vector<LabelRow> labels(rows.size());
  std::vector<bool> done(rows.size(), false);
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
      labels[index] = labelRoot(rows[index]);
      done[index] = true;
      if ((index + 1) % 100 == 0) {
        std::lock_guard<std::mutex> lock(log_mutex);
        std::cerr << "d4-labeled " << (index + 1) << "/" << rows.size()
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
  out << "root_uid\tfold\taction\td4_value\td1_value\n";
  std::int64_t written = 0;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (!done[i]) continue;
    for (std::size_t a = 0; a < labels[i].actions.size(); ++a) {
      out << labels[i].uid << "\t" << labels[i].fold << "\t"
          << labels[i].actions[a] << "\t" << labels[i].d4[a] << "\t"
          << labels[i].d1[a] << "\n";
      ++written;
    }
  }
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  std::cout << "comparator rows " << written << " roots " << rows.size()
            << " stopped_early " << (stop.load() ? "true" : "false")
            << " wall " << elapsed << "s\n";
  return stop.load() ? 2 : 0;
}
