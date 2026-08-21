// play - the distilled student as the LEAF of the parameterised fair search, on
// the base engine, over a named cohort.
//
//   play --parity [--parity-games 3] [--parity-moves 40]
//   play --leaf-stats --model model.d7pdst --corpus runs/RID/corpus.bin
//   play --model model.d7pdst --w 0.3 --scale 20000
//        --seed-start 0xa51d1000 --games 64 --depth 4 --chance-samples 7
//        --max-work 16000000 --threads 8 --output out.json
//
// THE BLEND
// ---------
//     leafValue = (1 - w) * frozen::fairLeaf(s) + w * scale * student(s)
//
// `w = 0` short-circuits to the frozen leaf *before* the model is touched, so
// the comparator arm is the reference bit for bit and costs exactly what the
// reference costs.  That is the correctness anchor, and `--parity` checks it
// against the unmodified frozen entry point.
//
// THE WORK BOUND
// --------------
// `finding-05` measured that at depth 4 exact seven-strata chance handling is
// worth +101,171 points (95% lower bound +47,457) while at depth 3 it is worth
// nothing.  Worst-case depth-4 work is 3,134,950 at five strata and 11,892,398
// at seven, so the frozen 3,200,000 bound silently degrades a seven-stratum
// depth-4 search to a completed depth 3.  Every seven-stratum arm must pass
// `--max-work 16000000`, and the completed depth is recorded per decision and
// summarised so the degradation cannot go unnoticed.

#include "fair-search.hpp"

#include "corpus.hpp"
#include "student.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace drop7;
using namespace drop7::distill;

// The one cohort outside the seed lease this binary may open: the shared
// evaluation cohort every other arm in this session reported on.  It is already
// development data (finding-05's confirmation cohort) and is never tuned on.
constexpr std::uint32_t kEvalStart = 0xa51d'1000u;
constexpr std::uint32_t kEvalEnd = 0xa51d'103fu;
constexpr std::uint32_t kLeaseStart = 0xa526'0000u;
constexpr std::uint32_t kLeaseEnd = 0xa526'ffffu;

struct StudentLeaf : LeafModel {
  const Student* model = nullptr;
  double value(const State& state) const override {
    float scratch[1024];
    float residual = 0.0f;
    float lifetime = 0.0f;
    model->evaluate(state.board.data(), state.next_disc, state.moves_remaining,
                    scratch, residual, lifetime);
    return residual;
  }
};

struct Options {
  std::string model;
  std::string corpus;
  std::string output;
  std::uint32_t seed_start = kEvalStart;
  int games = 64;
  int depth = 4;
  int chance_samples = 7;
  std::uint64_t max_work = 16'000'000;
  int max_moves = 2000;
  int threads = 8;
  double w = 0.0;
  double scale = 1.0;
  bool parity = false;
  bool leaf_stats = false;
  int parity_games = 3;
  int parity_moves = 40;
  std::string label = "student-leaf";
};

struct GameRecord {
  std::uint32_t seed = 0;
  std::int64_t score = 0;
  int moves = 0;
  bool censored = false;
  std::int64_t cleared = 0;
  std::int64_t revealed = 0;
  std::int64_t occupancy = 0;
  int max_chain_depth = 0;
  std::int64_t chain_depth_sum = 0;
  std::int64_t waves = 0;
  std::uint64_t work = 0;
  std::uint64_t leaf_calls = 0;
  int shallow_decisions = 0;
  int illegal = 0;
  double wall_seconds = 0.0;
  std::vector<int> cycle_occupancy;
};

int occupied(const Board& board) {
  int count = 0;
  for (std::uint8_t cell : board) {
    if (cell != kEmpty) ++count;
  }
  return count;
}

double occupancySlope(const std::vector<int>& values, int skip) {
  const int n = static_cast<int>(values.size()) - skip;
  if (n < 3) return 0.0;
  double sx = 0, sy = 0, sxy = 0, sxx = 0;
  for (int index = 0; index < n; ++index) {
    const double x = index;
    const double y = values[static_cast<std::size_t>(index + skip)];
    sx += x; sy += y; sxy += x * y; sxx += x * x;
  }
  const double denominator = n * sxx - sx * sx;
  return denominator == 0.0 ? 0.0 : (n * sxy - sx * sy) / denominator;
}

