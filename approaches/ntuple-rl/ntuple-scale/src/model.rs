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

/// Optimistic per-entry initialization for the 74-tuple layout (`20.0 / 74.0`).
pub const INIT_VAL: f32 = 20.0f32 / 74.0f32;
pub const INIT_BITS: u32 = INIT_VAL.to_bits();

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CalibrationConfig {
    /// 0 = raw (none), 1 = all tuples (Keshav's zeroed), 2 = selective high-elevation only.
    pub ut_mode: u8,
    pub ut_val: f32,
    pub ut_rows: usize,
    pub ut_col_h: usize,
    pub ut_w23: usize,
    pub ut_w32: usize,
    /// Additive singleton marginal cap on `row 0` (height 7 death row) when occupied.
    pub row0_add: f32,
    /// Additive singleton marginal cap on `row 1` (height 6 row) when k >= 2.
    pub row1_add: f32,
    /// Smooth downward shift subtracted from `cols[c]` when column height == 7.
    pub col_h7_shift: f32,
    /// Smooth downward shift subtracted from `cols[c]` when column height == 6.
    pub col_h6_shift: f32,
    /// Mirror-baseline cap on `win23(top == 0)` when `pattern != 0`.
    pub w23_top0_sym: f32,
    /// Mirror-baseline cap on `win32(top == 0)` when `pattern != 0`.
    pub w32_top0_sym: f32,
    /// If 1, uses `w0.min(0.0)` as base in `w23_top0_sym` / `w32_top0_sym`; if 0, uses `w0`.
    pub top0_zero: u8,
    /// Bonus scaling in `row 1` when a height-6 column has a buried `7` in rows 2..6.
    pub buried7: f32,
    /// If 1, applies `buried7` across all k >= 1 and decouples `row1_add` sum_delta.
    pub b7_all_k: u8,
    /// Bonus scaling in `row 2` when a height-5 column has a buried `6` in rows 3..6.
    pub buried6: f32,
    /// Additive singleton marginal cap on `row 2` (height 5 row) when k >= 2.
    pub row2_add: f32,
    /// Band around INIT_VAL treated as under-visited in ut_mode=2 (for non-empty patterns).
    pub ut_eps: f32,
    /// If 1, caps height-7 columns in `cols[c]` at <= 0.0 (eliminating 1-visit +0.27 landmines).
    pub col_h7_zero: u8,
    /// Blending weight toward lower-height stem (`word & 0x00FF_FFFF` / `0x000F_FFFF`) for height-7 cols.
    pub col_stem: f32,
    /// Blending weight toward 5-high stem (`word & 0x000F_FFFF`) for height-6 cols without a 7.
    pub col_h6_stem: f32,
    /// Extra downward shift on height-7 `cols[c]` when `moves_remaining == 1` (half when `== 2`).
    pub col_h7_mr1: f32,
    /// Extra downward shift on height-6 `cols[c]` (without a 7) when `moves_remaining == 1`.
    pub col_h6_mr1: f32,
    /// Iteration 2: Rao-Blackwellized next_disc wave-1 pop coverage bonus (`union_pop / 7`).
    pub nd_pop: f32,
    /// Iteration 2: Redundant cross-column next_disc pop overlap penalty (`overlap / 7`).
    pub nd_ovlp: f32,
    /// Iteration 2: Wave-1 cover-crack next_disc coverage bonus (`crack_pop / 7`).
    pub nd_crack: f32,
    /// Iteration 2: Exact ply-4 row-rise death fraction penalty when `moves_remaining == 1`.
    pub nd_mr1_dead: f32,
    /// Iteration 2: Minimum board max_height required for nd_pop/nd_ovlp/nd_tall_pop to apply.
    pub nd_min_h: u8,
    /// Iteration 2: Wave-1 pop coverage restricted to tall columns (h >= 4 or spanning h >= 5).
    pub nd_tall_pop: f32,
    /// Iteration 3: Stem backoff in `win23(top=0)` and `win32(top=0)` when height 7 is reached.
    pub win_stem: f32,
    /// Iteration 3: Untouched stem backoff in `win23(top=0..1)` and `win32(top=1)` when height 6 is reached.
    pub win_top1_stem: f32,
    /// Iteration 3: Neutral baseline count (out of 7) subtracted from `union_pop` in `nd_pop`.
    pub nd_base: f32,
    /// Iteration 3: Scale factor in [0, 1] applied to `nd_pop`/`nd_ovlp` when `max_h < nd_min_h`.
    pub nd_low_scale: f32,
    /// Iteration 3: Scale factor in [0, 1] applied at `max_h == 5` (smooth geometric ramp `low -> h5 -> 1.0`).
    pub nd_h5_scale: f32,
    /// Weight of FairLeaf (in points) blended in HybridLeaf.
    pub fair_w: f64,
}

