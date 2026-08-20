#include "../../../src/core/native/public-behavior.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Distills the reference phase-D3/s5 policy into scalar action values.
// Features are extracted from a horizontally canonical state/action pair and
// five common-random, stratified one-ply successor samples.
namespace drop7::phase_q_student {

constexpr std::uint32_t kTrainingStart = 0x3d70'0000u;
constexpr std::uint32_t kTrainingEnd = 0x4d00'0000u;
constexpr int kCurrentPhaseFeatures = 24;
constexpr int kSuccessorMetrics = 17;
constexpr int kFeatureCount = 112;
constexpr int kHidden1 = 128;
constexpr int kHidden2 = 64;
constexpr int kChanceSamples = 5;
static_assert(kCurrentPhaseFeatures + 1 + 7 + 5 + 7 + 17 +
                  3 * kSuccessorMetrics ==
              kFeatureCount);

bool mirrorIsSmaller(const Board& board) {
  const Board mirrored = cfpi::detail::mirrorBoard(board);
  return std::lexicographical_compare(mirrored.begin(), mirrored.end(),
                                      board.begin(), board.end());
}

struct CanonicalAction {
  State state{};
  int action = -1;
  bool mirrored = false;
};

CanonicalAction canonicalize(const State& source, int physical_action) {
  CanonicalAction result;
  result.state = cfpi::detail::canonicalState(source, result.mirrored);
  result.action = result.mirrored ? kBoardSize - 1 - physical_action
                                  : physical_action;
  return result;
}

using Features = std::array<float, kFeatureCount>;

void appendPhaseFeatures(const cfpi::detail::PhaseFeatures& phase,
                         Features& result, int& offset) {
  const std::array<double, kCurrentPhaseFeatures> values{{
      static_cast<double>(phase.open_columns),
      phase.height_load,
      static_cast<double>(phase.solid_cells),
      static_cast<double>(phase.cracked_cells),
      static_cast<double>(phase.numbered_cells),
      static_cast<double>(phase.high_low_numbers),
      phase.direct_potential,
      phase.latent_chain_potential,
      phase.cracked_exposure,
      phase.solid_exposure,
      phase.adjacent_ones,
      phase.triple_twos,
      phase.dead_low_numbers,
      phase.projected_occupancy_debt,
      phase.residual_cover_debt,
      phase.cover_altitude_debt,
      phase.imminent_cover_altitude_debt,
      phase.peak_height_risk,
      phase.low_cap_load,
      phase.adjacent_low_cap_load,
      phase.quiet_build_options,
      phase.quiet_direct_gain,
      phase.trigger_readiness,
      phase.rise_trigger_readiness,
  }};
  for (const double value : values) {
    result[offset++] = static_cast<float>(value);
  }
}

std::array<float, kSuccessorMetrics> successorMetrics(
    const MoveResult& move) {
  const cfpi::PhaseMetrics phase = cfpi::evaluatePhaseMetrics(move.state);
  const cfpi::detail::PhaseFeatures detailed =
      cfpi::detail::extractPhaseFeatures(move.state);
  int clears = 0;
  int reveals = 0;
  int maximum_depth = 0;
  for (const Wave& wave : move.waves) {
    clears += wave.cleared;
    reveals += wave.revealed;
    maximum_depth = std::max(maximum_depth, wave.depth);
  }
  return {{
      static_cast<float>(phase.potential),
      static_cast<float>(phase.occupied),
      static_cast<float>(phase.covers),
      static_cast<float>(phase.maximum_height),
      static_cast<float>(phase.legal_columns),
      static_cast<float>(move.score_delta),
      static_cast<float>(clears),
      static_cast<float>(reveals),
      static_cast<float>(move.waves.size()),
      static_cast<float>(maximum_depth),
      move.state.game_over ? 1.0f : 0.0f,
      move.level_advanced ? 1.0f : 0.0f,
      move.cleared_board ? 1.0f : 0.0f,
      static_cast<float>(detailed.direct_potential),
      static_cast<float>(detailed.latent_chain_potential),
      static_cast<float>(detailed.peak_height_risk),
      static_cast<float>(detailed.residual_cover_debt),
  }};
}

Features extractCanonical(const State& state, int action) {
  if (!isLegal(state.board, action)) {
    throw std::invalid_argument("cannot featurize illegal action");
  }
  Features result{};
  int offset = 0;
  appendPhaseFeatures(cfpi::detail::extractPhaseFeatures(state), result,
                      offset);
  result[offset++] = static_cast<float>(cfpi::phasePotential(state));
  for (int disc = 1; disc <= kBoardSize; ++disc) {
    result[offset++] = state.next_disc == disc ? 1.0f : 0.0f;
  }
  for (int phase = 1; phase <= kMovesPerLevel; ++phase) {
    result[offset++] = state.moves_remaining == phase ? 1.0f : 0.0f;
  }
  std::array<int, kBoardSize> heights{};
  for (int column = 0; column < kBoardSize; ++column) {
    for (int row = 0; row < kBoardSize; ++row) {
      heights[column] += state.board[indexOf(row, column)] != kEmpty;
    }
    result[offset++] = static_cast<float>(heights[column]);
  }
  for (int column = 0; column < kBoardSize; ++column) {
    result[offset++] = action == column ? 1.0f : 0.0f;
  }
  result[offset++] = static_cast<float>(std::abs(action - 3));
  result[offset++] = static_cast<float>(heights[action]);
  result[offset++] =
      static_cast<float>(action > 0 ? heights[action - 1] : -1);
  result[offset++] =
      static_cast<float>(action + 1 < kBoardSize ? heights[action + 1] : -1);
  const int landing_row = kBoardSize - 1 - heights[action];
  result[offset++] = static_cast<float>(landing_row);
  Board placed = state.board;
  placed[indexOf(landing_row, action)] = state.next_disc;
  const int horizontal = lineLength(placed, landing_row, action, false);
  const int vertical = lineLength(placed, landing_row, action, true);
  result[offset++] = static_cast<float>(horizontal);
  result[offset++] = static_cast<float>(vertical);
  result[offset++] = static_cast<float>(state.next_disc);
  result[offset++] = horizontal == state.next_disc ? 1.0f : 0.0f;
  result[offset++] = vertical == state.next_disc ? 1.0f : 0.0f;

  std::array<float, kSuccessorMetrics> sum{};
  std::array<float, kSuccessorMetrics> minimum{};
  std::array<float, kSuccessorMetrics> maximum{};
  minimum.fill(std::numeric_limits<float>::infinity());
  maximum.fill(-std::numeric_limits<float>::infinity());
  const std::uint32_t state_seed =
      cfpi::detail::scenarioSeedForState(state, 0xd707'5eedu, 1);
  for (int sample = 0; sample < kChanceSamples; ++sample) {
    cfpi::detail::StratifiedRandom random{
        state_seed, sample, kChanceSamples, 0,
    };
    MoveResult move;
    if (!cfpi::detail::playMoveSampled(state, action, random, move)) {
      throw std::runtime_error("one-ply feature transition failed");
    }
    if (!move.state.game_over) {
      move.state.next_disc =
          cfpi::detail::sampledNextDisc(state_seed, sample, kChanceSamples);
    }
    const auto metrics = successorMetrics(move);
    for (int metric = 0; metric < kSuccessorMetrics; ++metric) {
      sum[metric] += metrics[metric];
      minimum[metric] = std::min(minimum[metric], metrics[metric]);
      maximum[metric] = std::max(maximum[metric], metrics[metric]);
    }
  }
  for (const float value : sum) result[offset++] = value / kChanceSamples;
  for (const float value : minimum) result[offset++] = value;
  for (const float value : maximum) result[offset++] = value;
  if (offset != kFeatureCount) {
    throw std::logic_error("Q feature count invariant failed");
  }
  return result;
}

Features extract(const State& source, int physical_action) {
  const CanonicalAction canonical = canonicalize(source, physical_action);
  return extractCanonical(canonical.state, canonical.action);
}

float relu(float value) { return std::max(0.0f, value); }

struct ForwardCache {
  Features features{};
  std::array<float, kHidden1> pre1{};
  std::array<float, kHidden1> hidden1{};
  std::array<float, kHidden2> pre2{};
  std::array<float, kHidden2> hidden2{};
  float value = 0.0f;
};

struct Network {
  std::array<float, kHidden1 * kFeatureCount> weight1{};
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kHidden2> weight3{};
  float bias3 = 0.0f;

  explicit Network(std::uint32_t seed = 0x3d7a'5153u) {
    Mulberry32 random(seed);
    const auto fill = [&random](auto& values, float scale) {
      for (float& value : values) {
        value = static_cast<float>((2.0 * random.nextUnit() - 1.0) * scale);
      }
    };
    fill(weight1, std::sqrt(6.0f / (kFeatureCount + kHidden1)));
    fill(weight2, std::sqrt(6.0f / (kHidden1 + kHidden2)));
    fill(weight3, std::sqrt(6.0f / (kHidden2 + 1)));
  }

  float value(const Features& features) const {
    return forward(features, nullptr);
  }

  float forward(const Features& features, ForwardCache* cache) const {
    ForwardCache local;
    ForwardCache& result = cache != nullptr ? *cache : local;
    result.features = features;
    for (int hidden = 0; hidden < kHidden1; ++hidden) {
      float sum = bias1[hidden];
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        sum += weight1[hidden * kFeatureCount + feature] * features[feature];
      }
      result.pre1[hidden] = sum;
      result.hidden1[hidden] = relu(sum);
    }
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      float sum = bias2[hidden];
      for (int prior = 0; prior < kHidden1; ++prior) {
        sum += weight2[hidden * kHidden1 + prior] * result.hidden1[prior];
      }
      result.pre2[hidden] = sum;
      result.hidden2[hidden] = relu(sum);
    }
    result.value = bias3;
    for (int hidden = 0; hidden < kHidden2; ++hidden) {
      result.value += weight3[hidden] * result.hidden2[hidden];
    }
    return result.value;
  }

