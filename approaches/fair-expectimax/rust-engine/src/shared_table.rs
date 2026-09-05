//! Decision-scoped shared memoization. Full keys and completed values are read
//! and published under the same lock. Contention is an ordinary miss/skip;
//! workers never wait for another subtree or trust a partial hash signature.
//!
//! Storage and handles are crate-private so the parallel scheduler owns their
//! entire lifetime. Every decision allocates fresh storage; all workers use
//! identical search parameters and the same deterministic leaf factory.

use crate::search::{PackedKey, TranspositionTable};
use std::sync::Mutex;

const MAX_STRIPES: usize = 256;
// On the measured Rust 1.96 macOS runtime each std Mutex lazily allocates a
// 64-byte pthread mutex. Preinitializing each stripe removes the allocation
// race; 128 bytes per lock is a conservative runtime allowance. Linux's futex
// mutex is inline. As with private tables, allocator metadata is excluded.
const LOCK_HEAP_ALLOWANCE: usize = 128;

pub(crate) struct SharedStorage {
    stripes: Box<[Mutex<Box<[Slot]>>]>,
    mask: usize,
    stripe_mask: usize,
    stripe_shift: u32,
    from_depth: i32,
}

#[derive(Clone, Copy)]
struct Slot {
    key: PackedKey,
    value: f64,
    // Zero means vacant. Search caches only positive remaining depths.
    depth: i32,
}

impl SharedStorage {
    /// Key/value/depth entry payload, excluding stripe and runtime lock costs.
    pub(crate) fn entry_bytes(capacity: usize) -> Option<usize> {
        capacity
            .max(1)
            .checked_next_power_of_two()?
            .checked_mul(std::mem::size_of::<Slot>())
    }

    /// Conservative heap projection: slot payload, stripe objects and a
    /// per-lock runtime allocation allowance. Counts shared storage once;
    /// allocator metadata and thread stacks are outside the table estimate.
    pub(crate) fn projected_bytes(capacity: usize) -> Option<usize> {
        let capacity = capacity.max(1).checked_next_power_of_two()?;
        let stripes = capacity.min(MAX_STRIPES);
        Self::entry_bytes(capacity)?.checked_add(
            stripes.checked_mul(std::mem::size_of::<Mutex<Box<[Slot]>>>() + LOCK_HEAP_ALLOWANCE)?,
        )
    }

    pub(crate) fn new(capacity: usize, from_depth: i32) -> Self {
        let capacity = capacity
            .max(1)
            .checked_next_power_of_two()
            .expect("shared transposition-table capacity is too large");
        let stripe_count = capacity.min(MAX_STRIPES);
        let stripes = (0..stripe_count)
            .map(|_| {
                let slots = vec![
                    Slot {
                        key: PackedKey::default(),
                        value: 0.0,
                        depth: 0
                    };
                    capacity / stripe_count
                ]
                .into_boxed_slice();
                let stripe = Mutex::new(slots);
                // Some standard-library mutexes allocate on first use. Initialize
                // all locks here, before workers start; no racing temporary locks.
                drop(stripe.lock().expect("new shared cache stripe"));
                stripe
            })
            .collect::<Vec<_>>()
            .into_boxed_slice();
        Self {
            stripes,
            mask: capacity - 1,
            stripe_mask: stripe_count - 1,
            stripe_shift: stripe_count.trailing_zeros(),
            from_depth: from_depth.max(1),
        }
    }

    pub(crate) fn worker(&self) -> SharedTable<'_> {
        SharedTable {
            storage: self,
            hits: 0,
        }
    }
}

/// Only used inside a single scheduler decision. `clear` resets this worker's
/// counters; storage invalidation is the scheduler's fresh allocation, which
/// avoids both epoch-wrap reuse and entries surviving a parameter/leaf change.
pub(crate) struct SharedTable<'a> {
    storage: &'a SharedStorage,
    hits: u64,
}

