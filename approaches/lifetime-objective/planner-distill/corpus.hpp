#pragma once

// Record layout for the fair-planner teacher corpus.
//
// WHY THIS FILE EXISTS
// --------------------
// `docs/exploratory/audit-05-optimistic-curriculum.md` classifies seventeen
// failed learned models in this repository.  The single largest primary failure
// class is (iii) *sibling coverage / within-root discrimination*: six of the
// seventeen.  Every one of those experiments labelled the played action, or too
// few actions, or labelled siblings with too much independent noise to separate
// them at one root.
//
// This corpus is built so that class cannot recur.  At every decision the
// teacher records the exact value it computed for **every legal column**, under
// **common random numbers** (all columns share the same K sampled completions of
// the hidden board inside one decision), together with the realised afterstate
// of every legal column under the true environment.  Nothing is left to a
// separate roll-out.
//
// The teacher is `approaches/lifetime-objective/flow-ceiling/fair-planner.hpp`
// arm B: legal by construction.  `docs/exploratory/finding-07-fair-planning-
// ceiling.md` proves the information boundary with a self-test that swaps every
// hidden value and the entire future and requires an identical column.  This is
// the first distillation target in the repository whose advantage is, by
// construction, representable from public state.
//
// One record per decision.  Fixed width, packed, little-endian; the reader in
// `dataset.py` asserts the size.

#include <cstdint>

namespace drop7::distill {

constexpr int kCells = 49;
constexpr int kColumns = 7;

#pragma pack(push, 1)
struct RootRecord {
  // ---- the public state the teacher decided from -------------------------
  std::uint8_t board[kCells];      // 0 empty, 1..7 numbered, 8 solid, 9 cracked
  std::uint8_t next_disc;          // 1..7, visible
  std::uint8_t moves_remaining;    // 1..5 until the next rise
  std::uint8_t legal_mask;         // bit c set when column c is legal
  std::uint8_t occupied;           // occupied cells before the move, 0..49

  // ---- what the teacher did ----------------------------------------------
  std::uint8_t chosen_column;      // the planner's argmax
  std::uint8_t played_column;      // what was actually played (differs iff explored)
  std::uint8_t explored;           // 1 when an epsilon deviation was played
  std::uint8_t samples_used;       // K completions that solved inside the budget
  std::uint8_t incomplete;         // completions that exceeded the window budget

  // ---- the label: EVERY legal sibling, common random numbers --------------
  // `value[c]`  mean over the K completions of the exact horizon-H window
  //             optimum that starts by playing column c.  Objective: numbered
  //             discs cleared.  Illegal columns hold -1.
  // `immediate[c]` mean over the same K completions of the discs cleared by the
  //             move itself.  `value - immediate` is the mean value of the
  //             afterstate, which is the quantity a state-only afterstate
  //             evaluator can represent.
  float value[kColumns];
  float immediate[kColumns];

  // The same value, computed from the first and second half of the K
  // completions separately.  `docs/benchmarks.md` requires "action stability
  // across independent scenario halves" from a learned ranker; the same
  // statistic on the TEACHER is the ceiling any student can reach, because a
  // student cannot be more consistent than the label it is fitted to.  Nothing
  // in this repository has ever reported that ceiling.
  float value_lo[kColumns];
  float value_hi[kColumns];

  // ---- the realised afterstate of every legal column ----------------------
  // Resolved against the TRUE master tape, i.e. the same environment the game
  // continues into, so siblings are compared under common random numbers in the
  // environment as well as inside the planner.  These boards are public: a
  // revealed cover is a visible number.
  std::uint8_t after_board[kColumns][kCells];
  std::uint8_t after_survived[kColumns];
  std::uint8_t after_clears[kColumns];
  std::uint8_t after_reveals[kColumns];
  std::uint8_t after_occupied[kColumns];
  std::uint8_t after_next_disc[kColumns];     // the tape's next visible disc
  std::uint8_t after_moves_remaining[kColumns];

  // ---- provenance --------------------------------------------------------
  std::uint16_t move_index;        // 0-based within the game
  std::uint16_t moves_to_end;      // moves this game still had left (>=1)
  std::uint8_t censored_game;      // 1 when the source game hit the move cap
  std::uint8_t horizon;
  std::uint16_t samples_configured;
  std::uint32_t game_seed;
  std::uint8_t padding[9];         // keeps the record a round 576 bytes
};
#pragma pack(pop)

static_assert(sizeof(RootRecord) == 576, "RootRecord must stay 576 bytes");

}  // namespace drop7::distill
