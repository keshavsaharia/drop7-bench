#pragma once
// Fast parameterized fair expectimax search.
//
// EQUIVALENCE CONTRACT.  The control flow, the accumulation order of every
// double, the chance stratification, the canonicalisation, the column order,
// the iterative-deepening fallback and the work accounting are copied from
// approaches/fair-expectimax/reference/fair-only-depth4.cpp.  Nothing about the
// value computation is changed.
//
// What changes is storage:
//
//   O1  the transposition table.  The reference keys an
//       std::unordered_map<std::string, CacheEntry> with a 52-byte string built
//       fresh at every interior node -- 52 bytes exceeds libstdc++'s
//       small-string capacity of 15, so that is one malloc and one free per
//       interior node -- and maintains LRU order in a parallel
//       std::list<std::string>, which is a second allocation plus a second
//       52-byte copy per insert.  Here the key is a 32-byte packed value
//       (49 cells x 4 bits + next disc + moves remaining + depth), the table is
//       open addressed with linear probing and backward-shift deletion, and the
//       LRU order is an intrusive doubly linked list over slot indices.  Hit,
//       miss and eviction behaviour are identical: the packing is injective on
//       the reachable domain, so two states share a key here exactly when they
//       share a string there, and eviction is still strict LRU at the same
//       capacity.  Work counts and completed depths are therefore unchanged.
//   O5  MoveResult::waves is a std::vector allocated per node for data the
//       search never reads.  The search passes MinimalWaveSink, which records
//       only the two facts playMove itself consults.
//
// gate-search proves action, work and completed-depth identity against the
// frozen reference and against a same-storage slow parameterized search.

#include "fast-engine.hpp"
#include "fast-leaf.hpp"

#include <cstdint>
#include <exception>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <vector>

namespace drop7::fast {

// ---------------------------------------------------------------------------
// Packed transposition key
// ---------------------------------------------------------------------------

struct PackedKey {
  std::uint64_t words[4] = {0, 0, 0, 0};

