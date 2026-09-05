# Rust search work avoidance

This is a schematic of the current Rust source's control flow. Box sizes and
arrow lengths have no numeric meaning. It contains no board position, game
value, or timing measurement.

- `approaches/fair-expectimax/rust-engine/src/board.rs`, `Board::to_bytes`:
  BMI2 uses PDEP; the portable path extracts each nibble with a direct shift.
  Both return a row-major byte array for the fair leaf.
- `approaches/fair-expectimax/rust-engine/src/leaf.rs`, `leaf_features`:
  horizontal and vertical release-support gathering each test positive excess
  first. An excess at or below zero gives zero release readiness.
- `approaches/fair-expectimax/rust-engine/src/search.rs`, `best_future_value`:
  `accepts_depth` precedes `PackedKey::new`, `hash_key`, and table lookup.
- `approaches/fair-expectimax/rust-engine/src/shared_table.rs`,
  `SharedTable::lookup`: the worker tries the stripe lock once, verifies the full
  packed key under the lock, and returns a completed value or a miss.

The entry threshold does not change search depth. A cache miss, a busy shared
stripe, and a depth rejected by the cache all continue the normal search. The
shared cache's storage is discarded after the decision.

The changes belong to
[EX-20260905-rust-bitboard-improvements-check-c68c07b7](../../../../research/experiments/EX-20260905-rust-bitboard-improvements-check-c68c07b7.json).
The diagram describes the mechanism; the result record owns any measured
performance claim.
