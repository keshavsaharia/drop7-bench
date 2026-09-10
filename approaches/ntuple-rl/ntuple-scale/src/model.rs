// The lookup-table model: one learned number per (table, pattern), summed
// over the active patterns of a board, plus the temporal-coherence update.
//
// LAYOUT.  Families, each a run of tables of equal size:
//   rows   7 tables x 10^7   (row r of the canonical board)
//   cols   7 tables x 10^7   (column c of the canonical board)
//   win23  30 tables x 10^6  (2-wide x 3-tall windows, absolute placement)
//   win32  30 tables x 10^6  (3-wide x 2-tall windows, absolute placement)
//   win24  24 tables x 10^8  (2-wide x 4-tall windows, absolute placement)
//   win42  24 tables x 10^8  (4-wide x 2-tall windows, absolute placement)
// A family may be conditioned on the rise phase (moves remaining until the
// next rise, 1..=5), which multiplies its table size by five: phase=cols
// conditions the column tables, phase=all conditions rows, cols, win23 and
// win32.  The eight-cell windows are never phase-conditioned (their tables
// are a hundred times larger than the six-cell ones).  Every active pattern
// of a state is a distinct table entry by construction (one entry per table
// per state), so the semi-gradient multiplicity is always one.
//
// FILL CONDITIONING (the fill-conditioned leaf experiment).  Every family
// may additionally be conditioned on a global fill bucket of the board,
// which multiplies every table by five again: fill=occ5 buckets the number
// of occupied cells (0-13, 14-20, 21-27, 28-34, 35-49) and fill=hgt5 the
// tallest column (0-3, 4, 5, 6, 7).  A line or window pattern sees only its
// own cells; the bucket lets the same pattern carry a different number when
// the board around it is nearly empty or nearly full.  The bucket is a
// function of the public board alone.  Fill conditioning of the eight-cell
// windows is refused (five copies of a hundred-million-entry family do not
// fit beside the rest).  Within a table the slabs are ordered bucket-major,
// then phase, then pattern.
//
// PROMOTION.  `Model::promote` builds a fill-conditioned model from an
// unconditioned one of the same families by copying every table into every
// bucket, so the promoted model values every board exactly as its source
// did (bit-identical sums) until training moves the buckets apart.  The
// coherence accumulators start fresh (every entry's first update runs at
// full rate), which is the multi-stage weight promotion of the 2048 n-tuple
// literature applied to a bucket that is not monotone in time.
//
// CANONICALISATION.  The board is reflected left-right when its mirror is
// lexicographically smaller, exactly as the search canonicalises, so the same
// position and its mirror share every table entry.  The value is therefore
// reflection-invariant by construction.
//
// SHARED MEMORY.  Weights and the two temporal-coherence accumulators are
// AtomicU32-encoded f32 read and written with Relaxed ordering (plain loads
// and stores on x86-64).  Training threads update the same tables without
// locks (asynchronous, "Hogwild"): a lost update between two threads is
// tolerated.  Evaluation is a fixed-order sum over the active entries, so a
// frozen table always returns the same bits for the same state.

use std::io::{Read, Write};
use std::sync::atomic::{AtomicU32, Ordering::Relaxed};

use drop7_rs::board::{Board, BOARD_SIZE};

use crate::tuples::{
    chunk2, chunk3, chunk4, row_words, Codec, LINE_PATTERNS, WIDE_WINDOW_PATTERNS,
    WIN23_PLACEMENTS, WIN24_PLACEMENTS, WIN32_PLACEMENTS, WIN42_PLACEMENTS, WINDOW_PATTERNS,
};

/// Upper bound on active entries per state (7 + 7 + 30 + 30 + 24 + 24).
pub const MAX_ACTIVE: usize = 122;
/// Family slots, in feature order.
const FAMILIES: usize = 6;
const ROWS: usize = 0;
const COLS: usize = 1;
const WIN23: usize = 2;
const WIN32: usize = 3;
const WIN24: usize = 4;
const WIN42: usize = 5;
/// One row rise, in points: the value unit of the tables.
pub const VALUE_SCALE: f64 = 17_000.0;
pub const PHASES: usize = 5;
/// Fill buckets of a fill-conditioned family.
pub const FILL_BUCKETS: usize = 5;

