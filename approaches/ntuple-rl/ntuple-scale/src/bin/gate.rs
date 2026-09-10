// CHECK-tier gates for the n-tuple leaf.  No leased seed is read: random
// boards are reached by random legal play from the already-opened Rust-engine
// probe block (0xa5277000+, SEEDLEASE-A52-FAST), exactly as the nnue-evolution
// gates did.  Prints one PASS/FAIL line per gate and exits non-zero on any
// failure.
//
// Usage: gate [--probe-start 0xa5277000] [--layout SPEC] [--weights FILE]
//
// With --weights, the leaf-in-search gates (depth 3 and depth 4) run on the
// loaded frozen tables instead of a fresh optimistic model, so the exact
// tables a screen will play are the ones proven legal, complete and
// deterministic; the layout is then read from the file.
//
// The default candidate layout is the wide one of the replication experiment
// (rows, cols, win23, win32, win24, win42, phase=all); the first experiment's
// layout is covered by the feature-index gate list.

use std::sync::Arc;
use std::time::Instant;

use drop7_ntuple_scale::game::{evaluate_arm, Arm};
use drop7_ntuple_scale::model::{FillMode, Layout, Model, UpdateStats, MAX_ACTIVE};
use drop7_ntuple_scale::policy::{choose, NTupleLeaf, PolicyParams};
use drop7_ntuple_scale::tuples::{base10_ref, row_words, row_words_ref, Codec};
use drop7_rs::board::{Board, BOARD_SIZE};
use drop7_rs::engine::{play_headless_move, FullWaveSink, State};
use drop7_rs::rng::Mulberry32;
use drop7_rs::search::Leaf;

struct Gates {
    failures: usize,
}

impl Gates {
    fn report(&mut self, name: &str, passed: bool, detail: String) {
        println!("{} {name}: {detail}", if passed { "PASS" } else { "FAIL" });
        if !passed {
            self.failures += 1;
        }
    }
}

/// Random public states reached by random legal play on probe seeds.
fn sample_states(probe_start: u32, games: u32, per_game: usize) -> Vec<State> {
    let mut states = Vec::new();
    for game in 0..games {
        let seed = probe_start.wrapping_add(game);
        let mut rng = Mulberry32::new(seed ^ 0x5151_5151);
        let mut state = State::initial_headless(seed);
        let mut sink = FullWaveSink::new();
        let mut taken = 0;
        while !state.game_over && state.moves_played < 400 && taken < per_game {
            let legal: Vec<usize> = (0..7).filter(|&c| state.board.is_legal(c)).collect();
            if legal.is_empty() {
                break;
            }
            let column = legal[(rng.next_bits() as usize) % legal.len()];
            sink.clear();
            if play_headless_move(&mut state, seed, column, &mut sink).is_none() {
                break;
            }
            if rng.next_bits() % 3 == 0 {
                states.push(state);
                taken += 1;
            }
        }
    }
    states
}

/// Independent fill bucket from the cell accessor: occupied cells counted
/// one by one, the tallest column as the highest occupied row.
fn fill_bucket_ref(layout: &Layout, canonical: &Board) -> u64 {
    let mut occupied = 0u64;
    let mut tallest = 0u64;
    for c in 0..BOARD_SIZE {
        let mut height = 0u64;
        for r in 0..BOARD_SIZE {
            if canonical.get(r, c) != 0 {
                occupied += 1;
                // row 0 is the top: a disc at row r means height >= 7 - r.
                height = height.max(7 - r as u64);
            }
        }
        tallest = tallest.max(height);
    }
    match layout.fill {
        FillMode::None => 0,
        FillMode::Occupancy5 => [13u64, 20, 27, 34].iter().filter(|&&edge| occupied > edge).count() as u64,
        FillMode::Height5 => [3u64, 4, 5, 6].iter().filter(|&&edge| tallest > edge).count() as u64,
    }
}