  std::size_t parameterBytes() const {
    return (weight1.size() + bias1.size() + weight2.size() + bias2.size() +
            weight3.size() + 1) *
           sizeof(float);
  }
};

constexpr std::array<int, kBoardSize> kColumnOrder{{3, 2, 4, 1, 5, 0, 6}};

struct TeacherQuery {
  State canonical{};
  std::array<double, kBoardSize> values{};
  int action = -1;
  int completed_depth = 0;
  std::uint64_t work = 0;
  bool mirrored = false;
};

TeacherQuery queryTeacher(const State& source,
                          const cfpi::BehaviorOptions& options) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  cfpi::detail::SearchContext context(options);
  std::array<double, kBoardSize> completed_values{};
  completed_values.fill(-std::numeric_limits<double>::infinity());
  int completed_action = -1;
  int completed_depth = 0;
  for (int depth = 1; depth <= options.max_depth; ++depth) {
    std::array<double, kBoardSize> candidate_values{};
    candidate_values.fill(-std::numeric_limits<double>::infinity());
    int candidate_action = -1;
    double candidate_best = -std::numeric_limits<double>::infinity();
    try {
      for (const int column : kColumnOrder) {
        if (!isLegal(canonical.board, column)) continue;
        const double value =
            cfpi::detail::evaluateAction(canonical, column, depth, context);
        candidate_values[column] = value;
        if (value > candidate_best) {
          candidate_best = value;
          candidate_action = column;
        }
      }
    } catch (const cfpi::detail::WorkLimitReached&) {
      break;
    }
    if (candidate_action < 0) break;
    completed_values = candidate_values;
    completed_action = candidate_action;
    completed_depth = depth;
  }
  if (completed_action < 0) {
    throw std::runtime_error("Q teacher failed before depth one");
  }
  return {canonical, completed_values, completed_action, completed_depth,
          context.work, mirrored};
}