GameRecord playOne(const Options& options, const Student* model,
                   std::uint32_t seed) {
  GameRecord record;
  record.seed = seed;
  StudentLeaf leaf;
  leaf.model = model;
  SearchParameters parameters;
  parameters.depth = options.depth;
  parameters.chanceSamples = options.chance_samples;
  parameters.maximumWork = options.max_work;
  parameters.leafWeight = options.w;
  parameters.leafScale = options.scale;
  parameters.leaf = options.w == 0.0 ? nullptr : &leaf;
  ParameterizedSearch search{parameters};

  const auto started = std::chrono::steady_clock::now();
  State state = initialHeadlessState(seed);
  int moves_since_rise = 0;
  while (!state.game_over && state.moves_played < options.max_moves) {
    const RootValues values = search.evaluateRoot(state);
    if (values.action < 0 || !isLegal(state.board, values.action)) {
      ++record.illegal;
      break;
    }
    if (values.completedDepth < options.depth) ++record.shallow_decisions;
    record.work += values.work;
    record.leaf_calls += values.leafEvaluations;
    MoveResult move;
    if (!playHeadlessMove(state, seed, values.action, move)) break;
    for (const Wave& wave : move.waves) {
      record.cleared += wave.cleared;
      record.revealed += wave.revealed;
      record.chain_depth_sum += wave.depth;
      ++record.waves;
      record.max_chain_depth = std::max(record.max_chain_depth, wave.depth);
    }
    ++record.moves;
    record.occupancy += occupied(move.state.board);
    ++moves_since_rise;
    if (move.level_advanced) {
      record.cycle_occupancy.push_back(occupied(move.state.board));
      moves_since_rise = 0;
    }
    state = move.state;
  }
  record.score = state.score;
  record.censored = !state.game_over;
  record.wall_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();
  (void)moves_since_rise;
  return record;
}

int runParity(const Options& options) {
  namespace refns = drop7::fair_only_depth4;
  SearchParameters parameters;   // defaults: depth 4, 5 strata, w = 0
  ParameterizedSearch mine{parameters};
  std::uint64_t mismatches = 0, compared = 0;
  for (int game = 0; game < options.parity_games; ++game) {
    const std::uint32_t seed = kEvalStart + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < options.parity_moves) {
      const refns::SearchDecision reference = refns::chooseDepth4Action(state);
      std::uint64_t work = 0;
      const int candidate = mine.chooseAction(state, work);
      ++compared;
      if (candidate != reference.action) ++mismatches;
      MoveResult move;
      if (!playHeadlessMove(state, seed, reference.action, move)) break;
    }
  }
  std::printf("parity: %llu moves compared, %llu mismatches\n%s\n",
              static_cast<unsigned long long>(compared),
              static_cast<unsigned long long>(mismatches),
              mismatches == 0 ? "PARITY OK" : "PARITY FAILED");
  return mismatches == 0 ? 0 : 1;
}

// Measures the student's own spread against the frozen leaf's, so `--scale` can
// be read as a mixing weight between comparable spreads rather than as an
// arbitrary constant, and measures the per-state inference cost that decides
// whether the model can play at all.
int runLeafStats(const Options& options) {
  Student model(options.model);
  std::ifstream file(options.corpus, std::ios::binary | std::ios::ate);
  if (!file) {
    std::cerr << "cannot open " << options.corpus << "\n";
    return 1;
  }
  const std::streamsize bytes = file.tellg();
  const std::size_t rows =
      static_cast<std::size_t>(bytes) / sizeof(drop7::distill::RootRecord);
  std::vector<drop7::distill::RootRecord> records(rows);
  file.seekg(0);
  file.read(reinterpret_cast<char*>(records.data()), bytes);

  std::vector<double> frozen_values, student_values;
  std::vector<float> scratch(4096);
  const std::size_t limit = std::min<std::size_t>(rows, 20000);
  for (std::size_t row = 0; row < limit; ++row) {
    const auto& record = records[row];
    for (int column = 0; column < kBoardSize; ++column) {
      if (((record.legal_mask >> column) & 1u) == 0) continue;
      if (record.after_survived[column] == 0) continue;
      State state;
      for (int index = 0; index < drop7::distill::kCells; ++index) {
        state.board[static_cast<std::size_t>(index)] =
            record.after_board[column][index];
      }
      state.next_disc = record.after_next_disc[column];
      state.score = 0;
      state.level = 1;
      state.moves_remaining = record.after_moves_remaining[column];
      state.moves_played = 0;
      state.game_over = false;
      frozen_values.push_back(drop7::distill::frozen::fairLeaf(state));
      float residual = 0.0f, lifetime = 0.0f;
      model.evaluate(state.board.data(), state.next_disc, state.moves_remaining,
                     scratch.data(), residual, lifetime);
      student_values.push_back(residual);
      break;  // one afterstate per root keeps the sample independent-ish
    }
  }
  const auto stats = [](const std::vector<double>& values) {
    double mean = 0.0;
    for (double value : values) mean += value;
    mean /= std::max<std::size_t>(values.size(), 1);
    double variance = 0.0;
    for (double value : values) variance += (value - mean) * (value - mean);
    variance /= std::max<std::size_t>(values.size(), 1);
    return std::pair<double, double>{mean, std::sqrt(variance)};
  };
  const auto frozen_stat = stats(frozen_values);
  const auto student_stat = stats(student_values);
  double covariance = 0.0;
  for (std::size_t index = 0; index < frozen_values.size(); ++index) {
    covariance += (frozen_values[index] - frozen_stat.first) *
                  (student_values[index] - student_stat.first);
  }
  covariance /= std::max<std::size_t>(frozen_values.size(), 1);

  // Inference cost, one thread, one state at a time - the way the search calls
  // it.  Batched throughput is irrelevant at an expectimax leaf.
  State probe;
  for (int index = 0; index < drop7::distill::kCells; ++index) {
    probe.board[static_cast<std::size_t>(index)] = records[0].board[index];
  }
  probe.next_disc = records[0].next_disc;
  probe.moves_remaining = records[0].moves_remaining;
  const int repeats = 200000;
  const auto started = std::chrono::steady_clock::now();
  double sink = 0.0;
  for (int index = 0; index < repeats; ++index) {
    float residual = 0.0f, lifetime = 0.0f;
    probe.next_disc = static_cast<std::uint8_t>(1 + (index % 7));
    model.evaluate(probe.board.data(), probe.next_disc, probe.moves_remaining,
                   scratch.data(), residual, lifetime);
    sink += residual;
  }
  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::printf(
      "states sampled %zu\n"
      "frozen fairLeaf : mean %.1f  sd %.1f\n"
      "student residual: mean %.4f  sd %.4f\n"
      "pearson         : %.4f\n"
      "equal-influence scale (sd ratio): %.1f\n"
      "inference       : %.3f us per state, one thread (sink %.3f)\n"
      "parameters      : %zu, digest 0x%016llx\n",
      frozen_values.size(), frozen_stat.first, frozen_stat.second,
      student_stat.first, student_stat.second,
      covariance / std::max(1e-12, frozen_stat.second * student_stat.second),
      frozen_stat.second / std::max(1e-9, student_stat.second),
      1e6 * elapsed / repeats, sink, model.parameterCount(),
      static_cast<unsigned long long>(model.digest()));
  return 0;
}