impl TranspositionTable for SharedTable<'_> {
    #[inline]
    fn accepts_depth(&self, depth: i32) -> bool {
        depth >= self.storage.from_depth
    }

    #[inline]
    fn lookup(&mut self, key: &PackedKey, hash: u64, depth: i32) -> Option<f64> {
        if !self.accepts_depth(depth) {
            return None;
        }
        let index = hash as usize & self.storage.mask;
        let stripe = self.storage.stripes[index & self.storage.stripe_mask]
            .try_lock()
            .ok()?;
        let slot = &stripe[index >> self.storage.stripe_shift];
        if slot.depth > 0 && slot.key == *key {
            self.hits += 1;
            Some(slot.value)
        } else {
            None
        }
    }

    #[inline]
    fn store(&mut self, key: &PackedKey, hash: u64, depth: i32, value: f64) {
        if !self.accepts_depth(depth) {
            return;
        }
        let index = hash as usize & self.storage.mask;
        if let Ok(mut stripe) = self.storage.stripes[index & self.storage.stripe_mask].try_lock() {
            let slot = &mut stripe[index >> self.storage.stripe_shift];
            if depth >= slot.depth {
                *slot = Slot {
                    key: *key,
                    value,
                    depth,
                };
            }
        }
    }

    fn clear(&mut self) {
        self.hits = 0;
    }
    fn bytes(&self) -> usize {
        0
    } // no heap storage owned by this worker
    fn hits(&self) -> u64 {
        self.hits
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Board, State};

    fn key(disc: u8, depth: i32) -> PackedKey {
        PackedKey::new(
            &State {
                board: Board::empty(),
                next_disc: disc,
                moves_remaining: 3,
                score: 0,
                level: 1,
                moves_played: 0,
                game_over: false,
            },
            depth,
        )
    }

    #[test]
    fn collisions_compare_the_entire_key_and_prefer_deeper_entries() {
        let storage = SharedStorage::new(1, 2);
        let mut worker = storage.worker();
        let a = key(1, 3);
        let b = key(2, 2);
        worker.store(&a, 0, 3, -0.0);
        assert_eq!(
            worker.lookup(&a, 0, 3).unwrap().to_bits(),
            (-0.0f64).to_bits()
        );
        assert_eq!(worker.lookup(&b, 0, 2), None);
        worker.store(&b, 0, 2, 9.0);
        assert_eq!(worker.lookup(&b, 0, 2), None);
        let replacement = key(2, 3);
        worker.store(&replacement, 0, 3, 10.0);
        assert_eq!(worker.lookup(&a, 0, 3), None);
        assert_eq!(worker.lookup(&replacement, 0, 3), Some(10.0));
        assert!(!worker.accepts_depth(1));
        assert_eq!(worker.lookup(&b, 0, 1), None);
    }

    #[test]
    fn a_busy_slot_skips_without_blocking_or_publishing() {
        let storage = SharedStorage::new(1, 1);
        let mut worker = storage.worker();
        let key = key(1, 1);
        let lock = storage.stripes[0].lock().unwrap();
        assert_eq!(worker.lookup(&key, 0, 1), None);
        worker.store(&key, 0, 1, 4.0);
        drop(lock);
        assert_eq!(worker.lookup(&key, 0, 1), None);
    }

    #[test]
    fn concurrent_collisions_never_mix_key_and_value_bits() {
        let storage = SharedStorage::new(1, 1);
        std::thread::scope(|scope| {
            for disc in 1..=4 {
                let storage = &storage;
                scope.spawn(move || {
                    let mut worker = storage.worker();
                    let key = key(disc, 2);
                    let value = f64::from_bits(0x3ff0_1234_5678_0000 + disc as u64);
                    for _ in 0..2000 {
                        worker.store(&key, 0, 2, value);
                        if let Some(found) = worker.lookup(&key, 0, 2) {
                            assert_eq!(found.to_bits(), value.to_bits());
                        }
                    }
                });
            }
        });
    }

    #[test]
    fn fresh_decisions_cannot_observe_old_entries() {
        let old = SharedStorage::new(3, 1);
        let fresh = SharedStorage::new(3, 1);
        let key = key(1, 2);
        old.worker().store(&key, 0, 2, 100.0);
        assert_eq!(fresh.worker().lookup(&key, 0, 2), None);
        assert_eq!(
            SharedStorage::projected_bytes(3),
            Some(
                4 * (std::mem::size_of::<Slot>()
                    + std::mem::size_of::<Mutex<Box<[Slot]>>>()
                    + LOCK_HEAP_ALLOWANCE)
            )
        );
        assert_eq!(SharedStorage::projected_bytes(usize::MAX), None);
    }

    #[test]
    fn striped_indexing_preserves_every_entry_and_bounds_lock_count() {
        let storage = SharedStorage::new(777, 1);
        assert_eq!(storage.stripes.len(), MAX_STRIPES);
        let mut worker = storage.worker();
        let key = key(1, 2);
        for index in 0..1024 {
            worker.store(&key, index, 2, index as f64);
        }
        for index in 0..1024 {
            assert_eq!(worker.lookup(&key, index, 2), Some(index as f64));
        }
        let lock = storage.stripes[0].lock().unwrap();
        assert_eq!(worker.lookup(&key, 256, 2), None);
        assert_eq!(worker.lookup(&key, 1, 2), Some(1.0));
        drop(lock);
        assert_eq!(worker.lookup(&key, 256, 2), Some(256.0));
    }
}