struct ActionRecord {
  Features features{};
  double teacher_value = -std::numeric_limits<double>::infinity();
  float target = 0.0f;
  bool legal = false;
};

struct Group {
  std::array<ActionRecord, kBoardSize> actions{};
  int teacher_action = -1;
  int teacher_depth = 0;
};

Group makeGroup(const TeacherQuery& teacher) {
  Group group;
  group.teacher_action = teacher.action;
  group.teacher_depth = teacher.completed_depth;
  double mean = 0.0;
  int count = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!std::isfinite(teacher.values[column])) continue;
    mean += teacher.values[column];
    ++count;
  }
  if (count == 0) throw std::runtime_error("teacher group has no legal Q");
  mean /= count;
  double squared = 0.0;
  for (const double value : teacher.values) {
    if (std::isfinite(value)) squared += (value - mean) * (value - mean);
  }
  const double scale = std::max(100.0, std::sqrt(squared / count));
  for (int column = 0; column < kBoardSize; ++column) {
    if (!std::isfinite(teacher.values[column])) continue;
    ActionRecord& action = group.actions[column];
    action.features = extractCanonical(teacher.canonical, column);
    action.teacher_value = teacher.values[column];
    action.target = static_cast<float>(
        std::clamp((teacher.values[column] - mean) / scale, -6.0, 6.0));
    action.legal = true;
  }
  return group;
}

struct Corpus {
  std::vector<Group> groups;
  std::uint64_t teacher_work = 0;
  int depth_three = 0;
  int actions = 0;
};

void validateTrainingSeeds(std::uint32_t start, int games) {
  if (games <= 0) throw std::invalid_argument("game count must be positive");
  const std::uint64_t end =
      static_cast<std::uint64_t>(start) + static_cast<std::uint64_t>(games);
  if (start < kTrainingStart || end > kTrainingEnd) {
    throw std::invalid_argument("environment seed leaves training partition");
  }
}

struct FeatureNormalizer {
  Features mean{};
  Features scale{};

  Features apply(const Features& source) const {
    Features result{};
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      result[feature] = std::clamp(
          (source[feature] - mean[feature]) / scale[feature], -8.0f, 8.0f);
    }
    return result;
  }
};

int chooseStudentAction(const Network& network,
                        const FeatureNormalizer& normalizer,
                        const State& source) {
  bool mirrored = false;
  const State canonical = cfpi::detail::canonicalState(source, mirrored);
  int best = -1;
  float best_value = -std::numeric_limits<float>::infinity();
  for (const int column : kColumnOrder) {
    if (!isLegal(canonical.board, column)) continue;
    const Features features =
        normalizer.apply(extractCanonical(canonical, column));
    const float value = network.value(features);
    if (value > best_value) {
      best_value = value;
      best = column;
    }
  }
  if (best < 0) return -1;
  return mirrored ? kBoardSize - 1 - best : best;
}

Corpus collectCorpus(std::uint32_t seed_start, int games, int maximum_moves,
                     const cfpi::BehaviorOptions& options,
                     std::string_view label) {
  validateTrainingSeeds(seed_start, games);
  Corpus corpus;
  corpus.groups.reserve(static_cast<std::size_t>(games * maximum_moves));
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      const TeacherQuery teacher = queryTeacher(state, options);
      Group group = makeGroup(teacher);
      for (const ActionRecord& action : group.actions) {
        corpus.actions += action.legal;
      }
      corpus.groups.push_back(std::move(group));
      corpus.teacher_work += teacher.work;
      corpus.depth_three += teacher.completed_depth == 3;
      const int physical_action =
          teacher.mirrored ? kBoardSize - 1 - teacher.action : teacher.action;
      MoveResult move;
      if (!playHeadlessMove(state, seed, physical_action, move)) {
        throw std::runtime_error("Q teacher selected illegal action");
      }
    }
    std::cerr << label << ' ' << (game + 1) << '/' << games << " seed 0x"
              << std::hex << seed << std::dec << " groups "
              << corpus.groups.size() << '\n';
  }
  return corpus;
}

FeatureNormalizer fitNormalizer(const Corpus& corpus) {
  if (corpus.actions == 0) throw std::invalid_argument("empty Q corpus");
  FeatureNormalizer normalizer;
  for (const Group& group : corpus.groups) {
    for (const ActionRecord& action : group.actions) {
      if (!action.legal) continue;
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        normalizer.mean[feature] += action.features[feature];
      }
    }
  }
  for (float& value : normalizer.mean) value /= corpus.actions;
  for (const Group& group : corpus.groups) {
    for (const ActionRecord& action : group.actions) {
      if (!action.legal) continue;
      for (int feature = 0; feature < kFeatureCount; ++feature) {
        const double delta =
            action.features[feature] - normalizer.mean[feature];
        normalizer.scale[feature] += static_cast<float>(delta * delta);
      }
    }
  }
  for (float& value : normalizer.scale) {
    value = std::max(1.0e-4f,
                     std::sqrt(value / static_cast<float>(corpus.actions)));
  }
  return normalizer;
}

void normalizeCorpus(Corpus& corpus, const FeatureNormalizer& normalizer) {
  for (Group& group : corpus.groups) {
    for (ActionRecord& action : group.actions) {
      if (action.legal) action.features = normalizer.apply(action.features);
    }
  }
}