impl Default for CalibrationConfig {
    fn default() -> Self {
        CalibrationConfig {
            ut_mode: 0,
            ut_val: 0.0,
            ut_rows: 1,
            ut_col_h: 6,
            ut_w23: 0,
            ut_w32: 0,
            row0_add: 0.0,
            row1_add: 0.0,
            col_h7_shift: 0.0,
            col_h6_shift: 0.0,
            w23_top0_sym: 0.0,
            w32_top0_sym: 0.0,
            top0_zero: 0,
            buried7: 0.0,
            b7_all_k: 0,
            buried6: 0.0,
            row2_add: 0.0,
            ut_eps: 0.0,
            col_h7_zero: 0,
            col_stem: 0.0,
            col_h6_stem: 0.0,
            col_h7_mr1: 0.0,
            col_h6_mr1: 0.0,
            nd_pop: 0.0,
            nd_ovlp: 0.0,
            nd_crack: 0.0,
            nd_mr1_dead: 0.0,
            nd_min_h: 0,
            nd_tall_pop: 0.0,
            win_stem: 0.0,
            win_top1_stem: 0.0,
            nd_base: 0.0,
            nd_low_scale: 0.0,
            nd_h5_scale: 0.0,
            fair_w: 0.0,
        }
    }
}

impl CalibrationConfig {
    pub fn parse(spec: &str) -> Result<Self, String> {
        let mut cfg = Self::default();
        if spec.is_empty() {
            return Ok(cfg);
        }
        for part in spec.split(',') {
            let part = part.trim();
            if part.is_empty() {
                continue;
            }
            let (k, v) = part.split_once('=').ok_or_else(|| format!("bad calibration kv: {part}"))?;
            match k {
                "ut_mode" => cfg.ut_mode = v.parse().map_err(|_| format!("bad ut_mode: {v}"))?,
                "ut_val" => cfg.ut_val = v.parse().map_err(|_| format!("bad ut_val: {v}"))?,
                "ut_rows" => cfg.ut_rows = v.parse().map_err(|_| format!("bad ut_rows: {v}"))?,
                "ut_col_h" => cfg.ut_col_h = v.parse().map_err(|_| format!("bad ut_col_h: {v}"))?,
                "ut_w23" => cfg.ut_w23 = v.parse().map_err(|_| format!("bad ut_w23: {v}"))?,
                "ut_w32" => cfg.ut_w32 = v.parse().map_err(|_| format!("bad ut_w32: {v}"))?,
                "row0_add" => cfg.row0_add = v.parse().map_err(|_| format!("bad row0_add: {v}"))?,
                "row1_add" => cfg.row1_add = v.parse().map_err(|_| format!("bad row1_add: {v}"))?,
                "col_h7_shift" => cfg.col_h7_shift = v.parse().map_err(|_| format!("bad col_h7_shift: {v}"))?,
                "col_h6_shift" => cfg.col_h6_shift = v.parse().map_err(|_| format!("bad col_h6_shift: {v}"))?,
                "w23_top0_sym" => cfg.w23_top0_sym = v.parse().map_err(|_| format!("bad w23_top0_sym: {v}"))?,
                "w32_top0_sym" => cfg.w32_top0_sym = v.parse().map_err(|_| format!("bad w32_top0_sym: {v}"))?,
                "top0_zero" => cfg.top0_zero = v.parse().map_err(|_| format!("bad top0_zero: {v}"))?,
                "buried7" => cfg.buried7 = v.parse().map_err(|_| format!("bad buried7: {v}"))?,
                "b7_all_k" => cfg.b7_all_k = v.parse().map_err(|_| format!("bad b7_all_k: {v}"))?,
                "buried6" => cfg.buried6 = v.parse().map_err(|_| format!("bad buried6: {v}"))?,
                "row2_add" => cfg.row2_add = v.parse().map_err(|_| format!("bad row2_add: {v}"))?,
                "ut_eps" => cfg.ut_eps = v.parse().map_err(|_| format!("bad ut_eps: {v}"))?,
                "col_h7_zero" => cfg.col_h7_zero = v.parse().map_err(|_| format!("bad col_h7_zero: {v}"))?,
                "col_stem" => cfg.col_stem = v.parse().map_err(|_| format!("bad col_stem: {v}"))?,
                "col_h6_stem" => cfg.col_h6_stem = v.parse().map_err(|_| format!("bad col_h6_stem: {v}"))?,
                "col_h7_mr1" => cfg.col_h7_mr1 = v.parse().map_err(|_| format!("bad col_h7_mr1: {v}"))?,
                "col_h6_mr1" => cfg.col_h6_mr1 = v.parse().map_err(|_| format!("bad col_h6_mr1: {v}"))?,
                "nd_pop" => cfg.nd_pop = v.parse().map_err(|_| format!("bad nd_pop: {v}"))?,
                "nd_ovlp" => cfg.nd_ovlp = v.parse().map_err(|_| format!("bad nd_ovlp: {v}"))?,
                "nd_crack" => cfg.nd_crack = v.parse().map_err(|_| format!("bad nd_crack: {v}"))?,
                "nd_mr1_dead" => cfg.nd_mr1_dead = v.parse().map_err(|_| format!("bad nd_mr1_dead: {v}"))?,
                "nd_min_h" => cfg.nd_min_h = v.parse().map_err(|_| format!("bad nd_min_h: {v}"))?,
                "nd_tall_pop" => cfg.nd_tall_pop = v.parse().map_err(|_| format!("bad nd_tall_pop: {v}"))?,
                "win_stem" => cfg.win_stem = v.parse().map_err(|_| format!("bad win_stem: {v}"))?,
                "win_top1_stem" => cfg.win_top1_stem = v.parse().map_err(|_| format!("bad win_top1_stem: {v}"))?,
                "nd_base" => cfg.nd_base = v.parse().map_err(|_| format!("bad nd_base: {v}"))?,
                "nd_low_scale" => cfg.nd_low_scale = v.parse().map_err(|_| format!("bad nd_low_scale: {v}"))?,
                "nd_h5_scale" => cfg.nd_h5_scale = v.parse().map_err(|_| format!("bad nd_h5_scale: {v}"))?,
                "fair_w" => cfg.fair_w = v.parse().map_err(|_| format!("bad fair_w: {v}"))?,
                _ => return Err(format!("unknown calibration key: {k}")),
            }
        }
        Ok(cfg)
    }
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
            let mut n3 = [[0u32; 5]; BOARD_SIZE];
            for (c, &word) in cols.iter().enumerate() {
                if word != 0 {
                    for top in 0..=4usize {
                        n3[c][top] = self.codec.nib3(chunk3(word, top));
                    }
                }
            }
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 1 {
                let left = &n3[c];
                let right = &n3[c + 1];
                for top in 0..=4usize {
                    let pattern = left[top] + 1000 * right[top];
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
            let mut n2 = [[0u32; 6]; BOARD_SIZE];
            for (c, &word) in cols.iter().enumerate() {
                if word != 0 {
                    for top in 0..=5usize {
                        n2[c][top] = self.codec.nib2(chunk2(word, top));
                    }
                }
            }
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 2 {
                let a = &n2[c];
                let b = &n2[c + 1];
                let d = &n2[c + 2];
                for top in 0..=5usize {
                    let pattern = a[top] + 100 * b[top] + 10_000 * d[top];
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

    /// Calibrated N-tuple evaluation that corrects multi-column dilution in
    /// `row 0` and `row 1`, left-right mirror baseline inversion in `top == 0`
    /// windows, and the 0-visit vs 1-visit cliff in high-elevation tuples,
    /// without mutating or duplicating the 4.0 GB table in memory.
    #[inline]
    pub fn value_calibrated(
        &self,
        board: &Board,
        moves_remaining: i32,
        cfg: &CalibrationConfig,
    ) -> f32 {
        const POW10: [usize; 7] = [1, 10, 100, 1_000, 10_000, 100_000, 1_000_000];
        let canonical = if board.mirrored_is_smaller() {
            board.mirrored()
        } else {
            *board
        };
        let cols = &canonical.cols;
        let phase = (moves_remaining.clamp(1, PHASES as i32) - 1) as usize;
        let layout = self.layout;
        let bucket = layout.fill.bucket(&canonical);
        let slab = |conditioned: bool, patterns: usize| -> usize {
            let phases = if conditioned { PHASES } else { 1 };
            let p = if conditioned { phase } else { 0 };
            (bucket * phases + p) * patterns
        };
        let is_ut = |raw_bits: u32, non_empty: bool| -> bool {
            raw_bits == INIT_BITS
                || (non_empty
                    && cfg.ut_eps > 0.0
                    && (f32::from_bits(raw_bits) - INIT_VAL).abs() <= cfg.ut_eps)
        };
        let mut sum = 0.0f32;
        if layout.rows {
            let rows = row_words(cols);
            let phase_offset = slab(layout.phase == PhaseMode::All, LINE_PATTERNS);
            for (r, &word) in rows.iter().enumerate() {
                let table_base = self.off[ROWS] + r * self.size[ROWS] + phase_offset;
                let pattern = self.codec.line(word) as usize;
                let raw_bits = self.weights[table_base + pattern].load(Relaxed);
                let mut w = if is_ut(raw_bits, word != 0)
                    && (cfg.ut_mode == 1 || (cfg.ut_mode == 2 && r <= cfg.ut_rows))
                {
                    cfg.ut_val
                } else {
                    f32::from_bits(raw_bits)
                };
                if r == 0 && word != 0 && cfg.row0_add > 0.0 {
                    let w0 = load(&self.weights[table_base]);
                    let mut sum_delta = 0.0f32;
                    for c in 0..BOARD_SIZE {
                        let d = ((word >> (4 * c)) & 0xF) as usize;
                        if d != 0 {
                            let cm = c.max(BOARD_SIZE - 1 - c);
                            let mut s_bits = self.weights[table_base + d * POW10[cm]].load(Relaxed);
                            if s_bits == INIT_BITS {
                                let d_eff = if d == 1 || d == 7 { 3 } else { d };
                                s_bits = self.weights[table_base + d_eff * POW10[cm]].load(Relaxed);
                                if s_bits == INIT_BITS {
                                    s_bits = self.weights[table_base + d_eff * POW10[4]].load(Relaxed);
                                }
                            }
                            sum_delta += (f32::from_bits(s_bits) - w0).min(0.0);
                        }
                    }
                    let w_add = w0 + sum_delta;
                    if w_add < w {
                        w += cfg.row0_add * (w_add - w);
                    }
                } else if r == 1 && word != 0 && (cfg.row1_add > 0.0 || cfg.buried7 > 0.0) {
                    let w0 = load(&self.weights[table_base]);
                    let mut sum_delta = 0.0f32;
                    let mut buried_bonus = 0.0f32;
                    let mut k = 0u32;
                    for c in 0..BOARD_SIZE {
                        let d = ((word >> (4 * c)) & 0xF) as usize;
                        if d != 0 {
                            k += 1;
                            let cm = c.max(BOARD_SIZE - 1 - c);
                            let low5 = cols[c] & 0x000F_FFFF;
                            let has_b7 = (cols[c] >> 24) == 0
                                && (low5 & (low5 >> 1) & (low5 >> 2) & 0x0001_1111) != 0;
                            if cfg.b7_all_k != 0 {
                                let d_cur = if d == 1 || d == 6 { 3 } else { d };
                                let s_cur = load(&self.weights[table_base + d_cur * POW10[cm]]);
                                sum_delta += (s_cur - w0).min(0.0);
                                if has_b7 && cfg.buried7 > 0.0 && d != 7 {
                                    let s_7 = load(&self.weights[table_base + 7 * POW10[cm]]);
                                    buried_bonus += cfg.buried7 * (s_7 - s_cur).max(0.0);
                                }
                            } else {
                                let d_eff = if has_b7 && cfg.buried7 > 0.0 {
                                    7
                                } else if d == 1 {
                                    3
                                } else {
                                    d
                                };
                                let s_eff = load(&self.weights[table_base + d_eff * POW10[cm]]);
                                sum_delta += s_eff - w0;
                                if has_b7 && cfg.buried7 > 0.0 && d != 7 {
                                    let d_cur = if d == 1 { 3 } else { d };
                                    let s_cur = load(&self.weights[table_base + d_cur * POW10[cm]]);
                                    buried_bonus += cfg.buried7 * (s_eff - s_cur).max(0.0);
                                }
                            }
                        }
                    }
                    if cfg.b7_all_k != 0 {
                        if k >= 2 && cfg.row1_add > 0.0 {
                            let w_add = w0 + sum_delta;
                            if w_add < w {
                                w += cfg.row1_add * (w_add - w);
                            }
                        }
                        w += buried_bonus;
                    } else if k >= 2 && cfg.row1_add > 0.0 {
                        let w_add = w0 + sum_delta;
                        if w_add < w {
                            w += cfg.row1_add * (w_add - w);
                        }
                    } else if k == 1 && buried_bonus > 0.0 {
                        w += buried_bonus;
                    }
                } else if r == 2 && word != 0 && (cfg.row2_add > 0.0 || cfg.buried6 > 0.0) {
                    if cfg.row2_add > 0.0 {
                        let w0 = load(&self.weights[table_base]);
                        let mut sum_delta = 0.0f32;
                        let mut k = 0u32;
                        for c in 0..BOARD_SIZE {
                            let d = ((word >> (4 * c)) & 0xF) as usize;
                            if d != 0 {
                                k += 1;
                                let cm = c.max(BOARD_SIZE - 1 - c);
                                let mut s_bits = self.weights[table_base + d * POW10[cm]].load(Relaxed);
                                if s_bits == INIT_BITS {
                                    let d_eff = if d == 1 || d == 5 { 3 } else { d };
                                    s_bits = self.weights[table_base + d_eff * POW10[cm]].load(Relaxed);
                                }
                                sum_delta += (f32::from_bits(s_bits) - w0).min(0.0);
                            }
                        }
                        if k >= 2 {
                            let w_add = w0 + sum_delta;
                            if w_add < w {
                                w += cfg.row2_add * (w_add - w);
                            }
                        }
                    }
                    if cfg.buried6 > 0.0 {
                        for c in 0..BOARD_SIZE {
                            let d = ((word >> (4 * c)) & 0xF) as usize;
                            if d != 0 && d != 6 && (cols[c] >> 20) == 0 {
                                let low4 = cols[c] & 0x0000_FFFF;
                                if ((!low4) & (low4 >> 1) & (low4 >> 2) & 0x0000_1111) != 0 {
                                    let cm = c.max(BOARD_SIZE - 1 - c);
                                    let d_cur = if d == 1 || d == 5 { 3 } else { d };
                                    let s_cur = load(&self.weights[table_base + d_cur * POW10[cm]]);
                                    let s_6 = load(&self.weights[table_base + 6 * POW10[cm]]);
                                    w += cfg.buried6 * (s_6 - s_cur).max(0.0);
                                }
                            }
                        }
                    }
                }
                sum += w;
            }
        }
        if layout.cols {
            let phase_offset = slab(layout.phase != PhaseMode::None, LINE_PATTERNS);
            for (c, &word) in cols.iter().enumerate() {
                let table_base = self.off[COLS] + c * self.size[COLS] + phase_offset;
                let pattern = self.codec.line(word) as usize;
                let raw_bits = self.weights[table_base + pattern].load(Relaxed);
                let h_ge_min = cfg.ut_col_h == 0 || (cfg.ut_col_h <= 7 && word >= (1u32 << (4 * (cfg.ut_col_h - 1))));
                let mut w = if is_ut(raw_bits, word != 0)
                    && (cfg.ut_mode == 1 || (cfg.ut_mode == 2 && h_ge_min))
                {
                    cfg.ut_val
                } else {
                    f32::from_bits(raw_bits)
                };
                if word >= 0x0100_0000 {
                    if cfg.col_h7_zero != 0 {
                        w = w.min(0.0);
                    }
                    if cfg.col_stem > 0.0 {
                        let w6_word = word & 0x00FF_FFFF;
                        let raw6 = self.weights[table_base + self.codec.line(w6_word) as usize].load(Relaxed);
                        let mut w_stem = if !is_ut(raw6, true) {
                            f32::from_bits(raw6)
                        } else {
                            let w5_word = word & 0x000F_FFFF;
                            let raw5 = self.weights[table_base + self.codec.line(w5_word) as usize].load(Relaxed);
                            if !is_ut(raw5, true) {
                                f32::from_bits(raw5)
                            } else {
                                cfg.ut_val
                            }
                        };
                        if cfg.col_h7_zero != 0 {
                            w_stem = w_stem.min(0.0);
                        }
                        if w_stem < w {
                            w += cfg.col_stem * (w_stem - w);
                        }
                    }
                    if cfg.col_h7_shift != 0.0 {
                        w -= cfg.col_h7_shift;
                    }
                    if cfg.col_h7_mr1 != 0.0 {
                        if phase == 0 {
                            w -= cfg.col_h7_mr1;
                        } else if phase == 1 {
                            w -= 0.5 * cfg.col_h7_mr1;
                        }
                    }
                } else if word >= 0x0010_0000 && word < 0x0100_0000 {
                    let has_7 = (word & (word >> 1) & (word >> 2) & 0x0011_1111) != 0;
                    if !has_7 {
                        if cfg.col_h6_stem > 0.0 {
                            let w5_word = word & 0x000F_FFFF;
                            let raw5 = self.weights[table_base + self.codec.line(w5_word) as usize].load(Relaxed);
                            let w5 = if !is_ut(raw5, true) {
                                f32::from_bits(raw5)
                            } else {
                                cfg.ut_val
                            };
                            if w5 < w {
                                w += cfg.col_h6_stem * (w5 - w);
                            }
                        }
                        if cfg.col_h6_shift != 0.0 {
                            w -= cfg.col_h6_shift;
                        }
                        if cfg.col_h6_mr1 != 0.0 && phase == 0 {
                            w -= cfg.col_h6_mr1;
                        }
                    }
                }
                sum += w;
            }
        }
        if layout.win23 {
            let phase_offset = slab(layout.phase == PhaseMode::All, WINDOW_PATTERNS);
            let mut n3 = [[0u32; 5]; BOARD_SIZE];
            for (c, &word) in cols.iter().enumerate() {
                if word != 0 {
                    for top in 0..=4usize {
                        n3[c][top] = self.codec.nib3(chunk3(word, top));
                    }
                }
            }
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 1 {
                let left = &n3[c];
                let right = &n3[c + 1];
                for top in 0..=4usize {
                    let pattern = (left[top] + 1000 * right[top]) as usize;
                    let table_base = self.off[WIN23] + placement * self.size[WIN23] + phase_offset;
                    let raw_bits = self.weights[table_base + pattern].load(Relaxed);
                    let pat_ut = is_ut(raw_bits, pattern != 0)
                        && (cfg.ut_mode == 1 || (cfg.ut_mode == 2 && top <= cfg.ut_w23));
                    let mut w = if pat_ut {
                        cfg.ut_val
                    } else {
                        f32::from_bits(raw_bits)
                    };
                    if top == 0 && (left[0] >= 10 || right[0] >= 10) && cfg.w23_top0_sym > 0.0 {
                        let p_h5 = ((left[0] % 10) + 1000 * (right[0] % 10)) as usize;
                        let w_h5 = load(&self.weights[table_base + p_h5]);
                        let base = if cfg.top0_zero != 0 { w_h5.min(0.0) } else { w_h5 };
                        let bound = base + (f32::from_bits(raw_bits) - INIT_VAL).min(0.0);
                        if bound < w {
                            w += cfg.w23_top0_sym * (bound - w);
                        }
                    }
                    if top == 0 && (left[0] >= 100 || right[0] >= 100) && cfg.win_stem > 0.0 {
                        if (left[0] / 100) != 7 && (right[0] / 100) != 7 {
                            let eval_p = |p: usize| -> f32 {
                                let b = self.weights[table_base + p].load(Relaxed);
                                if is_ut(b, p != 0) && (cfg.ut_mode == 1 || (cfg.ut_mode == 2 && top <= cfg.ut_w23)) {
                                    cfg.ut_val
                                } else {
                                    f32::from_bits(b)
                                }
                            };
                            let p6 = ((left[0] % 100) + 1000 * (right[0] % 100)) as usize;
                            let p5 = ((left[0] % 10) + 1000 * (right[0] % 10)) as usize;
                            let bound = eval_p(p6).min(eval_p(p5)).min(0.0);
                            if bound < w {
                                w += cfg.win_stem * (bound - w);
                            }
                        }
                    } else if pat_ut && cfg.win_top1_stem > 0.0 {
                        if top == 0 && (left[0] >= 10 || right[0] >= 10) {
                            let p5 = ((left[0] % 10) + 1000 * (right[0] % 10)) as usize;
                            let b5 = self.weights[table_base + p5].load(Relaxed);
                            let w5 = if is_ut(b5, p5 != 0) { cfg.ut_val } else { f32::from_bits(b5) };
                            let bound = w5.min(0.0);
                            if bound < w {
                                w += cfg.win_top1_stem * (bound - w);
                            }
                        } else if top == 1 && (left[1] >= 100 || right[1] >= 100) {
                            let p5 = ((left[1] % 100) + 1000 * (right[1] % 100)) as usize;
                            let b5 = self.weights[table_base + p5].load(Relaxed);
                            let w5 = if is_ut(b5, p5 != 0) { cfg.ut_val } else { f32::from_bits(b5) };
                            let bound = w5.min(0.0);
                            if bound < w {
                                w += cfg.win_top1_stem * (bound - w);
                            }
                        }
                    }
                    sum += w;
                    placement += 1;
                }
            }
        }
        if layout.win32 {
            let phase_offset = slab(layout.phase == PhaseMode::All, WINDOW_PATTERNS);
            let mut n2 = [[0u32; 6]; BOARD_SIZE];
            for (c, &word) in cols.iter().enumerate() {
                if word != 0 {
                    for top in 0..=5usize {
                        n2[c][top] = self.codec.nib2(chunk2(word, top));
                    }
                }
            }
            let mut placement = 0usize;
            for c in 0..BOARD_SIZE - 2 {
                let a = &n2[c];
                let b = &n2[c + 1];
                let d = &n2[c + 2];
                for top in 0..=5usize {
                    let pattern = (a[top] + 100 * b[top] + 10_000 * d[top]) as usize;
                    let table_base = self.off[WIN32] + placement * self.size[WIN32] + phase_offset;
                    let raw_bits = self.weights[table_base + pattern].load(Relaxed);
                    let pat_ut = is_ut(raw_bits, pattern != 0)
                        && (cfg.ut_mode == 1 || (cfg.ut_mode == 2 && top <= cfg.ut_w32));
                    let mut w = if pat_ut {
                        cfg.ut_val
                    } else {
                        f32::from_bits(raw_bits)
                    };
                    if top == 0 && pattern != 0 && cfg.w32_top0_sym > 0.0 {
                        let w0 = load(&self.weights[table_base]);
                        let base = if cfg.top0_zero != 0 { w0.min(0.0) } else { w0 };
                        let bound = base + (f32::from_bits(raw_bits) - INIT_VAL).min(0.0);
                        if bound < w {
                            w += cfg.w32_top0_sym * (bound - w);
                        }
                    }
                    if top == 0 && (a[0] >= 10 || b[0] >= 10 || d[0] >= 10) && cfg.win_stem > 0.0 {
                        if (a[0] / 10) != 7 && (b[0] / 10) != 7 && (d[0] / 10) != 7 {
                            let p6 = ((a[0] % 10) + 100 * (b[0] % 10) + 10_000 * (d[0] % 10)) as usize;
                            let b6 = self.weights[table_base + p6].load(Relaxed);
                            let w6 = if is_ut(b6, p6 != 0) && (cfg.ut_mode == 1 || (cfg.ut_mode == 2 && top <= cfg.ut_w32)) {
                                cfg.ut_val
                            } else {
                                f32::from_bits(b6)
                            };
                            let bound = w6.min(0.0);
                            if bound < w {
                                w += cfg.win_stem * (bound - w);
                            }
                        }
                    } else if pat_ut && top == 1 && (a[1] >= 10 || b[1] >= 10 || d[1] >= 10) && cfg.win_top1_stem > 0.0 {
                        let p5 = ((a[1] % 10) + 100 * (b[1] % 10) + 10_000 * (d[1] % 10)) as usize;
                        let b5 = self.weights[table_base + p5].load(Relaxed);
                        let w5 = if is_ut(b5, p5 != 0) { cfg.ut_val } else { f32::from_bits(b5) };
                        let bound = w5.min(0.0);
                        if bound < w {
                            w += cfg.win_top1_stem * (bound - w);
                        }
                    }
                    sum += w;
                    placement += 1;
                }
            }
        }
        if cfg.nd_pop != 0.0
            || cfg.nd_ovlp != 0.0
            || cfg.nd_crack != 0.0
            || cfg.nd_mr1_dead != 0.0
            || cfg.nd_tall_pop != 0.0
        {
            let mut heights = [0u8; BOARD_SIZE];
            let mut h7_mask = 0u8;
            let mut h5_mask = 0u8;
            let mut min_h = 7u8;
            let mut max_h = 0u8;
            for c in 0..BOARD_SIZE {
                let w = cols[c];
                let nz = (w | (w >> 1) | (w >> 2) | (w >> 3)) & 0x0111_1111;
                let h = nz.count_ones() as u8;
                heights[c] = h;
                if h == 7 {
                    h7_mask |= 1u8 << c;
                }
                if h >= 5 {
                    h5_mask |= 1u8 << c;
                }
                if h < min_h {
                    min_h = h;
                }
                if h > max_h {
                    max_h = h;
                }
            }
            let h_scale = if max_h >= cfg.nd_min_h {
                1.0f32
            } else if max_h == 5 && cfg.nd_h5_scale > 0.0 {
                cfg.nd_h5_scale
            } else {
                cfg.nd_low_scale
            };
            if h_scale > 0.0 {
                let mut union_pop = 0u8;
                let mut tall_union_pop = 0u8;
                let mut crack_pop = 0u8;
                let mut sum_col_pops = 0u32;
                let mut h7_rescue = 0u8;
                for c in 0..BOARD_SIZE {
                    let h = heights[c] as usize;
                    if h >= 7 {
                        continue;
                    }
                    let h_new = (h + 1) as u32;
                    let sh = 4 * h;
                    let col = cols[c];
                    let xor = col ^ (h_new * 0x0111_1111);
                    let col_has_hnew =
                        ((xor | (xor >> 1) | (xor >> 2) | (xor >> 3)) & 0x0111_1111) != 0x0111_1111;
                    let v_mask: u8 = if col_has_hnew { 0xFE } else { 1u8 << h_new };

                    let mut left = c;
                    while left > 0 && heights[left - 1] as usize > h {
                        left -= 1;
                    }
                    let mut right = c;
                    while right + 1 < BOARD_SIZE && heights[right + 1] as usize > h {
                        right += 1;
                    }
                    let run_len = (right - left + 1) as u32;
                    let mut row_has_len = false;
                    for k in left..=right {
                        if k != c && ((cols[k] >> sh) & 0xF) == run_len {
                            row_has_len = true;
                            break;
                        }
                    }
                    let h_mask: u8 = if row_has_len { 0xFE } else { 1u8 << run_len };
                    let col_pop = v_mask | h_mask;
                    union_pop |= col_pop;
                    sum_col_pops += col_pop.count_ones();

                    let span_mask = ((1u16 << (right + 1)) - (1u16 << left)) as u8;
                    if h >= 4 {
                        tall_union_pop |= col_pop;
                    } else if (span_mask & h5_mask) != 0 {
                        tall_union_pop |= h_mask;
                    }

                    let adj_cover = (h > 0 && ((col >> (sh - 4)) & 8) != 0)
                        || (c > 0 && ((cols[c - 1] >> sh) & 8) != 0)
                        || (c + 1 < BOARD_SIZE && ((cols[c + 1] >> sh) & 8) != 0);
                    if adj_cover {
                        crack_pop |= (1u8 << h_new) | (1u8 << run_len);
                    }
                    if col_has_hnew && (col & 0x0888_8888) != 0 {
                        crack_pop |= 0xFE;
                    }
                    if h7_mask != 0 {
                        if (span_mask & h7_mask) != 0 {
                            h7_rescue |= h_mask;
                        }
                        if (c > 0 && (h7_mask & (1u8 << (c - 1))) != 0)
                            || (c + 1 < BOARD_SIZE && (h7_mask & (1u8 << (c + 1))) != 0)
                        {
                            h7_rescue |= col_pop;
                        }
                    }
                }
                let u_cnt = union_pop.count_ones() as f32;
                if cfg.nd_pop != 0.0 {
                    sum += h_scale * cfg.nd_pop * ((u_cnt - cfg.nd_base) / 7.0);
                }
                if cfg.nd_tall_pop != 0.0 {
                    sum += h_scale * cfg.nd_tall_pop * (tall_union_pop.count_ones() as f32 / 7.0);
                }
                if cfg.nd_ovlp != 0.0 && sum_col_pops > union_pop.count_ones() {
                    let ovlp = (sum_col_pops - union_pop.count_ones()) as f32;
                    sum -= h_scale * cfg.nd_ovlp * (ovlp / 7.0);
                }
                if cfg.nd_crack != 0.0 {
                    sum += h_scale * cfg.nd_crack * (crack_pop.count_ones() as f32 / 7.0);
                }
                if cfg.nd_mr1_dead != 0.0 && phase == 0 {
                    if h7_mask != 0 {
                        let dead_no_pop = (7 - union_pop.count_ones()) as f32;
                        let dead_no_rescue = (7 - h7_rescue.count_ones()) as f32;
                        sum -= h_scale * cfg.nd_mr1_dead * ((0.5 * dead_no_pop + 0.5 * dead_no_rescue) / 7.0);
                    } else if min_h >= 6 {
                        let dead = (7 - union_pop.count_ones()) as f32;
                        sum -= h_scale * cfg.nd_mr1_dead * (dead / 7.0);
                    }
                }
            }
        }
        sum
    }

    /// Stream a second weight file (`control`) in 256 KB chunks and blend into
    /// `self.weights` in place with zero extra memory allocation.
    pub fn blend_control(
        &self,
        path: &std::path::Path,
        w_shared: f32,
        w_new: f32,
    ) -> Result<(), String> {
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
        if layout != self.layout {
            return Err(format!("{}: layout mismatch", path.display()));
        }
        let mut count = [0u8; 8];
        input.read_exact(&mut count).map_err(|e| e.to_string())?;
        let count = u64::from_le_bytes(count) as usize;
        if count != self.weights.len() {
            return Err(format!("{}: entry count mismatch", path.display()));
        }
        let mut flag = [0u8; 1];
        input.read_exact(&mut flag).map_err(|e| e.to_string())?;
        let mut buffer = vec![0u8; 1 << 18];
        for chunk in self.weights.chunks(1 << 16) {
            let bytes = chunk.len() * 4;
            input.read_exact(&mut buffer[..bytes]).map_err(|e| e.to_string())?;
            for (cell, word) in chunk.iter().zip(buffer[..bytes].chunks_exact(4)) {
                let p_bits = cell.load(Relaxed);
                let c_bits = u32::from_le_bytes([word[0], word[1], word[2], word[3]]);
                if p_bits == c_bits {
                    continue;
                }
                let blended = if p_bits == INIT_BITS {
                    let c = f32::from_bits(c_bits);
                    INIT_VAL + w_new * (c - INIT_VAL)
                } else if c_bits == INIT_BITS {
                    f32::from_bits(p_bits)
                } else {
                    let p = f32::from_bits(p_bits);
                    let c = f32::from_bits(c_bits);
                    (1.0 - w_shared) * p + w_shared * c
                };
                cell.store(blended.to_bits(), Relaxed);
            }
        }
        Ok(())
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