int runCohort(const Options& options) {
  const std::uint32_t last =
      options.seed_start + static_cast<std::uint32_t>(options.games) - 1u;
  const bool in_eval =
      options.seed_start >= kEvalStart && last <= kEvalEnd;
  const bool in_lease =
      options.seed_start >= kLeaseStart && last <= kLeaseEnd;
  if (!in_eval && !in_lease) {
    std::cerr << "seeds outside the lease and outside the shared eval cohort\n";
    return 2;
  }
  std::unique_ptr<Student> model;
  if (options.w != 0.0) {
    if (options.model.empty()) {
      std::cerr << "--w != 0 needs --model\n";
      return 2;
    }
    model = std::make_unique<Student>(options.model);
  }

  std::vector<GameRecord> games(static_cast<std::size_t>(options.games));
  std::atomic<int> next{0};
  std::mutex log;
  const auto started = std::chrono::steady_clock::now();
  std::vector<std::thread> pool;
  const int threads = std::max(1, std::min(options.threads, options.games));
  for (int worker = 0; worker < threads; ++worker) {
    pool.emplace_back([&] {
      for (;;) {
        const int index = next.fetch_add(1);
        if (index >= options.games) return;
        const std::uint32_t seed =
            options.seed_start + static_cast<std::uint32_t>(index);
        GameRecord record = playOne(options, model.get(), seed);
        {
          std::lock_guard<std::mutex> guard(log);
          std::printf("game 0x%08x moves %5d score %10lld  %.1f s\n", seed,
                      record.moves, static_cast<long long>(record.score),
                      record.wall_seconds);
          std::fflush(stdout);
        }
        games[static_cast<std::size_t>(index)] = std::move(record);
      }
    });
  }
  for (std::thread& thread : pool) thread.join();
  const double wall =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started)
          .count();

  std::int64_t moves = 0, score = 0, cleared = 0, revealed = 0, occupancy = 0;
  std::int64_t work = 0, leaf_calls = 0, waves = 0, depth_sum = 0;
  int censored = 0, shallow = 0, illegal = 0, deepest = 0;
  double slope_sum = 0.0;
  int slope_games = 0;
  for (const GameRecord& record : games) {
    moves += record.moves;
    score += record.score;
    cleared += record.cleared;
    revealed += record.revealed;
    occupancy += record.occupancy;
    work += record.work;
    leaf_calls += record.leaf_calls;
    waves += record.waves;
    depth_sum += record.chain_depth_sum;
    deepest = std::max(deepest, record.max_chain_depth);
    censored += record.censored ? 1 : 0;
    shallow += record.shallow_decisions;
    illegal += record.illegal;
    if (record.cycle_occupancy.size() >= 4) {
      slope_sum += occupancySlope(record.cycle_occupancy, 1);
      ++slope_games;
    }
  }
  const double n = static_cast<double>(options.games);
  const double move_total = static_cast<double>(std::max<std::int64_t>(moves, 1));
  std::printf(
      "\n=== %s w=%.3f scale=%.1f depth %d strata %d work %llu, %d games ===\n"
      "score mean %.1f, moves mean %.2f, censored %d\n"
      "clears/move %.4f, reveals/move %.4f, mean occupancy %.2f, slope %.3f\n"
      "decisions below the requested depth: %d; illegal: %d\n"
      "leaf model calls %lld, logical work %lld, %.1f s wall on %d threads\n",
      options.label.c_str(), options.w, options.scale, options.depth,
      options.chance_samples, static_cast<unsigned long long>(options.max_work),
      options.games, score / n, moves / n, censored, cleared / move_total,
      revealed / move_total, occupancy / move_total,
      slope_games ? slope_sum / slope_games : 0.0, shallow, illegal,
      static_cast<long long>(leaf_calls), static_cast<long long>(work), wall,
      threads);
  (void)waves;
  (void)depth_sum;
  (void)deepest;

  if (!options.output.empty()) {
    std::ofstream file(options.output);
    if (!file) {
      std::cerr << "cannot open " << options.output << "\n";
      return 1;
    }
    file << std::setprecision(12);
    file << "{\"schema\":\"drop7-planner-distill-cohort-v1\",\"label\":\""
         << options.label << "\",\"config\":{\"w\":" << options.w
         << ",\"scale\":" << options.scale << ",\"depth\":" << options.depth
         << ",\"chanceSamples\":" << options.chance_samples
         << ",\"maximumWork\":" << options.max_work
         << ",\"model\":\"" << options.model << "\"},\"games\":[";
    for (std::size_t index = 0; index < games.size(); ++index) {
      const GameRecord& record = games[index];
      if (index) file << ',';
      file << "{\"seed\":\"0x" << std::hex << record.seed << std::dec
           << "\",\"score\":" << record.score
           << ",\"moves\":" << record.moves
           << ",\"censored\":" << (record.censored ? "true" : "false")
           << ",\"numberedClears\":" << record.cleared
           << ",\"coveredReveals\":" << record.revealed
           << ",\"maxChainDepth\":" << record.max_chain_depth
           << ",\"logicalWork\":" << record.work
           << ",\"modelInferences\":" << record.leaf_calls
           << ",\"shallowDecisions\":" << record.shallow_decisions
           << ",\"illegalDecisions\":" << record.illegal
           << ",\"wallSeconds\":" << record.wall_seconds << "}";
    }
    file << "],\"wallSeconds\":" << wall << "}\n";
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  for (int index = 1; index < argc; ++index) {
    const std::string flag = argv[index];
    if (flag == "--parity") {
      options.parity = true;
    } else if (flag == "--leaf-stats") {
      options.leaf_stats = true;
    } else if (flag == "--model" && index + 1 < argc) {
      options.model = argv[++index];
    } else if (flag == "--corpus" && index + 1 < argc) {
      options.corpus = argv[++index];
    } else if (flag == "--output" && index + 1 < argc) {
      options.output = argv[++index];
    } else if (flag == "--label" && index + 1 < argc) {
      options.label = argv[++index];
    } else if (flag == "--seed-start" && index + 1 < argc) {
      options.seed_start = static_cast<std::uint32_t>(
          std::strtoul(argv[++index], nullptr, 0));
    } else if (flag == "--games" && index + 1 < argc) {
      options.games = std::atoi(argv[++index]);
    } else if (flag == "--depth" && index + 1 < argc) {
      options.depth = std::atoi(argv[++index]);
    } else if (flag == "--chance-samples" && index + 1 < argc) {
      options.chance_samples = std::atoi(argv[++index]);
    } else if (flag == "--max-work" && index + 1 < argc) {
      options.max_work = std::strtoull(argv[++index], nullptr, 0);
    } else if (flag == "--max-moves" && index + 1 < argc) {
      options.max_moves = std::atoi(argv[++index]);
    } else if (flag == "--threads" && index + 1 < argc) {
      options.threads = std::atoi(argv[++index]);
    } else if (flag == "--w" && index + 1 < argc) {
      options.w = std::atof(argv[++index]);
    } else if (flag == "--scale" && index + 1 < argc) {
      options.scale = std::atof(argv[++index]);
    } else if (flag == "--parity-games" && index + 1 < argc) {
      options.parity_games = std::atoi(argv[++index]);
    } else if (flag == "--parity-moves" && index + 1 < argc) {
      options.parity_moves = std::atoi(argv[++index]);
    } else {
      std::cerr << "unknown argument " << flag << "\n";
      return 2;
    }
  }
  if (options.parity) return runParity(options);
  if (options.leaf_stats) return runLeafStats(options);
  return runCohort(options);
}