void saveCorpus(const std::string& path, const Corpus& training,
                const Corpus& heldout) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open Q corpus artifact");
  const std::array<std::uint32_t, 5> header{{
      0x4451'3751u,
      1u,
      static_cast<std::uint32_t>(kFeatureCount),
      static_cast<std::uint32_t>(training.groups.size()),
      static_cast<std::uint32_t>(heldout.groups.size()),
  }};
  output.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size() * sizeof(header[0])));
  const auto write_corpus = [&output](const Corpus& corpus) {
    for (const Group& group : corpus.groups) {
      const std::array<std::int32_t, 2> metadata{{group.teacher_action,
                                                 group.teacher_depth}};
      output.write(reinterpret_cast<const char*>(metadata.data()),
                   static_cast<std::streamsize>(metadata.size() *
                                                sizeof(metadata[0])));
      for (const ActionRecord& action : group.actions) {
        const std::uint8_t legal = action.legal ? 1 : 0;
        output.write(reinterpret_cast<const char*>(&legal), sizeof(legal));
        output.write(reinterpret_cast<const char*>(&action.teacher_value),
                     sizeof(action.teacher_value));
        output.write(reinterpret_cast<const char*>(&action.target),
                     sizeof(action.target));
        output.write(reinterpret_cast<const char*>(action.features.data()),
                     static_cast<std::streamsize>(action.features.size() *
                                                  sizeof(action.features[0])));
      }
    }
  };
  write_corpus(training);
  write_corpus(heldout);
  if (!output) throw std::runtime_error("could not write Q corpus artifact");
}

struct CorpusPair {
  Corpus training;
  Corpus heldout;
};

CorpusPair loadCorpus(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("could not open reusable Q corpus");
  std::array<std::uint32_t, 5> header{};
  input.read(reinterpret_cast<char*>(header.data()),
             static_cast<std::streamsize>(header.size() * sizeof(header[0])));
  if (header[0] != 0x4451'3751u || header[1] != 1u ||
      header[2] != static_cast<std::uint32_t>(kFeatureCount)) {
    throw std::runtime_error("reusable Q corpus header mismatch");
  }
  const auto read_corpus = [&input](std::uint32_t group_count) {
    Corpus corpus;
    corpus.groups.resize(group_count);
    for (Group& group : corpus.groups) {
      std::array<std::int32_t, 2> metadata{};
      input.read(reinterpret_cast<char*>(metadata.data()),
                 static_cast<std::streamsize>(metadata.size() *
                                              sizeof(metadata[0])));
      group.teacher_action = metadata[0];
      group.teacher_depth = metadata[1];
      corpus.depth_three += group.teacher_depth == 3;
      for (ActionRecord& action : group.actions) {
        std::uint8_t legal = 0;
        input.read(reinterpret_cast<char*>(&legal), sizeof(legal));
        input.read(reinterpret_cast<char*>(&action.teacher_value),
                   sizeof(action.teacher_value));
        input.read(reinterpret_cast<char*>(&action.target),
                   sizeof(action.target));
        input.read(reinterpret_cast<char*>(action.features.data()),
                   static_cast<std::streamsize>(action.features.size() *
                                                sizeof(action.features[0])));
        action.legal = legal != 0;
        corpus.actions += action.legal;
      }
    }
    return corpus;
  };
  CorpusPair result{read_corpus(header[3]), read_corpus(header[4])};
  if (!input) throw std::runtime_error("reusable Q corpus is truncated");
  return result;
}

void appendCorpus(Corpus& destination, Corpus&& source) {
  destination.teacher_work += source.teacher_work;
  destination.depth_three += source.depth_three;
  destination.actions += source.actions;
  destination.groups.insert(destination.groups.end(),
                            std::make_move_iterator(source.groups.begin()),
                            std::make_move_iterator(source.groups.end()));
}

struct Tensors {
  std::array<float, kHidden1 * kFeatureCount> weight1{};
  std::array<float, kHidden1> bias1{};
  std::array<float, kHidden2 * kHidden1> weight2{};
  std::array<float, kHidden2> bias2{};
  std::array<float, kHidden2> weight3{};
  float bias3 = 0.0f;

  void clear() {
    weight1.fill(0.0f);
    bias1.fill(0.0f);
    weight2.fill(0.0f);
    bias2.fill(0.0f);
    weight3.fill(0.0f);
    bias3 = 0.0f;
  }
};

void accumulateBackward(const Network& network, const ForwardCache& cache,
                        float output_delta, Tensors& gradient) {
  gradient.bias3 += output_delta;
  std::array<float, kHidden2> delta2{};
  for (int hidden = 0; hidden < kHidden2; ++hidden) {
    gradient.weight3[hidden] += output_delta * cache.hidden2[hidden];
    const float value = network.weight3[hidden] * output_delta;
    delta2[hidden] = cache.pre2[hidden] > 0.0f ? value : 0.0f;
    gradient.bias2[hidden] += delta2[hidden];
    for (int prior = 0; prior < kHidden1; ++prior) {
      gradient.weight2[hidden * kHidden1 + prior] +=
          delta2[hidden] * cache.hidden1[prior];
    }
  }
  for (int hidden = 0; hidden < kHidden1; ++hidden) {
    float value = 0.0f;
    for (int next = 0; next < kHidden2; ++next) {
      value += network.weight2[next * kHidden1 + hidden] * delta2[next];
    }
    const float delta1 = cache.pre1[hidden] > 0.0f ? value : 0.0f;
    gradient.bias1[hidden] += delta1;
    for (int feature = 0; feature < kFeatureCount; ++feature) {
      gradient.weight1[hidden * kFeatureCount + feature] +=
          delta1 * cache.features[feature];
    }
  }
}

float sigmoid(float value) {
  if (value >= 0.0f) {
    const float exponential = std::exp(-value);
    return 1.0f / (1.0f + exponential);
  }
  const float exponential = std::exp(value);
  return exponential / (1.0f + exponential);
}

float softplus(float value) {
  return std::max(0.0f, value) + std::log1p(std::exp(-std::abs(value)));
}

struct LossParts {
  double value = 0.0;
  double ranking = 0.0;
};