/// Reference feature construction through the cell accessor and Horner
/// base-10 codes, mirroring model.rs's documented layout independently.
fn features_ref(layout: &Layout, board: &Board, moves_remaining: i32) -> Vec<u64> {
    let canonical = if board.mirrored_is_smaller() { board.mirrored() } else { *board };
    let get = |r: usize, c: usize| canonical.get(r, c) as u64;
    let phase = (moves_remaining - 1) as u64;
    let bucket = fill_bucket_ref(layout, &canonical);
    let buckets = if layout.fill == FillMode::None { 1u64 } else { 5 };
    let mut out = Vec::new();
    let mut base = 0u64;
    let line = 10_000_000u64;
    let window = 1_000_000u64;
    let wide = 100_000_000u64;
    let phases_all = layout.phase == drop7_ntuple_scale::model::PhaseMode::All;
    let phases_cols = layout.phase != drop7_ntuple_scale::model::PhaseMode::None;
    // A table is buckets x phase slabs x patterns, bucket-major; the offset
    // of one state's slab within it.
    let slab = |conditioned: bool, patterns: u64| -> u64 {
        let phases = if conditioned { 5 } else { 1 };
        (bucket * phases + if conditioned { phase } else { 0 }) * patterns
    };
    if layout.rows {
        let size = (if phases_all { 5 * line } else { line }) * buckets;
        for r in 0..BOARD_SIZE {
            let mut code = 0u64;
            for c in (0..BOARD_SIZE).rev() {
                code = code * 10 + get(r, c);
            }
            out.push(base + r as u64 * size + slab(phases_all, line) + code);
        }
        base += 7 * size;
    }
    if layout.cols {
        let size = (if phases_cols { 5 * line } else { line }) * buckets;
        for c in 0..BOARD_SIZE {
            // nibble n = row 6-n is the least significant digit.
            let mut code = 0u64;
            for r in 0..BOARD_SIZE {
                code = code * 10 + get(r, c);
            }
            out.push(base + c as u64 * size + slab(phases_cols, line) + code);
        }
        base += 7 * size;
    }
    if layout.win23 {
        let size = (if phases_all { 5 * window } else { window }) * buckets;
        let mut placement = 0u64;
        for c in 0..BOARD_SIZE - 1 {
            for top in 0..=4usize {
                // chunk3 of a column: rows top, top+1, top+2 with top+2 least
                // significant; the right column is scaled by 1000.
                let left = get(top, c) * 100 + get(top + 1, c) * 10 + get(top + 2, c);
                let right = get(top, c + 1) * 100 + get(top + 1, c + 1) * 10 + get(top + 2, c + 1);
                out.push(base + placement * size + slab(phases_all, window) + left + 1000 * right);
                placement += 1;
            }
        }
        base += 30 * size;
    }
    if layout.win32 {
        let size = (if phases_all { 5 * window } else { window }) * buckets;
        let mut placement = 0u64;
        for c in 0..BOARD_SIZE - 2 {
            for top in 0..=5usize {
                let a = get(top, c) * 10 + get(top + 1, c);
                let b = get(top, c + 1) * 10 + get(top + 1, c + 1);
                let d = get(top, c + 2) * 10 + get(top + 1, c + 2);
                out.push(base + placement * size + slab(phases_all, window) + a + 100 * b + 10_000 * d);
                placement += 1;
            }
        }
        base += 30 * size;
    }
    if layout.win24 {
        // never phase-conditioned: 2 columns x 4 rows, rows top..top+4 with
        // top+3 least significant, the right column scaled by 10^4.
        let mut placement = 0u64;
        for c in 0..BOARD_SIZE - 1 {
            for top in 0..=3usize {
                let mut left = 0u64;
                let mut right = 0u64;
                for r in 0..4 {
                    left = left * 10 + get(top + r, c);
                    right = right * 10 + get(top + r, c + 1);
                }
                out.push(base + placement * wide + left + 10_000 * right);
                placement += 1;
            }
        }
        base += 24 * wide;
    }
    if layout.win42 {
        // never phase-conditioned: 4 columns x 2 rows, each column two
        // digits (top, top+1) scaled by 100^k for column offset k.
        let mut placement = 0u64;
        for c in 0..BOARD_SIZE - 3 {
            for top in 0..=5usize {
                let mut code = 0u64;
                for k in 0..4 {
                    code += (get(top, c + k) * 10 + get(top + 1, c + k)) * 100u64.pow(k as u32);
                }
                out.push(base + placement * wide + code);
                placement += 1;
            }
        }
    }
    out
}