const MAGIC: &[u8; 8] = b"D7NTUP01";

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum PhaseMode {
    None,
    Columns,
    All,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum FillMode {
    None,
    /// Occupied cells: 0-13, 14-20, 21-27, 28-34, 35-49.
    Occupancy5,
    /// Tallest column: 0-3, 4, 5, 6, 7.
    Height5,
}

impl FillMode {
    /// The bucket of a board (0..FILL_BUCKETS), or 0 for an unconditioned
    /// layout.  Reads only the board.
    #[inline]
    pub fn bucket(self, board: &Board) -> usize {
        match self {
            FillMode::None => 0,
            FillMode::Occupancy5 => {
                let occupied: usize = (0..BOARD_SIZE).map(|c| board.height(c)).sum();
                match occupied {
                    0..=13 => 0,
                    14..=20 => 1,
                    21..=27 => 2,
                    28..=34 => 3,
                    _ => 4,
                }
            }
            FillMode::Height5 => {
                let tallest = (0..BOARD_SIZE).map(|c| board.height(c)).max().unwrap_or(0);
                match tallest {
                    0..=3 => 0,
                    4 => 1,
                    5 => 2,
                    6 => 3,
                    _ => 4,
                }
            }
        }
    }

    pub fn buckets(self) -> usize {
        if self == FillMode::None {
            1
        } else {
            FILL_BUCKETS
        }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub struct Layout {
    pub rows: bool,
    pub cols: bool,
    pub win23: bool,
    pub win32: bool,
    pub win24: bool,
    pub win42: bool,
    pub phase: PhaseMode,
    pub fill: FillMode,
}

impl Layout {
    /// Parse "rows,cols,win23,win32,win24,win42,phase=cols,fill=occ5" (any
    /// subset of the families, phase in {none, cols, all}, fill in {none,
    /// occ5, hgt5}; both default none).
    pub fn parse(spec: &str) -> Result<Layout, String> {
        let mut layout = Layout {
            rows: false,
            cols: false,
            win23: false,
            win32: false,
            win24: false,
            win42: false,
            phase: PhaseMode::None,
            fill: FillMode::None,
        };
        for item in spec.split(',').map(str::trim).filter(|s| !s.is_empty()) {
            match item {
                "rows" => layout.rows = true,
                "cols" => layout.cols = true,
                "win23" => layout.win23 = true,
                "win32" => layout.win32 = true,
                "win24" => layout.win24 = true,
                "win42" => layout.win42 = true,
                "phase=none" => layout.phase = PhaseMode::None,
                "phase=cols" => layout.phase = PhaseMode::Columns,
                "phase=all" => layout.phase = PhaseMode::All,
                "fill=none" => layout.fill = FillMode::None,
                "fill=occ5" => layout.fill = FillMode::Occupancy5,
                "fill=hgt5" => layout.fill = FillMode::Height5,
                other => return Err(format!("unknown layout item {other:?}")),
            }
        }
        if !(layout.rows || layout.cols || layout.win23 || layout.win32 || layout.win24 || layout.win42) {
            return Err("layout names no tuple family".into());
        }
        if layout.fill != FillMode::None && (layout.win24 || layout.win42) {
            return Err("fill conditioning is not supported for the eight-cell windows (win24, win42)".into());
        }
        Ok(layout)
    }

    /// The same families and phase mode without fill conditioning: the
    /// layout a fill-conditioned model is promoted from.
    pub fn unconditioned(&self) -> Layout {
        Layout { fill: FillMode::None, ..*self }
    }

    pub fn spec(&self) -> String {
        let mut parts = Vec::new();
        if self.rows {
            parts.push("rows");
        }
        if self.cols {
            parts.push("cols");
        }
        if self.win23 {
            parts.push("win23");
        }
        if self.win32 {
            parts.push("win32");
        }
        if self.win24 {
            parts.push("win24");
        }
        if self.win42 {
            parts.push("win42");
        }
        parts.push(match self.phase {
            PhaseMode::None => "phase=none",
            PhaseMode::Columns => "phase=cols",
            PhaseMode::All => "phase=all",
        });
        // Emitted only when set, so the spec of an unconditioned layout is
        // byte-identical to the one the earlier table files carry.
        match self.fill {
            FillMode::None => {}
            FillMode::Occupancy5 => parts.push("fill=occ5"),
            FillMode::Height5 => parts.push("fill=hgt5"),
        }
        parts.join(",")
    }

    fn phases_for(&self, family: &str) -> usize {
        match (self.phase, family) {
            (PhaseMode::None, _) => 1,
            (PhaseMode::Columns, "cols") => PHASES,
            (PhaseMode::Columns, _) => 1,
            (PhaseMode::All, "win24" | "win42") => 1,
            (PhaseMode::All, _) => PHASES,
        }
    }

    /// Phase slabs of a family (1 or PHASES); public for the gate's
    /// independent reference.
    pub fn phase_slabs(&self, family: &str) -> usize {
        self.phases_for(family)
    }

    /// (family, tables, entries per table).  A table holds
    /// buckets x phase slabs x patterns entries, bucket-major.
    pub fn families(&self) -> Vec<(&'static str, usize, usize)> {
        let buckets = self.fill.buckets();
        let mut out = Vec::new();
        if self.rows {
            out.push(("rows", BOARD_SIZE, LINE_PATTERNS * self.phases_for("rows") * buckets));
        }
        if self.cols {
            out.push(("cols", BOARD_SIZE, LINE_PATTERNS * self.phases_for("cols") * buckets));
        }
        if self.win23 {
            out.push(("win23", WIN23_PLACEMENTS, WINDOW_PATTERNS * self.phases_for("win23") * buckets));
        }
        if self.win32 {
            out.push(("win32", WIN32_PLACEMENTS, WINDOW_PATTERNS * self.phases_for("win32") * buckets));
        }
        if self.win24 {
            out.push(("win24", WIN24_PLACEMENTS, WIDE_WINDOW_PATTERNS * self.phases_for("win24") * buckets));
        }
        if self.win42 {
            out.push(("win42", WIN42_PLACEMENTS, WIDE_WINDOW_PATTERNS * self.phases_for("win42") * buckets));
        }
        out
    }

    pub fn total_entries(&self) -> usize {
        self.families().iter().map(|(_, t, e)| t * e).sum()
    }

    pub fn active_count(&self) -> usize {
        self.families().iter().map(|(_, t, _)| t).sum()
    }
}

#[inline(always)]
fn load(cell: &AtomicU32) -> f32 {
    f32::from_bits(cell.load(Relaxed))
}

#[inline(always)]
fn store(cell: &AtomicU32, value: f32) {
    cell.store(value.to_bits(), Relaxed)
}

/// A vector of `len` atomics holding `value`.  Zero is allocated with
/// alloc_zeroed, so the pages of a zero-filled array (the coherence
/// accumulators) become resident only when they are first written; a
/// non-zero fill writes every element.
fn atomic_vec(len: usize, value: f32) -> Vec<AtomicU32> {
    let bits = value.to_bits();
    if bits == 0 && len > 0 {
        let layout = std::alloc::Layout::array::<AtomicU32>(len).expect("layout");
        // SAFETY: AtomicU32 has the same size and alignment as u32 and the
        // all-zero bit pattern is a valid AtomicU32; the allocation has
        // exactly the layout Vec will free it with (capacity == len).
        unsafe {
            let ptr = std::alloc::alloc_zeroed(layout) as *mut AtomicU32;
            if ptr.is_null() {
                std::alloc::handle_alloc_error(layout);
            }
            return Vec::from_raw_parts(ptr, len, len);
        }
    }
    let mut out = Vec::with_capacity(len);
    out.resize_with(len, || AtomicU32::new(bits));
    out
}

#[derive(Default, Clone, Copy, Debug)]
pub struct UpdateStats {
    pub updates: u64,
    pub abs_delta_sum: f64,
    pub beta_sum: f64,
    pub entry_updates: u64,
}

pub struct Model {
    pub layout: Layout,
    codec: Codec,
    weights: Vec<AtomicU32>,
    /// Temporal-coherence accumulators, present only for a trainable model.
    tc_e: Vec<AtomicU32>,
    tc_a: Vec<AtomicU32>,
    /// First entry of each family slot (rows, cols, win23, win32, win24, win42).
    off: [usize; FAMILIES],
    /// Entries per table of each family slot.
    size: [usize; FAMILIES],
}

impl Model {
    /// A fresh model: every entry `optimistic_total / active_count`, so a
    /// never-seen board is valued at `optimistic_total` rise units.
    pub fn new(layout: Layout, optimistic_total: f32, trainable: bool) -> Model {
        let total = layout.total_entries();
        let init = optimistic_total / layout.active_count() as f32;
        let mut off = [0usize; FAMILIES];
        let mut size = [0usize; FAMILIES];
        let mut cursor = 0usize;
        for (name, tables, entries) in layout.families() {
            let slot = match name {
                "rows" => ROWS,
                "cols" => COLS,
                "win23" => WIN23,
                "win32" => WIN32,
                "win24" => WIN24,
                "win42" => WIN42,
                _ => unreachable!(),
            };
            off[slot] = cursor;
            size[slot] = entries;
            cursor += tables * entries;
        }
        debug_assert_eq!(cursor, total);
        Model {
            layout,
            codec: Codec::new(),
            weights: atomic_vec(total, init),
            tc_e: if trainable { atomic_vec(total, 0.0) } else { Vec::new() },
            tc_a: if trainable { atomic_vec(total, 0.0) } else { Vec::new() },
            off,
            size,
        }
    }

    /// A fill-conditioned, trainable model whose every bucket is a copy of
    /// `source` (an unconditioned model of the same families and phase mode):
    /// multi-stage weight promotion.  The coherence accumulators start at
    /// zero.  The promoted model's value of any board is bit-identical to the
    /// source's, because each active entry is a copy of the source's entry and
    /// the sum runs in the same order.
    pub fn promote(source: &Model, layout: Layout) -> Result<Model, String> {
        if layout.fill == FillMode::None {
            return Err("promote needs a fill-conditioned target layout".into());
        }
        if source.layout != layout.unconditioned() {
            return Err(format!(
                "promote: source layout {} is not the unconditioned form of {}",
                source.layout.spec(),
                layout.spec()
            ));
        }
        let model = Model::new(layout, 0.0, true);
        let buckets = layout.fill.buckets();
        let families = layout.families();
        std::thread::scope(|scope| {
            for (family, tables, dst_size) in &families {
                let slot = match *family {
                    "rows" => ROWS,
                    "cols" => COLS,
                    "win23" => WIN23,
                    "win32" => WIN32,
                    "win24" => WIN24,
                    "win42" => WIN42,
                    _ => unreachable!(),
                };
                // The source table is the (phases x patterns) slab that
                // each bucket of the destination table repeats.
                let src_size = source.size[slot];
                debug_assert_eq!(src_size * buckets, *dst_size);
                for t in 0..*tables {
                    let src_start = source.off[slot] + t * src_size;
                    let dst_start = model.off[slot] + t * dst_size;
                    let model = &model;
                    scope.spawn(move || {
                        let src = &source.weights[src_start..src_start + src_size];
                        for b in 0..buckets {
                            let dst = &model.weights[dst_start + b * src_size..dst_start + (b + 1) * src_size];
                            for (d, s) in dst.iter().zip(src.iter()) {
                                d.store(s.load(Relaxed), Relaxed);
                            }
                        }
                    });
                }
            }
        });
        Ok(model)
    }

    pub fn entries(&self) -> usize {
        self.weights.len()
    }

    pub fn trainable(&self) -> bool {
        !self.tc_a.is_empty()
    }

    pub fn bytes(&self) -> usize {
        (self.weights.len() + self.tc_e.len() + self.tc_a.len()) * 4
    }

    /// Active table entries of the canonical board at rise phase
    /// `moves_remaining` (1..=5).  Fixed order: rows 0..7, columns 0..7,
    /// win23, win32, win24 and win42 placements, each column-major.
    #[inline]
    pub fn features(
        &self,
        board: &Board,
        moves_remaining: i32,
        out: &mut [u64; MAX_ACTIVE],
    ) -> usize {
        // Live states carry 1..=5 moves until the rise.  A terminal state
        // reached by a rise that ended the game carries 0; no caller values
        // such a state on purpose, and clamping keeps every entry index in
        // range for one that does (the CHECK gate perturbs terminal states).
        let canonical = if board.mirrored_is_smaller() {
            board.mirrored()
        } else {
            *board
        };
        let cols = &canonical.cols;
        let phase = (moves_remaining.clamp(1, PHASES as i32) - 1) as usize;
        let layout = self.layout;
        let bucket = layout.fill.bucket(&canonical);
        // Slab of one table: bucket-major, then the phase slab (0 for an
        // unconditioned family), then the pattern.
        let slab = |conditioned: bool, patterns: usize| -> usize {
            let phases = if conditioned { PHASES } else { 1 };
            let p = if conditioned { phase } else { 0 };
            (bucket * phases + p) * patterns
        };
        let mut count = 0usize;
        if layout.rows {
            let rows = row_words(cols);
            let phase_offset = slab(layout.phase == PhaseMode::All, LINE_PATTERNS);
            for (r, &word) in rows.iter().enumerate() {
                out[count] = (self.off[ROWS] + r * self.size[ROWS] + phase_offset) as u64
                    + self.codec.line(word) as u64;
                count += 1;
            }
        }
        if layout.cols {
            let phase_offset = slab(layout.phase != PhaseMode::None, LINE_PATTERNS);
            for (c, &word) in cols.iter().enumerate() {
                out[count] = (self.off[COLS] + c * self.size[COLS] + phase_offset) as u64
                    + self.codec.line(word) as u64;
                count += 1;
            }
        }
        if layout.win23 {
            let phase_offset = slab(layout.phase == PhaseMode::All, WINDOW_PATTERNS);
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 1 {
                let left = cols[c];
                let right = cols[c + 1];
                for top in 0..=4usize {
                    let pattern = self.codec.nib3(chunk3(left, top))
                        + 1000 * self.codec.nib3(chunk3(right, top));
                    out[count] = (self.off[WIN23] + placement * self.size[WIN23] + phase_offset)
                        as u64
                        + pattern as u64;
                    count += 1;
                    placement += 1;
                }
            }
        }
        if layout.win32 {
            let phase_offset = slab(layout.phase == PhaseMode::All, WINDOW_PATTERNS);
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 2 {
                let a = cols[c];
                let b = cols[c + 1];
                let d = cols[c + 2];
                for top in 0..=5usize {
                    let pattern = self.codec.nib2(chunk2(a, top))
                        + 100 * self.codec.nib2(chunk2(b, top))
                        + 10_000 * self.codec.nib2(chunk2(d, top));
                    out[count] = (self.off[WIN32] + placement * self.size[WIN32] + phase_offset)
                        as u64
                        + pattern as u64;
                    count += 1;
                    placement += 1;
                }
            }
        }
        if layout.win24 {
            // 2 wide x 4 tall: columns c, c+1, rows top..top+4 (top 0..=3).
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 1 {
                let left = cols[c];
                let right = cols[c + 1];
                for top in 0..=3usize {
                    let pattern = self.codec.nib4(chunk4(left, top))
                        + 10_000 * self.codec.nib4(chunk4(right, top));
                    out[count] = (self.off[WIN24] + placement * self.size[WIN24]) as u64 + pattern as u64;
                    count += 1;
                    placement += 1;
                }
            }
        }
        if layout.win42 {
            // 4 wide x 2 tall: columns c..c+4, rows top..top+2 (top 0..=5).
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 3 {
                let a = cols[c];
                let b = cols[c + 1];
                let d = cols[c + 2];
                let e = cols[c + 3];
                for top in 0..=5usize {
                    let pattern = self.codec.nib2(chunk2(a, top))
                        + 100 * self.codec.nib2(chunk2(b, top))
                        + 10_000 * self.codec.nib2(chunk2(d, top))
                        + 1_000_000 * self.codec.nib2(chunk2(e, top));
                    out[count] = (self.off[WIN42] + placement * self.size[WIN42]) as u64 + pattern as u64;
                    count += 1;
                    placement += 1;
                }
            }
        }
        count
    }

    /// Sum of the active entries, in rise units, in feature order.
    #[inline]
    pub fn value_of(&self, active: &[u64]) -> f32 {
        let mut sum = 0.0f32;
        for &index in active {
            sum += load(&self.weights[index as usize]);
        }
        sum
    }

    #[inline]
    pub fn value(&self, board: &Board, moves_remaining: i32, scratch: &mut [u64; MAX_ACTIVE]) -> f32 {
        let n = self.features(board, moves_remaining, scratch);
        self.value_of(&scratch[..n])
    }

    /// Temporal-coherence semi-gradient step toward `target` for the state
    /// whose active entries are `active`: every entry moves by
    /// alpha * beta_i * delta / n, where beta_i = |E_i| / A_i is that entry's
    /// coherence (1 before its first update), and the accumulators are then
    /// updated with the error.  `delta` is clamped to +-clamp rise units.
    #[inline]
    pub fn update(
        &self,
        active: &[u64],
        target: f32,
        alpha: f32,
        clamp: f32,
        stats: &mut UpdateStats,
    ) -> f32 {
        debug_assert!(self.trainable());
        let prediction = self.value_of(active);
        let delta = (target - prediction).clamp(-clamp, clamp);
        let step = alpha * delta / active.len() as f32;
        let abs_delta = delta.abs();
        for &index in active {
            let i = index as usize;
            let e = load(&self.tc_e[i]);
            let a = load(&self.tc_a[i]);
            let beta = if a > 0.0 { e.abs() / a } else { 1.0 };
            store(&self.weights[i], load(&self.weights[i]) + beta * step);
            store(&self.tc_e[i], e + delta);
            store(&self.tc_a[i], a + abs_delta);
            stats.beta_sum += beta as f64;
        }
        stats.updates += 1;
        stats.entry_updates += active.len() as u64;
        stats.abs_delta_sum += abs_delta as f64;
        delta
    }

    /// Entries whose coherence denominator is non-zero: the count of table
    /// entries that have received at least one update.  A full scan.
    pub fn touched_entries(&self) -> u64 {
        self.tc_a.iter().filter(|a| load(a) > 0.0).count() as u64
    }

    /// Write the model.  `with_tc` also writes the two accumulator arrays so a
    /// training run can resume; a frozen candidate is written without them.
    pub fn save(&self, path: &std::path::Path, with_tc: bool) -> Result<(), String> {
        let tmp = path.with_extension("tmp");
        {
            let file = std::fs::File::create(&tmp).map_err(|e| format!("{}: {e}", tmp.display()))?;
            let mut out = std::io::BufWriter::with_capacity(1 << 20, file);
            out.write_all(MAGIC).map_err(|e| e.to_string())?;
            let spec = self.layout.spec();
            out.write_all(&(spec.len() as u32).to_le_bytes()).map_err(|e| e.to_string())?;
            out.write_all(spec.as_bytes()).map_err(|e| e.to_string())?;
            out.write_all(&(self.weights.len() as u64).to_le_bytes()).map_err(|e| e.to_string())?;
            out.write_all(&[if with_tc && self.trainable() { 1u8 } else { 0u8 }])
                .map_err(|e| e.to_string())?;
            write_atomics(&mut out, &self.weights)?;
            if with_tc && self.trainable() {
                write_atomics(&mut out, &self.tc_e)?;
                write_atomics(&mut out, &self.tc_a)?;
            }
            out.flush().map_err(|e| e.to_string())?;
        }
        std::fs::rename(&tmp, path).map_err(|e| format!("{}: {e}", path.display()))
    }

    /// Read a model written by `save`.  A file without accumulators loads as
    /// a frozen (non-trainable) model unless `trainable` asks for fresh
    /// zeroed accumulators.
    pub fn load(path: &std::path::Path, trainable: bool) -> Result<Model, String> {
        let file = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut input = std::io::BufReader::with_capacity(1 << 20, file);
        let mut magic = [0u8; 8];
        input.read_exact(&mut magic).map_err(|e| e.to_string())?;
        if &magic != MAGIC {
            return Err(format!("{}: not a D7NTUP01 file", path.display()));
        }
        let mut len = [0u8; 4];
        input.read_exact(&mut len).map_err(|e| e.to_string())?;
        let mut spec = vec![0u8; u32::from_le_bytes(len) as usize];
        input.read_exact(&mut spec).map_err(|e| e.to_string())?;
        let layout = Layout::parse(std::str::from_utf8(&spec).map_err(|e| e.to_string())?)?;
        let mut count = [0u8; 8];
        input.read_exact(&mut count).map_err(|e| e.to_string())?;
        let count = u64::from_le_bytes(count) as usize;
        if count != layout.total_entries() {
            return Err(format!("{}: entry count {count} does not match layout", path.display()));
        }
        let mut flag = [0u8; 1];
        input.read_exact(&mut flag).map_err(|e| e.to_string())?;
        let has_tc = flag[0] == 1;
        let mut model = Model::new(layout, 0.0, trainable);
        read_atomics(&mut input, &model.weights)?;
        if has_tc {
            if trainable {
                read_atomics(&mut input, &model.tc_e)?;
                read_atomics(&mut input, &model.tc_a)?;
            }
        } else if trainable {
            // fresh accumulators: already zero
        }
        if !trainable {
            model.tc_e = Vec::new();
            model.tc_a = Vec::new();
        }
        Ok(model)
    }

    /// SHA-256 of the weights as the file `save(path, false)` would write
    /// them is what the pipeline records; this returns a fast content
    /// fingerprint for logs (FNV-1a over the weight bits).
    pub fn fingerprint(&self) -> u64 {
        let mut hash = 0xcbf2_9ce4_8422_2325u64;
        for cell in &self.weights {
            hash ^= cell.load(Relaxed) as u64;
            hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
        }
        hash
    }
}

fn write_atomics<W: Write>(out: &mut W, cells: &[AtomicU32]) -> Result<(), String> {
    let mut buffer = Vec::with_capacity(1 << 18);
    for chunk in cells.chunks(1 << 16) {
        buffer.clear();
        for cell in chunk {
            buffer.extend_from_slice(&cell.load(Relaxed).to_le_bytes());
        }
        out.write_all(&buffer).map_err(|e| e.to_string())?;
    }
    Ok(())
}

fn read_atomics<R: Read>(input: &mut R, cells: &[AtomicU32]) -> Result<(), String> {
    let mut buffer = vec![0u8; 1 << 18];
    for chunk in cells.chunks(1 << 16) {
        let bytes = chunk.len() * 4;
        input.read_exact(&mut buffer[..bytes]).map_err(|e| e.to_string())?;
        for (cell, word) in chunk.iter().zip(buffer[..bytes].chunks_exact(4)) {
            cell.store(u32::from_le_bytes([word[0], word[1], word[2], word[3]]), Relaxed);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn layouts_size_as_documented() {
        let full = Layout::parse("rows,cols,win23,win32,phase=cols").unwrap();
        assert_eq!(full.active_count(), 74);
        assert_eq!(
            full.total_entries(),
            7 * LINE_PATTERNS + 7 * 5 * LINE_PATTERNS + 60 * WINDOW_PATTERNS
        );
        let wide = Layout::parse("rows,cols,win23,win32,win24,win42,phase=all").unwrap();
        assert_eq!(wide.active_count(), MAX_ACTIVE);
        assert_eq!(
            wide.total_entries(),
            14 * 5 * LINE_PATTERNS + 60 * 5 * WINDOW_PATTERNS + 48 * WIDE_WINDOW_PATTERNS
        );
        assert_eq!(wide.total_entries(), 5_800_000_000);
        assert_eq!(Layout::parse(&wide.spec()).unwrap(), wide);
        let small = Layout::parse("rows").unwrap();
        assert_eq!(small.total_entries(), 7 * LINE_PATTERNS);
        assert!(Layout::parse("phase=cols").is_err());
        assert_eq!(Layout::parse(&full.spec()).unwrap(), full);
        let filled = Layout::parse("rows,cols,win23,win32,phase=all,fill=occ5").unwrap();
        assert_eq!(filled.active_count(), 74);
        assert_eq!(filled.total_entries(), 5 * (14 * 5 * LINE_PATTERNS + 60 * 5 * WINDOW_PATTERNS));
        assert_eq!(filled.total_entries(), 5_000_000_000);
        assert_eq!(filled.spec(), "rows,cols,win23,win32,phase=all,fill=occ5");
        assert_eq!(Layout::parse(&filled.spec()).unwrap(), filled);
        assert_eq!(filled.unconditioned(), Layout::parse("rows,cols,win23,win32,phase=all").unwrap());
        assert_eq!(Layout::parse("rows,cols,win23,win32,phase=all").unwrap().spec(), "rows,cols,win23,win32,phase=all");
        assert!(Layout::parse("rows,win24,fill=hgt5").is_err());
    }

    #[test]
    fn fill_buckets_read_the_board() {
        let mut board = Board { cols: [0u32; BOARD_SIZE] };
        assert_eq!(FillMode::Occupancy5.bucket(&board), 0);
        assert_eq!(FillMode::Height5.bucket(&board), 0);
        for c in 0..BOARD_SIZE {
            for _ in 0..2 {
                board.place_disc(c, 3);
            }
        }
        // 14 discs, tallest column 2.
        assert_eq!(FillMode::Occupancy5.bucket(&board), 1);
        assert_eq!(FillMode::Height5.bucket(&board), 0);
        for _ in 0..4 {
            board.place_disc(3, 4);
        }
        // 18 discs, tallest column 6.
        assert_eq!(FillMode::Occupancy5.bucket(&board), 1);
        assert_eq!(FillMode::Height5.bucket(&board), 3);
        board.place_disc(3, 4);
        assert_eq!(FillMode::Height5.bucket(&board), 4);
        for c in 0..BOARD_SIZE {
            while board.height(c) < 5 {
                board.place_disc(c, 2);
            }
        }
        // 35 discs.
        assert_eq!(FillMode::Occupancy5.bucket(&board), 4);
        assert_eq!(FillMode::None.bucket(&board), 0);
    }

    #[test]
    fn promotion_preserves_values_and_separates_buckets() {
        let source_layout = Layout::parse("rows,win32,phase=cols").unwrap();
        let source = Model::new(source_layout, 3.0, true);
        let mut scratch = [0u64; MAX_ACTIVE];
        let mut stats = UpdateStats::default();
        let mut board = Board::initial();
        board.place_disc(2, 4);
        let n = source.features(&board, 2, &mut scratch);
        source.update(&scratch[..n], 7.0, 1.0, 100.0, &mut stats);
        for fill in ["fill=occ5", "fill=hgt5"] {
            let layout = Layout::parse(&format!("rows,win32,phase=cols,{fill}")).unwrap();
            let promoted = Model::promote(&source, layout).unwrap();
            assert_eq!(promoted.entries(), 5 * source.entries());
            assert!(promoted.trainable());
            assert_eq!(promoted.touched_entries(), 0);
            let mut b2 = Board::initial();
            for phase in 1..=5 {
                assert_eq!(promoted.value(&board, phase, &mut scratch).to_bits(), source.value(&board, phase, &mut scratch).to_bits());
                assert_eq!(promoted.value(&b2, phase, &mut scratch).to_bits(), source.value(&b2, phase, &mut scratch).to_bits());
            }
            // Fill a column so the board changes bucket; the two boards now
            // read different buckets, and an update to one leaves the other.
            for _ in 0..7 {
                b2.place_disc(0, 1);
            }
            for c in 1..7 {
                for _ in 0..5 {
                    b2.place_disc(c, 1);
                }
            }
            assert_ne!(layout.fill.bucket(&b2), layout.fill.bucket(&board));
            let before = promoted.value(&board, 3, &mut scratch);
            let n2 = promoted.features(&b2, 3, &mut scratch);
            promoted.update(&scratch[..n2], 50.0, 1.0, 100.0, &mut stats);
            assert_eq!(promoted.value(&board, 3, &mut scratch).to_bits(), before.to_bits());
        }
        assert!(Model::promote(&source, source_layout).is_err());
        let other = Model::new(Layout::parse("rows,win23,phase=cols").unwrap(), 1.0, false);
        assert!(Model::promote(&other, Layout::parse("rows,win32,phase=cols,fill=occ5").unwrap()).is_err());
    }

    #[test]
    fn features_are_distinct_and_in_range() {
        for spec in ["rows,cols,win23,win32,phase=all", "win24,win42"] {
            let layout = Layout::parse(spec).unwrap();
            let model = Model::new(layout, 1.0, false);
            let mut board = Board::initial();
            board.place_disc(2, 4);
            board.place_disc(3, 1);
            board.place_disc(3, 7);
            let mut out = [0u64; MAX_ACTIVE];
            for mtr in 1..=5 {
                let n = model.features(&board, mtr, &mut out);
                assert_eq!(n, layout.active_count());
                let mut seen: Vec<u64> = out[..n].to_vec();
                seen.sort_unstable();
                seen.dedup();
                assert_eq!(seen.len(), n, "entries must be distinct");
                assert!(out[..n].iter().all(|&i| (i as usize) < model.entries()));
            }
        }
    }

    #[test]
    fn wide_windows_read_the_named_cells() {
        // One disc of value 5 at (row 6, column 3): every 2x4 or 4x2 window
        // that covers that cell has pattern 5 * 10^k for that cell's digit
        // position, and every other window has pattern 0.  A single disc in
        // the middle column is its own mirror, so canonicalisation is moot.
        let layout = Layout::parse("win24,win42").unwrap();
        let model = Model::new(layout, 0.0, false);
        let mut board = Board { cols: [0u32; BOARD_SIZE] };
        board.place_disc(3, 5);
        assert_eq!(board.get(6, 3), 5);
        let mut out = [0u64; MAX_ACTIVE];
        let n = model.features(&board, 5, &mut out);
        assert_eq!(n, 48);
        let mut nonzero = 0;
        for (index, &entry) in out[..n].iter().enumerate() {
            let family_start = if index < 24 { 0usize } else { 24 * WIDE_WINDOW_PATTERNS };
            let local = entry as usize - family_start;
            let pattern = local % WIDE_WINDOW_PATTERNS;
            let placement = local / WIDE_WINDOW_PATTERNS;
            if index < 24 {
                // win24: placement = c * 4 + top; columns c, c+1, rows top..top+4.
                let (c, top) = (placement / 4, placement % 4);
                let covers = (c == 3 || c + 1 == 3) && top + 3 == 6;
                if covers {
                    nonzero += 1;
                    // the left column's lowest row is the least-significant digit
                    let digit = if c == 3 { 0 } else { 4 };
                    assert_eq!(pattern, 5 * 10usize.pow(digit), "win24 placement {placement}");
                } else {
                    assert_eq!(pattern, 0, "win24 placement {placement}");
                }
            } else {
                // win42: placement = c * 6 + top; columns c..c+4, rows top..top+2.
                let (c, top) = (placement / 6, placement % 6);
                let covers = (c..c + 4).contains(&3) && top + 1 == 6;
                if covers {
                    nonzero += 1;
                    // two digits per column, the lower row least significant
                    let digit = 2 * (3 - c) as u32;
                    assert_eq!(pattern, 5 * 10usize.pow(digit), "win42 placement {placement}");
                } else {
                    assert_eq!(pattern, 0, "win42 placement {placement}");
                }
            }
        }
        assert_eq!(nonzero, 2 + 4);
    }

    #[test]
    fn value_is_reflection_invariant_and_optimistic_at_start() {
        let layout = Layout::parse("rows,cols,win23,win32,phase=cols").unwrap();
        let model = Model::new(layout, 20.0, false);
        let mut scratch = [0u64; MAX_ACTIVE];
        let mut board = Board::initial();
        board.place_disc(0, 3);
        board.place_disc(1, 5);
        let value = model.value(&board, 3, &mut scratch);
        let mirrored = model.value(&board.mirrored(), 3, &mut scratch);
        assert_eq!(value.to_bits(), mirrored.to_bits());
        assert!((value - 20.0).abs() < 1e-3);
    }

    #[test]
    fn a_single_update_moves_the_prediction_by_alpha_delta() {
        let layout = Layout::parse("rows,cols").unwrap();
        let model = Model::new(layout, 0.0, true);
        let mut scratch = [0u64; MAX_ACTIVE];
        let board = Board::initial();
        let n = model.features(&board, 5, &mut scratch);
        let mut stats = UpdateStats::default();
        let delta = model.update(&scratch[..n], 4.0, 0.5, 100.0, &mut stats);
        assert!((delta - 4.0).abs() < 1e-6);
        let after = model.value_of(&scratch[..n]);
        assert!((after - 2.0).abs() < 1e-4, "alpha 0.5 of a 4.0 error: {after}");
        // Coherence is computed from the history before the current error,
        // so the first sign reversal still moves at full rate (delta -4, beta
        // |4|/4 = 1 -> value 0) and the accumulators become E 0, A 8.
        model.update(&scratch[..n], -2.0, 0.5, 100.0, &mut stats);
        let next = model.value_of(&scratch[..n]);
        assert!(next.abs() < 1e-4, "{next}");
        // The following update sees beta = |0| / 8 = 0 and does not move.
        model.update(&scratch[..n], 2.0, 0.5, 100.0, &mut stats);
        let third = model.value_of(&scratch[..n]);
        assert!(third.abs() < 1e-4, "{third}");
        assert_eq!(stats.updates, 3);
    }

    #[test]
    fn save_and_load_round_trip() {
        let layout = Layout::parse("win32").unwrap();
        let model = Model::new(layout, 3.0, true);
        let mut scratch = [0u64; MAX_ACTIVE];
        let board = Board::initial();
        let n = model.features(&board, 2, &mut scratch);
        let mut stats = UpdateStats::default();
        model.update(&scratch[..n], 7.0, 1.0, 100.0, &mut stats);
        let dir = std::env::temp_dir().join(format!("d7ntup-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("m.bin");
        model.save(&path, true).unwrap();
        let loaded = Model::load(&path, true).unwrap();
        assert_eq!(loaded.fingerprint(), model.fingerprint());
        assert_eq!(loaded.touched_entries(), model.touched_entries());
        let frozen = Model::load(&path, false).unwrap();
        assert!(!frozen.trainable());
        assert_eq!(frozen.value(&board, 2, &mut scratch).to_bits(), model.value(&board, 2, &mut scratch).to_bits());
        std::fs::remove_dir_all(&dir).ok();
    }
}