LossParts accumulateGroupGradient(const Network& network, const Group& group,
                                  Tensors& gradient) {
  std::array<ForwardCache, kBoardSize> caches{};
  std::array<float, kBoardSize> predictions{};
  std::array<float, kBoardSize> deltas{};
  int legal_count = 0;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!group.actions[column].legal) continue;
    predictions[column] =
        network.forward(group.actions[column].features, &caches[column]);
    ++legal_count;
  }
  LossParts loss;
  constexpr float value_weight = 0.5f;
  for (int column = 0; column < kBoardSize; ++column) {
    if (!group.actions[column].legal) continue;
    const float difference =
        predictions[column] - group.actions[column].target;
    loss.value += 0.5 * difference * difference / legal_count;
    deltas[column] += value_weight * difference / legal_count;
  }

  float total_pair_weight = 0.0f;
  for (int first = 0; first < kBoardSize; ++first) {
    if (!group.actions[first].legal) continue;
    for (int second = first + 1; second < kBoardSize; ++second) {
      if (!group.actions[second].legal) continue;
      const float target_difference =
          group.actions[first].target - group.actions[second].target;
      if (std::abs(target_difference) < 1.0e-5f) continue;
      float weight = 0.5f + std::min(1.5f, std::abs(target_difference));
      if (first == group.teacher_action || second == group.teacher_action) {
        weight *= 1.5f;
      }
      total_pair_weight += weight;
    }
  }
  if (total_pair_weight > 0.0f) {
    for (int first = 0; first < kBoardSize; ++first) {
      if (!group.actions[first].legal) continue;
      for (int second = first + 1; second < kBoardSize; ++second) {
        if (!group.actions[second].legal) continue;
        const float target_difference =
            group.actions[first].target - group.actions[second].target;
        if (std::abs(target_difference) < 1.0e-5f) continue;
        const float sign = target_difference > 0.0f ? 1.0f : -1.0f;
        float weight = 0.5f + std::min(1.5f, std::abs(target_difference));
        if (first == group.teacher_action || second == group.teacher_action) {
          weight *= 1.5f;
        }
        weight /= total_pair_weight;
        const float signed_margin =
            sign * (predictions[first] - predictions[second]);
        loss.ranking += weight * softplus(-signed_margin);
        const float derivative = -weight * sign * sigmoid(-signed_margin);
        deltas[first] += derivative;
        deltas[second] -= derivative;
      }
    }
  }
  for (int column = 0; column < kBoardSize; ++column) {
    if (group.actions[column].legal) {
      accumulateBackward(network, caches[column], deltas[column], gradient);
    }
  }
  return loss;
}

struct Adam {
  Tensors first;
  Tensors second;
  int steps = 0;

  void update(Network& network, const Tensors& gradient, int batch_size,
              float learning_rate) {
    ++steps;
    constexpr float beta1 = 0.9f;
    constexpr float beta2 = 0.999f;
    constexpr float epsilon = 1.0e-8f;
    constexpr float decay = 2.0e-5f;
    const float correction =
        learning_rate *
        std::sqrt(1.0f - std::pow(beta2, static_cast<float>(steps))) /
        (1.0f - std::pow(beta1, static_cast<float>(steps)));
    const float inverse_batch = 1.0f / batch_size;
    const auto update_values = [&](auto& parameters, const auto& gradients,
                                   auto& first_values, auto& second_values,
                                   bool regularize) {
      for (std::size_t index = 0; index < parameters.size(); ++index) {
        const float value = gradients[index] * inverse_batch;
        first_values[index] = beta1 * first_values[index] +
                              (1.0f - beta1) * value;
        second_values[index] = beta2 * second_values[index] +
                               (1.0f - beta2) * value * value;
        parameters[index] -=
            correction * first_values[index] /
                (std::sqrt(second_values[index]) + epsilon) +
            (regularize ? learning_rate * decay * parameters[index] : 0.0f);
      }
    };
    update_values(network.weight1, gradient.weight1, first.weight1,
                  second.weight1, true);
    update_values(network.bias1, gradient.bias1, first.bias1, second.bias1,
                  false);
    update_values(network.weight2, gradient.weight2, first.weight2,
                  second.weight2, true);
    update_values(network.bias2, gradient.bias2, first.bias2, second.bias2,
                  false);
    update_values(network.weight3, gradient.weight3, first.weight3,
                  second.weight3, true);

    const float scalar_gradient = gradient.bias3 * inverse_batch;
    first.bias3 = beta1 * first.bias3 + (1.0f - beta1) * scalar_gradient;
    second.bias3 = beta2 * second.bias3 +
                   (1.0f - beta2) * scalar_gradient * scalar_gradient;
    network.bias3 -= correction * first.bias3 /
                     (std::sqrt(second.bias3) + epsilon);
  }
};

struct PolicyMetrics {
  double top1 = 0.0;
  double top2 = 0.0;
  double cross_entropy = 0.0;
  double q_rmse = 0.0;
  double pair_accuracy = 0.0;
  std::uint64_t pairs = 0;
};

PolicyMetrics evaluatePolicy(const Network& network, const Corpus& corpus) {
  if (corpus.groups.empty()) throw std::invalid_argument("empty Q corpus");
  PolicyMetrics metrics;
  double squared_error = 0.0;
  std::uint64_t q_count = 0;
  std::uint64_t correct_pairs = 0;
  for (const Group& group : corpus.groups) {
    std::array<float, kBoardSize> values{};
    values.fill(-std::numeric_limits<float>::infinity());
    for (int column = 0; column < kBoardSize; ++column) {
      if (!group.actions[column].legal) continue;
      values[column] = network.value(group.actions[column].features);
      const double error = values[column] - group.actions[column].target;
      squared_error += error * error;
      ++q_count;
    }
    std::array<int, kBoardSize> ranking = kColumnOrder;
    std::stable_sort(ranking.begin(), ranking.end(), [&](int left, int right) {
      return values[left] > values[right];
    });
    metrics.top1 += ranking[0] == group.teacher_action;
    metrics.top2 += ranking[0] == group.teacher_action ||
                    ranking[1] == group.teacher_action;
    const float maximum = values[ranking[0]];
    double total = 0.0;
    for (int column = 0; column < kBoardSize; ++column) {
      if (group.actions[column].legal) {
        total += std::exp(values[column] - maximum);
      }
    }
    metrics.cross_entropy +=
        std::log(total) + maximum - values[group.teacher_action];
    for (int first = 0; first < kBoardSize; ++first) {
      if (!group.actions[first].legal) continue;
      for (int second = first + 1; second < kBoardSize; ++second) {
        if (!group.actions[second].legal) continue;
        const float target_difference =
            group.actions[first].target - group.actions[second].target;
        if (std::abs(target_difference) < 1.0e-5f) continue;
        const float prediction_difference = values[first] - values[second];
        correct_pairs += target_difference * prediction_difference > 0.0f;
        ++metrics.pairs;
      }
    }
  }
  const double groups = static_cast<double>(corpus.groups.size());
  metrics.top1 /= groups;
  metrics.top2 /= groups;
  metrics.cross_entropy /= groups;
  metrics.q_rmse = std::sqrt(squared_error / std::max<std::uint64_t>(1, q_count));
  metrics.pair_accuracy =
      static_cast<double>(correct_pairs) /
      std::max<std::uint64_t>(1, metrics.pairs);
  return metrics;
}