fn params_default() -> PolicyParams {
    PolicyParams::default()
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut probe_start = 0xa527_7000u32;
    let mut layout_spec = "rows,cols,win23,win32,win24,win42,phase=all".to_string();
    let mut weights: Option<String> = None;
    let mut i = 1;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--probe-start" => probe_start = u32::from_str_radix(args[i + 1].trim_start_matches("0x"), 16).expect("hex"),
            "--layout" => layout_spec = args[i + 1].clone(),
            "--weights" => weights = Some(args[i + 1].clone()),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    let frozen: Option<Arc<Model>> = weights.as_deref().map(|path| {
        let model = Model::load(std::path::Path::new(path), false).expect("--weights loads");
        layout_spec = model.layout.spec();
        Arc::new(model)
    });
    let mut gates = Gates { failures: 0 };
    println!("gate: probe block 0x{probe_start:08x}, candidate layout {layout_spec}{}", if let Some(path) = &weights { format!(", frozen tables {path}") } else { String::new() });

    // 1. Codec against Horner on every valid 7-nibble word structure sample.
    {
        let codec = Codec::new();
        let mut rng = Mulberry32::new(0x0c0d_ec01);
        let mut mismatches = 0u64;
        let trials = 2_000_000u64;
        for _ in 0..trials {
            let mut word = 0u32;
            for n in 0..7 {
                word |= (rng.next_bits() % 10) << (4 * n);
            }
            if codec.line(word) != base10_ref(word, 7) {
                mismatches += 1;
            }
        }
        gates.report("codec-vs-horner", mismatches == 0 && codec.line(0x0999_9999) == 9_999_999, format!("{trials} random line words, {mismatches} mismatches; max index {}", codec.line(0x0999_9999)));
    }

    let states = sample_states(probe_start, 64, 40);
    println!("sampled {} public states by random play on 64 probe seeds", states.len());

    // 2. Row gather against the cell accessor.
    {
        let mismatches = states.iter().filter(|s| row_words(&s.board.cols) != row_words_ref(&s.board)).count();
        gates.report("row-words-vs-accessor", mismatches == 0, format!("{} states, {mismatches} mismatches", states.len()));
    }

    // 3. Feature indices against the independent reference, every layout.
    for spec in ["rows", "cols", "win23", "win32", "win24", "win42", "rows,cols,win23,win32,phase=none", "rows,cols,win23,win32,phase=cols", "rows,cols,win23,win32,phase=all", "rows,cols,win23,win32,win24,win42,phase=all", "cols,fill=occ5", "rows,win23,phase=none,fill=hgt5", "rows,cols,win23,win32,phase=all,fill=occ5", "rows,cols,win23,win32,phase=all,fill=hgt5"] {
        let layout = Layout::parse(spec).unwrap();
        let model = Model::new(layout, 1.0, false);
        let mut out = [0u64; MAX_ACTIVE];
        let mut mismatches = 0usize;
        let mut duplicates = 0usize;
        let mut out_of_range = 0usize;
        for state in &states {
            for mtr in 1..=5 {
                let n = model.features(&state.board, mtr, &mut out);
                let expected = features_ref(&layout, &state.board, mtr);
                let got: Vec<u64> = out[..n].to_vec();
                if got != expected {
                    mismatches += 1;
                }
                let mut sorted = got.clone();
                sorted.sort_unstable();
                sorted.dedup();
                if sorted.len() != n || n != layout.active_count() {
                    duplicates += 1;
                }
                if got.iter().any(|&x| x >= model.entries() as u64) {
                    out_of_range += 1;
                }
            }
        }
        gates.report(
            &format!("features-vs-reference[{spec}]"),
            mismatches == 0 && duplicates == 0 && out_of_range == 0,
            format!("{} states x 5 phases: {mismatches} mismatches, {duplicates} duplicate/short sets, {out_of_range} out of range; {} entries", states.len(), model.entries()),
        );
    }

    // 3b. Fill buckets against the accessor reference, both modes, on the
    // sampled states and on their mirrors (the bucket must be
    // reflection-invariant); and every bucket of both modes is reached by
    // random play, so the promotion gate below exercises all of them.
    for (mode, name) in [(FillMode::Occupancy5, "occ5"), (FillMode::Height5, "hgt5")] {
        let layout = Layout::parse(&format!("cols,fill={name}")).unwrap();
        let mut mismatches = 0usize;
        let mut mirror_mismatches = 0usize;
        let mut seen = [0usize; 5];
        for state in &states {
            let canonical = if state.board.mirrored_is_smaller() { state.board.mirrored() } else { state.board };
            let got = mode.bucket(&state.board);
            if got as u64 != fill_bucket_ref(&layout, &canonical) {
                mismatches += 1;
            }
            if got != mode.bucket(&state.board.mirrored()) {
                mirror_mismatches += 1;
            }
            seen[got] += 1;
        }
        let all_reached = seen.iter().all(|&n| n > 0);
        gates.report(
            &format!("fill-bucket-vs-reference[{name}]"),
            mismatches == 0 && mirror_mismatches == 0 && all_reached,
            format!("{} states: {mismatches} mismatches, {mirror_mismatches} mirror mismatches; states per bucket {seen:?} (all reached {all_reached})", states.len()),
        );
    }

    // 3c. Promotion preserves the value: a small model trained for a few
    // games, promoted into each fill mode, values every sampled state at
    // every phase bit-identically; the promoted model has five times the
    // entries, fresh accumulators, and moves apart under one update.
    {
        let small = Layout::parse("rows,win32,phase=cols").unwrap();
        let trained = Model::new(small, 20.0, true);
        {
            let mut scratch = [0u64; MAX_ACTIVE];
            let mut prev = [0u64; MAX_ACTIVE];
            let mut next = [0u64; MAX_ACTIVE];
            let mut stats = UpdateStats::default();
            let mut sink = FullWaveSink::new();
            for game in 0..6u32 {
                let seed = probe_start.wrapping_add(0x300 + game);
                let mut state = State::initial_headless(seed);
                let mut n_prev = trained.features(&state.board, state.moves_remaining, &mut prev);
                while !state.game_over && state.moves_played < 120 {
                    let d = choose(&trained, &state, &params_default(), &mut scratch);
                    if d.action < 0 {
                        break;
                    }
                    sink.clear();
                    let Some(result) = play_headless_move(&mut state, seed, d.action as usize, &mut sink) else { break };
                    let reward = result.score_delta as f32 / 17_000.0;
                    let target = if state.game_over {
                        reward
                    } else {
                        let n = trained.features(&state.board, state.moves_remaining, &mut next);
                        reward + trained.value_of(&next[..n])
                    };
                    trained.update(&prev[..n_prev], target, 1.0, 30.0, &mut stats);
                    if !state.game_over {
                        std::mem::swap(&mut prev, &mut next);
                        n_prev = small.active_count();
                    }
                }
            }
        }
        for name in ["occ5", "hgt5"] {
            let layout = Layout::parse(&format!("rows,win32,phase=cols,fill={name}")).unwrap();
            let promoted = Model::promote(&trained, layout).expect("promotion");
            let mut scratch = [0u64; MAX_ACTIVE];
            let mut mismatches = 0usize;
            for state in &states {
                for mtr in 1..=5 {
                    if promoted.value(&state.board, mtr, &mut scratch).to_bits() != trained.value(&state.board, mtr, &mut scratch).to_bits() {
                        mismatches += 1;
                    }
                }
            }
            let five_times = promoted.entries() == 5 * trained.entries();
            let fresh = promoted.touched_entries() == 0 && promoted.trainable();
            // One update in the bucket of the first sampled state moves that
            // state and leaves a sampled state of another bucket unchanged.
            let first = &states[0];
            let other = states.iter().find(|s| layout.fill.bucket(&s.board) != layout.fill.bucket(&first.board));
            let separated = match other {
                Some(other) => {
                    let before_other = promoted.value(&other.board, 3, &mut scratch).to_bits();
                    let before_first = promoted.value(&first.board, 3, &mut scratch);
                    let n = promoted.features(&first.board, 3, &mut scratch);
                    let mut stats = UpdateStats::default();
                    promoted.update(&scratch[..n], before_first + 5.0, 1.0, 100.0, &mut stats);
                    promoted.value(&other.board, 3, &mut scratch).to_bits() == before_other
                        && promoted.value(&first.board, 3, &mut scratch) > before_first
                }
                None => false,
            };
            gates.report(
                &format!("promotion-preserves-value[{name}]"),
                mismatches == 0 && five_times && fresh && separated,
                format!("{} states x 5 phases: {mismatches} value mismatches after promotion; entries x5 {five_times}; fresh accumulators {fresh}; buckets separate under one update {separated}", states.len()),
            );
        }
    }

    let layout = Layout::parse(&layout_spec).unwrap();
    let model = Arc::new(Model::new(layout, 20.0, false));
    let params = PolicyParams::default();

    // 4. Information boundary: score, level, moves played and the visible
    // next disc do not change the leaf value; score/level/moves played do
    // not change the direct decision.
    {
        let mut leaf = NTupleLeaf::new(model.clone());
        let mut scratch = [0u64; MAX_ACTIVE];
        let mut value_changes = 0usize;
        let mut action_changes = 0usize;
        for state in &states {
            let base = leaf.value(state).to_bits();
            let base_action = choose(&model, state, &params, &mut scratch).action;
            for variant in 0..4 {
                let mut altered = *state;
                match variant {
                    0 => altered.score += 123_456,
                    1 => altered.level += 7,
                    2 => altered.moves_played += 99,
                    _ => altered.next_disc = (altered.next_disc % 7) + 1,
                }
                if leaf.value(&altered).to_bits() != base {
                    value_changes += 1;
                }
                if variant < 3 && choose(&model, &altered, &params, &mut scratch).action != base_action {
                    action_changes += 1;
                }
            }
        }
        gates.report("information-boundary", value_changes == 0 && action_changes == 0, format!("{} states x 4 hidden-field perturbations: {value_changes} leaf-value changes, {action_changes} decision changes", states.len()));
    }

    // 5. Reflection: mirrored boards share the value; decisions mirror when
    // the best column is unique.
    {
        let mut scratch = [0u64; MAX_ACTIVE];
        let mut value_mismatch = 0usize;
        let mut decision_mismatch = 0usize;
        let mut checked = 0usize;
        for state in &states {
            let mut mirrored = *state;
            mirrored.board = state.board.mirrored();
            let a = model.value(&state.board, state.moves_remaining, &mut scratch).to_bits();
            let b = model.value(&mirrored.board, state.moves_remaining, &mut scratch).to_bits();
            if a != b {
                value_mismatch += 1;
            }
            if state.game_over {
                continue;
            }
            checked += 1;
            let d = choose(&model, state, &params, &mut scratch).action;
            let m = choose(&model, &mirrored, &params, &mut scratch).action;
            if d < 0 || m != 6 - d {
                decision_mismatch += 1;
            }
        }
        gates.report("reflection", value_mismatch == 0 && decision_mismatch == 0, format!("{} states: {value_mismatch} value mismatches; {checked} live states, {decision_mismatch} decision mismatches under mirroring", states.len()));
    }

    // 6. Direct-policy legality.
    {
        let mut scratch = [0u64; MAX_ACTIVE];
        let mut illegal = 0usize;
        let mut terminal = 0usize;
        for state in &states {
            let d = choose(&model, state, &params, &mut scratch);
            if state.game_over {
                terminal += 1;
                if d.action >= 0 {
                    illegal += 1;
                }
                continue;
            }
            let any_legal = (0..7).any(|c| state.board.is_legal(c));
            if (d.action < 0) == any_legal || (d.action >= 0 && !state.board.is_legal(d.action as usize)) {
                illegal += 1;
            }
        }
        gates.report("direct-policy-legality", illegal == 0, format!("{} states ({terminal} terminal), {illegal} illegal or missing decisions", states.len()));
    }

    // 7. Leaf inside the deployment search: legal, complete, deterministic,
    // worker-count independent.  On the frozen tables when --weights is given.
    let search_model = frozen.clone().unwrap_or_else(|| model.clone());
    let tables_label = if frozen.is_some() { "frozen tables" } else { "untrained tables" };
    {
        let seeds: Vec<u32> = (0..4).map(|g| probe_start.wrapping_add(0x100 + g)).collect();
        let arm = Arm::NTupleD3(search_model.clone());
        let one = evaluate_arm(&arm, &seeds, 1, 80);
        let four = evaluate_arm(&arm, &seeds, 4, 80);
        let again = evaluate_arm(&arm, &seeds, 4, 80);
        let same = one.iter().zip(four.iter()).zip(again.iter()).all(|((a, b), c)| {
            a.score == b.score && a.moves == b.moves && a.work == b.work && b.score == c.score && b.work == c.work
        });
        let clean = one.iter().all(|g| g.illegal_decisions == 0 && g.incomplete_decisions == 0);
        gates.report("leaf-in-search-determinism", same && clean, format!("{tables_label}, 4 games x (1, 4, 4 workers): identical {same}, illegal/incomplete-free {clean}; mean {:.0} over 80-move caps", one.iter().map(|g| g.score as f64).sum::<f64>() / 4.0));
    }

    // 7b. The same leaf inside the reference depth-4 search: legal, complete
    // (the d4s7 work bound guarantees completion), deterministic and
    // worker-count independent; and the depth-4 arm is not the depth-3 arm
    // (the two searches must differ in work on every game).
    {
        let seeds: Vec<u32> = (0..2).map(|g| probe_start.wrapping_add(0x100 + g)).collect();
        let started = Instant::now();
        let arm = Arm::NTupleD4(search_model.clone());
        let one = evaluate_arm(&arm, &seeds, 1, 40);
        let two = evaluate_arm(&arm, &seeds, 2, 40);
        let again = evaluate_arm(&arm, &seeds, 2, 40);
        let same = one.iter().zip(two.iter()).zip(again.iter()).all(|((a, b), c)| {
            a.score == b.score && a.moves == b.moves && a.work == b.work && b.score == c.score && b.work == c.work
        });
        let clean = one.iter().all(|g| g.illegal_decisions == 0 && g.incomplete_decisions == 0);
        let shallow = evaluate_arm(&Arm::NTupleD3(search_model.clone()), &seeds, 2, 40);
        let deeper = one.iter().zip(shallow.iter()).all(|(d4, d3)| d4.work > d3.work);
        gates.report(
            "leaf-in-d4-search-determinism",
            same && clean && deeper,
            format!(
                "{tables_label}, 2 games x (1, 2, 2 workers) over 40-move caps: identical {same}, illegal/incomplete-free {clean}, more work than depth 3 on every game {deeper}; work {} vs {} at depth 3; {:.0} s",
                one.iter().map(|g| g.work).sum::<u64>(),
                shallow.iter().map(|g| g.work).sum::<u64>(),
                started.elapsed().as_secs_f64()
            ),
        );
    }

    // 8. Serial training determinism: two identical single-thread runs give
    // identical tables; the optimistic start values every board at the
    // declared total.
    {
        let small = Layout::parse("rows,win32").unwrap();
        let mut fingerprints = Vec::new();
        for _ in 0..2 {
            let trained = Model::new(small, 20.0, true);
            let mut scratch = [0u64; MAX_ACTIVE];
            let mut prev = [0u64; MAX_ACTIVE];
            let mut next = [0u64; MAX_ACTIVE];
            let mut stats = UpdateStats::default();
            let mut sink = FullWaveSink::new();
            for game in 0..12u32 {
                let seed = probe_start.wrapping_add(0x200 + game);
                let mut state = State::initial_headless(seed);
                let mut n_prev = trained.features(&state.board, state.moves_remaining, &mut prev);
                while !state.game_over && state.moves_played < 200 {
                    let d = choose(&trained, &state, &params, &mut scratch);
                    if d.action < 0 {
                        break;
                    }
                    sink.clear();
                    let Some(result) = play_headless_move(&mut state, seed, d.action as usize, &mut sink) else { break };
                    let reward = result.score_delta as f32 / 17_000.0;
                    let target = if state.game_over {
                        reward
                    } else {
                        let n = trained.features(&state.board, state.moves_remaining, &mut next);
                        reward + trained.value_of(&next[..n])
                    };
                    trained.update(&prev[..n_prev], target, 1.0, 30.0, &mut stats);
                    if !state.game_over {
                        std::mem::swap(&mut prev, &mut next);
                        n_prev = small.active_count();
                    }
                }
            }
            fingerprints.push((trained.fingerprint(), stats.updates, trained.touched_entries()));
        }
        let fresh = Model::new(small, 20.0, false);
        let mut scratch = [0u64; MAX_ACTIVE];
        let start_value = fresh.value(&Board::initial(), 5, &mut scratch);
        gates.report(
            "serial-training-determinism",
            fingerprints[0] == fingerprints[1] && (start_value - 20.0).abs() < 1e-3,
            format!("two 12-game single-thread runs: fingerprints {:016x} / {:016x}, {} updates, {} touched entries; fresh initial-board value {start_value:.4} (declared 20)", fingerprints[0].0, fingerprints[1].0, fingerprints[0].1, fingerprints[0].2),
        );
    }

    // 9. Finiteness under random weights and the memory/time profile of the
    // candidate layout (informational timings).
    {
        let mut scratch = [0u64; MAX_ACTIVE];
        let finite = states.iter().all(|s| model.value(&s.board, s.moves_remaining, &mut scratch).is_finite());
        let started = Instant::now();
        let mut sink = 0u64;
        let reps = 200;
        for _ in 0..reps {
            for s in &states {
                let n = model.features(&s.board, s.moves_remaining, &mut scratch);
                sink = sink.wrapping_add(scratch[n - 1] as u64);
            }
        }
        let feature_ns = started.elapsed().as_nanos() as f64 / (reps * states.len()) as f64;
        let started = Instant::now();
        let mut acc = 0.0f32;
        for _ in 0..reps {
            for s in &states {
                acc += model.value(&s.board, s.moves_remaining, &mut scratch);
            }
        }
        let value_ns = started.elapsed().as_nanos() as f64 / (reps * states.len()) as f64;
        let started = Instant::now();
        let mut sims = 0u64;
        for s in &states {
            sims += choose(&model, s, &params, &mut scratch).simulations as u64;
        }
        let decision_us = started.elapsed().as_micros() as f64 / states.len() as f64;
        gates.report(
            "finite-and-profile",
            finite,
            format!(
                "values finite {finite}; {} entries = {:.2} GB frozen, {:.2} GB trainable; per state: features {feature_ns:.0} ns, value {value_ns:.0} ns (untrained tables, hot); direct decision {decision_us:.1} us ({:.1} simulations) [sink {sink} {acc:.1}]",
                model.entries(),
                model.entries() as f64 * 4.0 / 1e9,
                model.entries() as f64 * 12.0 / 1e9,
                sims as f64 / states.len() as f64
            ),
        );
    }

    println!("{}", if gates.failures == 0 { "ALL GATES PASSED" } else { "GATE FAILURES" });
    std::process::exit(if gates.failures == 0 { 0 } else { 1 });
}
