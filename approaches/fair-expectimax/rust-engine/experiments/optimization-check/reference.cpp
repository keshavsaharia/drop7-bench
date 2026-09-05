// Reuse the independent C++ reference evaluator on constructed public roots.
// No game is generated and no seed is accepted by this entry point.
// Compile with -ffp-contract=off: Rust preserves separate multiply/add rounding,
// while Apple clang otherwise contracts a leaf expression on arm64.
#define main historical_search_trace_main
#include "approaches/fair-expectimax/rust-engine/cpp/search_trace.cpp"
#undef main
#include <fstream>
#include <sstream>

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  std::ifstream input{argv[1]};
  if (!input) return 2;
  const std::string mode{argv[2]};
  const int depth = std::stoi(argv[3]);
  if ((mode != "leaf" && mode != "search") || depth < 1 || depth > 5) return 2;
  std::string line;
  unsigned count = 0;
  while (std::getline(input, line)) {
    if (line.rfind("s ", 0) != 0) continue;
    std::istringstream row{line};
    std::string prefix, board;
    State state{};
    int next, over;
    if (!(row >> prefix >> board >> next >> state.moves_remaining >> state.level >> over)
        || board.size() != 49 || next < 1 || next > 7 || over != 0) return 2;
    for (std::size_t i = 0; i < board.size(); ++i) {
      if (board[i] < '0' || board[i] > '9') return 2;
      state.board[i] = static_cast<std::uint8_t>(board[i] - '0');
    }
    state.next_disc = static_cast<std::uint8_t>(next);
    printState(state);
    if (mode == "leaf") {
      LeafScratch scratch;
      const double value = fastFairLeaf(state, scratch);
      std::uint64_t bits;
      std::memcpy(&bits, &value, sizeof bits);
      std::cout << "v " << std::hex << bits << std::dec << '\n';
    } else {
      bool mirrored = false;
      const State canonical = canonicalStateFast(state, mirrored);
      ValueSearch search;
      search.strata_ = 7;
      int chosen = -1;
      double best = -std::numeric_limits<double>::infinity();
      for (int column : cfpi::detail::kColumnOrder) {
        if (!isLegal(canonical.board, column)) continue;
        const double value = search.evaluateAction(canonical, column, depth);
        std::uint64_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        std::cout << "c " << column << ' ' << std::hex << bits << std::dec << '\n';
        if (value > best) { best = value; chosen = column; }
      }
      if (mirrored && chosen >= 0) chosen = 6 - chosen;
      std::cout << "a " << chosen << '\n';
    }
    ++count;
  }
  return count ? 0 : 2;
}