void train(Network& network, const Corpus& corpus, int epochs, int batch_size,
           float learning_rate) {
  if (corpus.groups.empty()) throw std::invalid_argument("empty Q corpus");
  Adam adam;
  Tensors gradient;
  std::vector<int> order(corpus.groups.size());
  std::iota(order.begin(), order.end(), 0);
  Mulberry32 random(0x3d7a'414du);
  for (int epoch = 0; epoch < epochs; ++epoch) {
    for (std::size_t offset = order.size(); offset > 1; --offset) {
      const std::size_t swap = static_cast<std::size_t>(
          (static_cast<std::uint64_t>(random.nextBits()) * offset) >> 32);
      std::swap(order[offset - 1], order[swap]);
    }
    LossParts loss;
    for (std::size_t begin = 0; begin < order.size();
         begin += static_cast<std::size_t>(batch_size)) {
      const std::size_t end = std::min(
          order.size(), begin + static_cast<std::size_t>(batch_size));
      gradient.clear();
      for (std::size_t index = begin; index < end; ++index) {
        const LossParts sample =
            accumulateGroupGradient(network, corpus.groups[order[index]],
                                    gradient);
        loss.value += sample.value;
        loss.ranking += sample.ranking;
      }
      adam.update(network, gradient, static_cast<int>(end - begin),
                  learning_rate);
    }
    if (epoch == 0 || (epoch + 1) % 10 == 0 || epoch + 1 == epochs) {
      std::cerr << "Q-distill epoch " << (epoch + 1) << '/' << epochs
                << " value " << std::fixed << std::setprecision(4)
                << loss.value / order.size() << " rank "
                << loss.ranking / order.size() << '\n';
    }
  }
}

struct GameMetrics {
  double mean_score = 0.0;
  double mean_moves = 0.0;
  int censored = 0;
};

GameMetrics evaluateGames(const Network& network,
                          const FeatureNormalizer& normalizer,
                          std::uint32_t seed_start, int games,
                          int maximum_moves) {
  validateTrainingSeeds(seed_start, games);
  GameMetrics metrics;
  for (int game = 0; game < games; ++game) {
    const std::uint32_t seed =
        seed_start + static_cast<std::uint32_t>(game);
    State state = initialHeadlessState(seed);
    while (!state.game_over && state.moves_played < maximum_moves) {
      const int action = chooseStudentAction(network, normalizer, state);
      if (!isLegal(state.board, action)) {
        throw std::runtime_error("Q student selected illegal action");
      }
      MoveResult move;
      if (!playHeadlessMove(state, seed, action, move)) {
        throw std::runtime_error("Q student transition rejected legal action");
      }
    }
    metrics.mean_score += static_cast<double>(state.score) / games;
    metrics.mean_moves += static_cast<double>(state.moves_played) / games;
    metrics.censored += !state.game_over;
    std::cerr << "Q-student " << (game + 1) << '/' << games << " seed 0x"
              << std::hex << seed << std::dec << ' ' << state.score << " ("
              << state.moves_played << " moves)\n";
  }
  return metrics;
}

void saveModel(const std::string& path, const Network& network,
               const FeatureNormalizer& normalizer) {
  std::ofstream output(path, std::ios::binary);
  if (!output) throw std::runtime_error("could not open Q model artifact");
  const std::array<std::uint32_t, 5> header{{
      0x4451'3753u, kFeatureCount, kHidden1, kHidden2, 1u,
  }};
  output.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size() * sizeof(header[0])));
  const auto write = [&output](const auto& values) {
    output.write(reinterpret_cast<const char*>(values.data()),
                 static_cast<std::streamsize>(values.size() *
                                              sizeof(values[0])));
  };
  write(normalizer.mean);
  write(normalizer.scale);
  write(network.weight1);
  write(network.bias1);
  write(network.weight2);
  write(network.bias2);
  write(network.weight3);
  output.write(reinterpret_cast<const char*>(&network.bias3),
               sizeof(network.bias3));
  if (!output) throw std::runtime_error("could not write Q model artifact");
}

struct RunConfig {
  int train_games = 8;
  int heldout_games = 3;
  int evaluation_games = 6;
  int label_moves = 80;
  int evaluation_moves = 300;
  int epochs = 80;
  int batch_size = 32;
  float learning_rate = 0.001f;
  std::string output = "/tmp/drop7-phase-q-student.json";
  std::string model = "/tmp/drop7-phase-q-student.bin";
  std::string corpus = "/tmp/drop7-phase-q-corpus.bin";
  std::string reuse_corpus;
  int base_train_games = 0;
  int base_heldout_games = 0;
};

constexpr std::uint32_t kTrainSeedStart = 0x3d7a'0000u;
constexpr std::uint32_t kHeldoutSeedStart = 0x3d7b'0000u;
constexpr std::uint32_t kEvaluationSeedStart = 0x3d7c'0000u;