  bool operator==(const PackedKey& other) const {
    return words[0] == other.words[0] && words[1] == other.words[1] &&
           words[2] == other.words[2] && words[3] == other.words[3];
  }
};

// Injective on the reachable domain: cells are 0..9 (4 bits), next_disc 1..7,
// moves_remaining 1..5, depth 1..8.  gate-search asserts the domain bounds on
// every key it builds.
inline PackedKey packKey(const State& state, int depth) {
  PackedKey key;
  const std::uint8_t* cells = state.board.data();
  for (int group = 0; group < 3; ++group) {
    std::uint64_t word = 0;
    for (int offset = 0; offset < 16; ++offset) {
      word |= static_cast<std::uint64_t>(cells[group * 16 + offset] & 0x0fu)
              << (4 * offset);
    }
    key.words[group] = word;
  }
  key.words[3] = static_cast<std::uint64_t>(cells[48] & 0x0fu) |
                 (static_cast<std::uint64_t>(state.next_disc) << 8) |
                 (static_cast<std::uint64_t>(
                      static_cast<std::uint32_t>(state.moves_remaining))
                  << 16) |
                 (static_cast<std::uint64_t>(static_cast<std::uint32_t>(depth))
                  << 24);
  return key;
}

inline std::uint64_t mixKey(std::uint64_t value) {
  value ^= value >> 33;
  value *= 0xff51'afd7'ed55'8ccdull;
  value ^= value >> 33;
  value *= 0xc4ce'b9fe'1a85'ec53ull;
  value ^= value >> 33;
  return value;
}

inline std::uint64_t hashKey(const PackedKey& key) {
  std::uint64_t hash = key.words[0];
  hash = mixKey(hash ^ (key.words[1] + 0x9e37'79b9'7f4a'7c15ull));
  hash = mixKey(hash ^ (key.words[2] + 0xbf58'476d'1ce4'e5b9ull));
  hash = mixKey(hash ^ (key.words[3] + 0x94d0'49bb'1331'11ebull));
  return hash;
}

// ---------------------------------------------------------------------------
// Open-addressed LRU transposition table
// ---------------------------------------------------------------------------

class TranspositionTable {
 public:
  static constexpr std::int32_t kNone = -1;

  explicit TranspositionTable(std::size_t capacity) { allocate(capacity); }

  void allocate(std::size_t capacity) {
    capacity_ = capacity;
    std::size_t slots = 8;
    while (slots < capacity * 2) slots <<= 1;
    mask_ = slots - 1;
    entries_.assign(capacity, Entry{});
    index_.assign(slots, kNone);
    stamp_.assign(slots, 0);
    recycled_.clear();
    recycled_.reserve(capacity);
    epoch_ = 1;
    next_id_ = 0;
    head_ = kNone;
    tail_ = kNone;
    size_ = 0;
  }

  // O(1).  A decision starts with an empty cache in the reference because it
  // constructs a fresh SearchContext; bumping the epoch makes every slot read
  // as empty without touching the arrays.
  void clear() {
    ++epoch_;
    if (epoch_ == 0) {  // wraparound; cannot happen in a bounded run
      std::fill(stamp_.begin(), stamp_.end(), 0u);
      epoch_ = 1;
    }
    recycled_.clear();
    next_id_ = 0;
    head_ = kNone;
    tail_ = kNone;
    size_ = 0;
  }

  std::size_t size() const { return size_; }
  std::size_t capacity() const { return capacity_; }
  std::size_t slotBytes() const {
    return entries_.capacity() * sizeof(Entry) +
           index_.capacity() * sizeof(std::int32_t) +
           stamp_.capacity() * sizeof(std::uint32_t) +
           recycled_.capacity() * sizeof(std::int32_t);
  }

  // Returns the cached value on a hit and marks the entry most recently used,
  // mirroring the reference's order.splice(order.end(), ...).
  const double* lookup(const PackedKey& key, std::uint64_t hash) {
    const std::size_t slot = findSlot(key, hash);
    if (slot == kNoSlot) return nullptr;
    const std::int32_t id = index_[slot];
    moveToBack(id);
    return &entries_[static_cast<std::size_t>(id)].value;
  }

  // Mirrors cacheValue: erase any prior entry for the key, evict from the front
  // while at capacity, then append at the back.
  void store(const PackedKey& key, std::uint64_t hash, double value) {
    const std::size_t prior = findSlot(key, hash);
    if (prior != kNoSlot) {
      const std::int32_t id = index_[prior];
      unlink(id);
      eraseSlot(prior);
      recycled_.push_back(id);
      --size_;
    }
    while (size_ >= capacity_) {
      const std::int32_t oldest = head_;
      const Entry& entry = entries_[static_cast<std::size_t>(oldest)];
      const std::size_t oldest_slot = findSlot(entry.key, entry.hash);
      unlink(oldest);
      eraseSlot(oldest_slot);
      recycled_.push_back(oldest);
      --size_;
    }
    std::int32_t id;
    if (!recycled_.empty()) {
      id = recycled_.back();
      recycled_.pop_back();
    } else {
      id = next_id_++;
    }
    Entry& entry = entries_[static_cast<std::size_t>(id)];
    entry.key = key;
    entry.hash = hash;
    entry.value = value;
    entry.previous = kNone;
    entry.next = kNone;
    std::size_t slot = hash & mask_;
    while (occupied(slot)) slot = (slot + 1) & mask_;
    index_[slot] = id;
    stamp_[slot] = epoch_;
    pushBack(id);
    ++size_;
  }

 private:
  struct Entry {
    PackedKey key;
    double value = 0.0;
    std::uint64_t hash = 0;
    std::int32_t previous = kNone;
    std::int32_t next = kNone;
  };

  static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

  bool occupied(std::size_t slot) const { return stamp_[slot] == epoch_; }
  void vacate(std::size_t slot) { stamp_[slot] = 0; }

  std::size_t findSlot(const PackedKey& key, std::uint64_t hash) const {
    std::size_t slot = hash & mask_;
    while (occupied(slot)) {
      const Entry& entry = entries_[static_cast<std::size_t>(index_[slot])];
      if (entry.hash == hash && entry.key == key) return slot;
      slot = (slot + 1) & mask_;
    }
    return kNoSlot;
  }

  // Backward-shift deletion keeps every probe sequence intact without
  // tombstones, so probe lengths stay bounded under heavy eviction.
  void eraseSlot(std::size_t hole) {
    vacate(hole);
    std::size_t scan = (hole + 1) & mask_;
    while (occupied(scan)) {
      const std::size_t home =
          entries_[static_cast<std::size_t>(index_[scan])].hash & mask_;
      const bool inside_run = hole <= scan ? (home > hole && home <= scan)
                                           : (home > hole || home <= scan);
      if (!inside_run) {
        index_[hole] = index_[scan];
        stamp_[hole] = epoch_;
        vacate(scan);
        hole = scan;
      }
      scan = (scan + 1) & mask_;
    }
  }

  void unlink(std::int32_t id) {
    Entry& entry = entries_[static_cast<std::size_t>(id)];
    if (entry.previous != kNone) {
      entries_[static_cast<std::size_t>(entry.previous)].next = entry.next;
    } else {
      head_ = entry.next;
    }
    if (entry.next != kNone) {
      entries_[static_cast<std::size_t>(entry.next)].previous = entry.previous;
    } else {
      tail_ = entry.previous;
    }
    entry.previous = kNone;
    entry.next = kNone;
  }

  void pushBack(std::int32_t id) {
    Entry& entry = entries_[static_cast<std::size_t>(id)];
    entry.previous = tail_;
    entry.next = kNone;
    if (tail_ != kNone) {
      entries_[static_cast<std::size_t>(tail_)].next = id;
    } else {
      head_ = id;
    }
    tail_ = id;
  }

  void moveToBack(std::int32_t id) {
    if (tail_ == id) return;
    unlink(id);
    pushBack(id);
  }

  std::vector<Entry> entries_;
  std::vector<std::int32_t> index_;
  std::vector<std::uint32_t> stamp_;
  std::vector<std::int32_t> recycled_;
  std::size_t mask_ = 0;
  std::size_t capacity_ = 0;
  std::size_t size_ = 0;
  std::uint32_t epoch_ = 1;
  std::int32_t next_id_ = 0;
  std::int32_t head_ = kNone;
  std::int32_t tail_ = kNone;
};

// ---------------------------------------------------------------------------
// Chance stratification, bit-identical to cfpi::detail
// ---------------------------------------------------------------------------

struct FastStratifiedRandom {
  std::uint32_t seed = 0;
  int sample = 0;
  int count = 1;
  int event = 0;

  std::uint8_t nextDisc() {
    const double unit = cfpi::detail::stratifiedUnit(
        seed, sample, count, cfpi::detail::kRevealSampleDomain, event++);
    // unit is in [0,1) by construction, so truncation and std::floor agree
    // exactly and the resulting integer is identical.
    return static_cast<std::uint8_t>(
        static_cast<int>(unit * static_cast<double>(kBoardSize)) + 1);
  }
};

inline std::uint8_t fastSampledNextDisc(std::uint32_t seed, int sample,
                                        int count) {
  const double unit = cfpi::detail::stratifiedUnit(
      seed, sample, count, cfpi::detail::kDiscSampleDomain, 0);
  return static_cast<std::uint8_t>(
      static_cast<int>(unit * static_cast<double>(kBoardSize)) + 1);
}

// ---------------------------------------------------------------------------
// The search
// ---------------------------------------------------------------------------

struct FastSearchParameters {
  int depth = 4;
  int chance_samples = 5;
  double terminal_utility = -1'000'000.0;
  std::uint64_t maximum_work = 3'200'000;
  std::size_t maximum_cache_entries = 60'000;
  std::uint32_t policy_seed = 0xd707'5eedu;
};

struct FastSearchMetrics {
  int action = -1;
  int completed_depth = 0;
  std::uint64_t nodes = 0;
  std::uint64_t work = 0;
  std::uint64_t cache_hits = 0;
  std::size_t cache_entries = 0;
};

class FastWorkLimitReached : public std::exception {};

class FastSearch {
 public:
  explicit FastSearch(FastSearchParameters parameters)
      : parameters_(parameters), table_(parameters.maximum_cache_entries) {}

  int chooseAction(const State& source, FastSearchMetrics& metrics) {
    metrics = FastSearchMetrics{};
    if (source.game_over) return -1;
    bool mirrored = false;
    const State canonical = canonicalStateFast(source, mirrored);
    table_.clear();
    nodes_ = 0;
    work_ = 0;
    cache_hits_ = 0;
    int action = -1;
    int completed_depth = 0;
    for (int depth = 1; depth <= parameters_.depth; ++depth) {
      try {
        const int candidate = rootDecision(canonical, depth);
        if (candidate < 0) break;
        action = candidate;
        completed_depth = depth;
      } catch (const FastWorkLimitReached&) {
        break;
      }
    }
    if (action < 0) action = centerFirstMove(canonical.board);
    metrics.completed_depth = completed_depth;
    metrics.nodes = nodes_;
    metrics.work = work_;
    metrics.cache_hits = cache_hits_;
    metrics.cache_entries = table_.size();
    metrics.action = mirrored && action >= 0 ? kBoardSize - 1 - action : action;
    return metrics.action;
  }

  int chooseAction(const State& source, std::uint64_t& work) {
    FastSearchMetrics metrics;
    const int action = chooseAction(source, metrics);
    work += metrics.work;
    return action;
  }

  std::size_t tableBytes() const { return table_.slotBytes(); }

  int requestedDepth() const { return parameters_.depth; }

 private:
  void checkBudget() const {
    if (work_ >= parameters_.maximum_work) throw FastWorkLimitReached{};
  }

  double evaluateAction(const State& state, int column, int depth) {
    const std::uint32_t state_seed = cfpi::detail::scenarioSeedForState(
        state, parameters_.policy_seed, depth);
    double value = 0.0;
    for (int sample = 0; sample < parameters_.chance_samples; ++sample) {
      checkBudget();
      FastStratifiedRandom random{state_seed, sample,
                                  parameters_.chance_samples, 0};
      MinimalWaveSink sink;
      FastMoveResult move;
      const bool played = playMoveFast(state, column, random, sink, move);
      ++work_;
      if (!played) {
        value += parameters_.terminal_utility;
        continue;
      }
      const double score_delta = static_cast<double>(move.score_delta);
      if (move.state.game_over) {
        value += score_delta + parameters_.terminal_utility;
        continue;
      }
      move.state.score = 0;
      move.state.next_disc = fastSampledNextDisc(state_seed, sample,
                                                 parameters_.chance_samples);
      bool ignored = false;
      const State next = canonicalStateFast(move.state, ignored);
      value += score_delta + bestFutureValue(next, depth - 1);
    }
    return value / parameters_.chance_samples;
  }

  double evaluateLeaf(const State& state) {
    checkBudget();
    ++work_;
    const double value = fastFairLeaf(state, scratch_);
    if (!std::isfinite(value)) {
      throw std::runtime_error("fast leaf returned a non-finite value");
    }
    return value;
  }

  double bestFutureValue(const State& state, int depth) {
    ++nodes_;
    checkBudget();
    if (state.game_over) return parameters_.terminal_utility;
    if (depth == 0) return evaluateLeaf(state);
    const PackedKey key = packKey(state, depth);
    const std::uint64_t hash = hashKey(key);
    if (const double* cached = table_.lookup(key, hash)) {
      ++cache_hits_;
      return *cached;
    }
    double best = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (state.board[static_cast<std::size_t>(column)] != kEmpty) continue;
      const double value = evaluateAction(state, column, depth);
      if (value > best) best = value;
    }
    if (!std::isfinite(best)) best = parameters_.terminal_utility;
    table_.store(key, hash, best);
    return best;
  }

  int rootDecision(const State& canonical, int depth) {
    int action = -1;
    double best_value = -std::numeric_limits<double>::infinity();
    for (const int column : cfpi::detail::kColumnOrder) {
      if (canonical.board[static_cast<std::size_t>(column)] != kEmpty) continue;
      const double value = evaluateAction(canonical, column, depth);
      if (value > best_value) {
        best_value = value;
        action = column;
      }
    }
    return action;
  }

  FastSearchParameters parameters_;
  TranspositionTable table_;
  LeafScratch scratch_{};
  std::uint64_t nodes_ = 0;
  std::uint64_t work_ = 0;
  std::uint64_t cache_hits_ = 0;
};

}  // namespace drop7::fast
