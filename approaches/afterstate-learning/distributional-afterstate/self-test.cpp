// Seed-free CHECK-tier tests for the afterstate corpus pipeline.
// Covers mechanics, legality, determinism, reflection, information boundary,
// and resource bounds. Uses only synthetic states; reads no game seeds.

#include <cmath>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "../../../src/core/native/engine.hpp"
#include "../../../src/core/native/public-behavior.hpp"
#include "common.hpp"

namespace afterstate = drop7::afterstate;
using drop7::Board;
using drop7::State;

namespace {

int failures = 0;

void check(bool condition, const std::string& name) {
  if (condition) {
    std::cout << "PASS " << name << "\n";
  } else {
    std::cout << "FAIL " << name << "\n";
    ++failures;
  }
}

// A deterministic synthetic mid-game public state with covered discs, cracked
// discs, and at least three legal columns.
State syntheticState(int variant) {
  State state;
  state.board = drop7::initialBoard();
  state.board[drop7::indexOf(5, 0)] = 3;
  state.board[drop7::indexOf(5, 1)] = 5;
  state.board[drop7::indexOf(5, 4)] = 4;
  state.board[drop7::indexOf(4, 1)] = static_cast<std::uint8_t>(1 + variant % 7);
  state.board[drop7::indexOf(4, 4)] = drop7::kCracked;
  state.board[drop7::indexOf(3, 4)] = drop7::kSolid;
  state.next_disc = static_cast<std::uint8_t>(1 + (variant * 3) % 7);
  state.moves_remaining = 1 + (variant % drop7::kMovesPerLevel);
  state.score = 12'345 + variant;      // privileged at deployment
  state.level = 2 + variant;           // privileged at deployment
  state.moves_played = 17 + variant;   // privileged at deployment
  return state;
}

afterstate::RootRecord syntheticRoot(int variant) {
  afterstate::RootRecord root;
  const State state = syntheticState(variant);
  if (!afterstate::makeRoot(state, 0u, root)) {
    throw std::runtime_error("synthetic state produced no root");
  }
  return root;
}

void testLegality() {
  const afterstate::RootRecord root = syntheticRoot(0);
  const auto labels = afterstate::labelRoot(root);
  check(labels.size() == root.legal_actions.size() * afterstate::kScenarios,
        "legality: every legal sibling has every scenario label");
  std::set<int> seen;
  for (const auto& label : labels) {
    seen.insert(label.action);
    check(drop7::isLegal(root.board, label.action),
          "legality: labeled action is legal on the root board");
  }
  check(seen == std::set<int>(root.legal_actions.begin(),
                              root.legal_actions.end()),
        "legality: exactly the legal action set is labeled");
}

void testDeterminism() {
  const afterstate::RootRecord root = syntheticRoot(1);
  const auto first = afterstate::labelRoot(root);
  const auto second = afterstate::labelRoot(root);
  bool identical = first.size() == second.size();
  for (std::size_t i = 0; identical && i < first.size(); ++i) {
    identical = first[i].score_gained == second[i].score_gained &&
                first[i].moves_survived == second[i].moves_survived &&
                first[i].afterstate == second[i].afterstate &&
                first[i].afterstate_next_disc == second[i].afterstate_next_disc &&
                first[i].terminal == second[i].terminal &&
                first[i].clears == second[i].clears &&
                first[i].reveals == second[i].reveals &&
                first[i].max_chain == second[i].max_chain;
  }
  check(identical, "determinism: repeated labeling is byte-identical");

  const auto d2_first = afterstate::labelRoot(root, afterstate::kScenarios,
                                              afterstate::Continuation::kD2);
  const auto d2_second = afterstate::labelRoot(root, afterstate::kScenarios,
                                               afterstate::Continuation::kD2);
  bool d2_identical = d2_first.size() == d2_second.size();
  for (std::size_t i = 0; d2_identical && i < d2_first.size(); ++i) {
    d2_identical = d2_first[i].score_gained == d2_second[i].score_gained &&
                   d2_first[i].afterstate == d2_second[i].afterstate;
  }
  check(d2_identical, "determinism: D2-continuation labeling is byte-identical");
}

void testReflection() {
  State mirrored_state = syntheticState(2);
  mirrored_state.board = drop7::cfpi::detail::mirrorBoard(mirrored_state.board);
  afterstate::RootRecord mirrored_root;
  if (!afterstate::makeRoot(mirrored_state, 0u, mirrored_root)) {
    check(false, "reflection: mirrored root exists");
    return;
  }
  const afterstate::RootRecord root = syntheticRoot(2);
  check(root.board == mirrored_root.board &&
            root.next_disc == mirrored_root.next_disc &&
            root.moves_remaining == mirrored_root.moves_remaining &&
            root.legal_actions == mirrored_root.legal_actions,
        "reflection: mirrored states canonicalize to the same root");
  const auto plain = afterstate::labelRoot(root);
  const auto mirrored = afterstate::labelRoot(mirrored_root);
  bool identical = plain.size() == mirrored.size();
  for (std::size_t i = 0; identical && i < plain.size(); ++i) {
    identical = plain[i].action == mirrored[i].action &&
                plain[i].score_gained == mirrored[i].score_gained &&
                plain[i].afterstate == mirrored[i].afterstate;
  }
  check(identical, "reflection: mirrored roots produce identical labels");
}

void testInformationBoundary() {
  // Two states sharing the public triple but differing in score, level, and
  // moves played must produce the same root identity and the same labels.
  State a = syntheticState(3);
  State b = a;
  b.score = 999'999;
  b.level = 40;
  b.moves_played = 1'900;
  afterstate::RootRecord ra;
  afterstate::RootRecord rb;
  if (!afterstate::makeRoot(a, 0u, ra) || !afterstate::makeRoot(b, 0u, rb)) {
    check(false, "information-boundary: roots exist");
    return;
  }
  check(ra.board == rb.board && ra.next_disc == rb.next_disc &&
            ra.moves_remaining == rb.moves_remaining &&
            ra.legal_actions == rb.legal_actions,
        "information-boundary: privileged fields do not change the root");
  check(afterstate::publicStateHash(ra.board, ra.next_disc,
                                    ra.moves_remaining) ==
            afterstate::publicStateHash(rb.board, rb.next_disc,
                                        rb.moves_remaining),
        "information-boundary: scenario key depends only on public state");
  const auto la = afterstate::labelRoot(ra);
  const auto lb = afterstate::labelRoot(rb);
  bool identical = la.size() == lb.size();
  for (std::size_t i = 0; identical && i < la.size(); ++i) {
    identical = la[i].score_gained == lb[i].score_gained &&
                la[i].afterstate == lb[i].afterstate;
  }
  check(identical,
        "information-boundary: labels are identical across privileged fields");
}

void testMechanics() {
  const afterstate::RootRecord root = syntheticRoot(4);
  const auto labels = afterstate::labelRoot(root);
  bool sane = true;
  for (const auto& label : labels) {
    if (!(label.score_gained >= 0.0 && std::isfinite(label.score_gained))) sane = false;
    if (label.moves_survived < 0 || label.moves_survived > afterstate::kHorizon) sane = false;
    if (label.terminal && label.moves_survived > afterstate::kHorizon) sane = false;
    if (label.afterstate_next_disc < 1 || label.afterstate_next_disc > 7) sane = false;
    if (label.afterstate_moves_remaining < 1 ||
        label.afterstate_moves_remaining > drop7::kMovesPerLevel) sane = false;
    if (label.max_chain < 0 || label.max_chain > 64) sane = false;
  }
  check(sane, "mechanics: label fields are within engine bounds");
}

void testResourceBound() {
  // Worst-case labeling work is structurally bounded: 7 siblings x 8
  // scenarios x (1 + kHorizon) transitions, each with a bounded D1 evaluation.
  constexpr std::int64_t worst_transitions =
      drop7::kBoardSize * afterstate::kScenarios * (1 + afterstate::kHorizon);
  check(worst_transitions == 7 * 8 * 41,
        "resource-bound: per-root transition count is the frozen constant");
}

}  // namespace

int main() {
  testLegality();
  testDeterminism();
  testReflection();
  testInformationBoundary();
  testMechanics();
  testResourceBound();
  if (failures == 0) {
    std::cout << "SELFTEST OK\n";
    return 0;
  }
  std::cout << "SELFTEST FAILURES " << failures << "\n";
  return 1;
}