int positiveInteger(const char* value, std::string_view name) {
  std::size_t consumed = 0;
  const std::string text = value;
  const long long parsed = std::stoll(text, &consumed, 0);
  if (consumed != text.size() || parsed <= 0 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return static_cast<int>(parsed);
}

float positiveFloat(const char* value, std::string_view name) {
  std::size_t consumed = 0;
  const std::string text = value;
  const float parsed = std::stof(text, &consumed);
  if (consumed != text.size() || !(parsed > 0.0f) ||
      !std::isfinite(parsed)) {
    throw std::invalid_argument(std::string(name) + " must be positive");
  }
  return parsed;
}

RunConfig parseRunConfig(int argc, char** argv) {
  RunConfig config;
  for (int index = 2; index < argc; ++index) {
    const std::string argument = argv[index];
    if (index + 1 >= argc) {
      throw std::invalid_argument("missing value for " + argument);
    }
    const char* value = argv[++index];
    if (argument == "--train-games") {
      config.train_games = positiveInteger(value, argument);
    } else if (argument == "--heldout-games") {
      config.heldout_games = positiveInteger(value, argument);
    } else if (argument == "--evaluation-games") {
      config.evaluation_games = positiveInteger(value, argument);
    } else if (argument == "--label-moves") {
      config.label_moves = positiveInteger(value, argument);
    } else if (argument == "--evaluation-moves") {
      config.evaluation_moves = positiveInteger(value, argument);
    } else if (argument == "--epochs") {
      config.epochs = positiveInteger(value, argument);
    } else if (argument == "--batch-size") {
      config.batch_size = positiveInteger(value, argument);
    } else if (argument == "--learning-rate") {
      config.learning_rate = positiveFloat(value, argument);
    } else if (argument == "--output") {
      config.output = value;
    } else if (argument == "--model") {
      config.model = value;
    } else if (argument == "--corpus") {
      config.corpus = value;
    } else if (argument == "--reuse-corpus") {
      config.reuse_corpus = value;
    } else if (argument == "--base-train-games") {
      config.base_train_games = positiveInteger(value, argument);
    } else if (argument == "--base-heldout-games") {
      config.base_heldout_games = positiveInteger(value, argument);
    } else {
      throw std::invalid_argument("unknown argument " + argument);
    }
  }
  return config;
}

void writeMetrics(std::ostream& output, const PolicyMetrics& metrics) {
  output << "{\"top1\":" << metrics.top1 << ",\"top2\":" << metrics.top2
         << ",\"crossEntropy\":" << metrics.cross_entropy
         << ",\"qRmse\":" << metrics.q_rmse
         << ",\"pairAccuracy\":" << metrics.pair_accuracy << '}';
}

void writeArtifact(const RunConfig& config, const Corpus& training,
                   const Corpus& heldout,
                   const PolicyMetrics& training_metrics,
                   const PolicyMetrics& heldout_metrics,
                   const GameMetrics& game_metrics, bool passed,
                   const Network& network, double elapsed_seconds) {
  std::ofstream output(config.output);
  if (!output) throw std::runtime_error("could not open Q result artifact");
  const int all_groups =
      static_cast<int>(training.groups.size() + heldout.groups.size());
  const double depth_three_rate =
      static_cast<double>(training.depth_three + heldout.depth_three) /
      std::max(1, all_groups);
  output << std::setprecision(10)
         << "{\n  \"format\": \"drop7-phase-q-student-v1\",\n"
         << "  \"trainingSeedOnly\": true,\n"
         << "  \"teacher\": \"phase-d3-s5-exact-root-q\",\n"
         << "  \"network\": \"112x128x64x1-action-scorer\",\n"
         << "  \"featureDesign\": \"engineered-phase+action-geometry+five-stratum-one-ply-mean-min-max\",\n"
         << "  \"parameterBytes\": " << network.parameterBytes() << ",\n"
         << "  \"trainGroups\": " << training.groups.size() << ",\n"
         << "  \"trainActions\": " << training.actions << ",\n"
         << "  \"heldoutGroups\": " << heldout.groups.size() << ",\n"
         << "  \"heldoutActions\": " << heldout.actions << ",\n"
         << "  \"teacherDepthThreeRate\": " << depth_three_rate << ",\n"
         << "  \"training\": ";
  writeMetrics(output, training_metrics);
  output << ",\n  \"heldout\": ";
  writeMetrics(output, heldout_metrics);
  output << ",\n  \"student\": {\"meanScore\":"
         << game_metrics.mean_score << ",\"meanMoves\":"
         << game_metrics.mean_moves << ",\"censored\":"
         << game_metrics.censored << "},\n"
         << "  \"gates\": {\"top1\":0.75,\"top2\":0.92,"
            "\"studentMeanScore\":250000,\"studentMeanMoves\":75},\n"
         << "  \"qualified\": " << (passed ? "true" : "false") << ",\n"
         << "  \"decision\": \"" << (passed ? "advance" : "reject")
         << "\",\n  \"model\": \"" << config.model
         << "\",\n  \"corpus\": \"" << config.corpus
         << "\",\n  \"elapsedSeconds\": " << elapsed_seconds << "\n}\n";
}

int runPilot(const RunConfig& config, std::ostream& output) {
  validateTrainingSeeds(kTrainSeedStart, config.train_games);
  validateTrainingSeeds(kHeldoutSeedStart, config.heldout_games);
  validateTrainingSeeds(kEvaluationSeedStart, config.evaluation_games);
  const auto started = std::chrono::steady_clock::now();
  cfpi::BehaviorOptions teacher;
  teacher.max_depth = 3;
  teacher.chance_samples = kChanceSamples;
  teacher.max_work = 1'000'000;
  teacher.max_cache_entries = 40'000;

  Corpus training;
  Corpus heldout;
  if (config.reuse_corpus.empty()) {
    training = collectCorpus(kTrainSeedStart, config.train_games,
                             config.label_moves, teacher, "Q-train");
    heldout = collectCorpus(kHeldoutSeedStart, config.heldout_games,
                            config.label_moves, teacher, "Q-heldout");
  } else {
    if (config.base_train_games <= 0 || config.base_heldout_games <= 0 ||
        config.base_train_games > config.train_games ||
        config.base_heldout_games > config.heldout_games) {
      throw std::invalid_argument("invalid reusable corpus game counts");
    }
    CorpusPair reused = loadCorpus(config.reuse_corpus);
    training = std::move(reused.training);
    heldout = std::move(reused.heldout);
    if (config.train_games > config.base_train_games) {
      appendCorpus(
          training,
          collectCorpus(kTrainSeedStart +
                            static_cast<std::uint32_t>(config.base_train_games),
                        config.train_games - config.base_train_games,
                        config.label_moves, teacher, "Q-train-append"));
    }
    if (config.heldout_games > config.base_heldout_games) {
      appendCorpus(
          heldout,
          collectCorpus(
              kHeldoutSeedStart +
                  static_cast<std::uint32_t>(config.base_heldout_games),
              config.heldout_games - config.base_heldout_games,
              config.label_moves, teacher, "Q-heldout-append"));
    }
  }
  saveCorpus(config.corpus, training, heldout);
  const FeatureNormalizer normalizer = fitNormalizer(training);
  normalizeCorpus(training, normalizer);
  normalizeCorpus(heldout, normalizer);

  Network network;
  train(network, training, config.epochs, config.batch_size,
        config.learning_rate);
  const PolicyMetrics training_metrics = evaluatePolicy(network, training);
  const PolicyMetrics heldout_metrics = evaluatePolicy(network, heldout);
  const GameMetrics game_metrics =
      evaluateGames(network, normalizer, kEvaluationSeedStart,
                    config.evaluation_games, config.evaluation_moves);
  const bool passed = heldout_metrics.top1 >= 0.75 &&
                      heldout_metrics.top2 >= 0.92 &&
                      game_metrics.mean_score >= 250'000.0 &&
                      game_metrics.mean_moves >= 75.0;
  saveModel(config.model, network, normalizer);
  const double elapsed_seconds = std::chrono::duration<double>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
  writeArtifact(config, training, heldout, training_metrics, heldout_metrics,
                game_metrics, passed, network, elapsed_seconds);
  output << std::fixed << std::setprecision(6)
         << "PHASE_Q_STUDENT_RESULT {\"trainingSeedOnly\":true"
         << ",\"trainGroups\":" << training.groups.size()
         << ",\"heldoutGroups\":" << heldout.groups.size()
         << ",\"trainTop1\":" << training_metrics.top1
         << ",\"trainCE\":" << training_metrics.cross_entropy
         << ",\"heldoutTop1\":" << heldout_metrics.top1
         << ",\"heldoutTop2\":" << heldout_metrics.top2
         << ",\"heldoutCE\":" << heldout_metrics.cross_entropy
         << ",\"studentMeanScore\":" << game_metrics.mean_score
         << ",\"studentMeanMoves\":" << game_metrics.mean_moves
         << ",\"qualified\":" << (passed ? "true" : "false")
         << ",\"decision\":\"" << (passed ? "advance" : "reject")
         << "\",\"artifact\":\"" << config.output << "\"}\n";
  return 0;
}

bool selfTest(std::ostream& output) {
  State state;
  state.board = initialBoard();
  state.board[indexOf(5, 0)] = 3;
  state.board[indexOf(5, 1)] = 5;
  state.board[indexOf(4, 1)] = 2;
  state.board[indexOf(5, 4)] = 4;
  state.next_disc = 6;
  state.moves_remaining = 3;
  State mirrored = state;
  mirrored.board = cfpi::detail::mirrorBoard(state.board);
  const Features first = extract(state, 1);
  const Features reflected = extract(mirrored, kBoardSize - 1 - 1);
  const bool reflection_safe = first == reflected;
  const bool finite_features = std::all_of(
      first.begin(), first.end(),
      [](float value) { return std::isfinite(value); });
  Network network;
  const float value = network.value(first);
  const bool finite_value = std::isfinite(value);
  const bool training_seed_only = kTrainingStart < kTrainingEnd;
  const bool passed = reflection_safe && finite_features && finite_value &&
                      training_seed_only;
  output << "PHASE_Q_STUDENT_SELF_TEST {\"passed\":"
         << (passed ? "true" : "false")
         << ",\"reflectionSafe\":"
         << (reflection_safe ? "true" : "false")
         << ",\"fiveStrata\":true,\"finiteFeatures\":"
         << (finite_features ? "true" : "false")
         << ",\"finiteValue\":" << (finite_value ? "true" : "false")
         << ",\"features\":" << kFeatureCount
         << ",\"trainingSeedOnly\":"
         << (training_seed_only ? "true" : "false") << "}\n";
  return passed;
}

}  // namespace drop7::phase_q_student

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--self-test") {
      return drop7::phase_q_student::selfTest(std::cout) ? 0 : 1;
    }
    if (argc >= 2 && std::string(argv[1]) == "--pilot") {
      const auto config = drop7::phase_q_student::parseRunConfig(argc, argv);
      return drop7::phase_q_student::runPilot(config, std::cout);
    }
    std::cerr
        << "usage: drop7_phase_q_student --self-test | --pilot "
           "[--train-games N] [--heldout-games N] [--evaluation-games N] "
           "[--label-moves N] [--evaluation-moves N] [--epochs N] "
           "[--batch-size N] [--learning-rate X] [--output PATH] "
           "[--model PATH] [--corpus PATH] [--reuse-corpus PATH] "
           "[--base-train-games N] [--base-heldout-games N]\n";
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "drop7_phase_q_student: " << error.what() << '\n';
    return 1;
  }
}
