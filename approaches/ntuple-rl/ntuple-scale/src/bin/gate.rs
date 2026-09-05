// CHECK-tier gates for the n-tuple leaf.  No leased seed is read: random
// boards are reached by random legal play from the already-opened Rust-engine
// probe block (0xa5277000+, SEEDLEASE-A52-FAST), exactly as the nnue-evolution
// gates did.  Prints one PASS/FAIL line per gate and exits non-zero on any
// failure.
//
// Usage: gate [--probe-start 0xa5277000] [--layout SPEC]

use std::sync::Arc;
use std::time::Instant;

use drop7_ntuple_scale::game::{evaluate_arm, Arm};
use drop7_ntuple_scale::model::{Layout, Model, UpdateStats, MAX_ACTIVE};
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

/// Reference feature construction through the cell accessor and Horner
/// base-10 codes, mirroring model.rs's documented layout independently.
fn features_ref(layout: &Layout, board: &Board, moves_remaining: i32) -> Vec<u64> {
    let canonical = if board.mirrored_is_smaller() { board.mirrored() } else { *board };
    let get = |r: usize, c: usize| canonical.get(r, c) as u64;
    let phase = (moves_remaining - 1) as u64;
    let mut out = Vec::new();
    let mut base = 0u64;
    let line = 10_000_000u64;
    let window = 1_000_000u64;
    let phases_all = layout.phase == drop7_ntuple_scale::model::PhaseMode::All;
    let phases_cols = layout.phase != drop7_ntuple_scale::model::PhaseMode::None;
    if layout.rows {
        let size = if phases_all { 5 * line } else { line };
        for r in 0..BOARD_SIZE {
            let mut code = 0u64;
            for c in (0..BOARD_SIZE).rev() {
                code = code * 10 + get(r, c);
            }
            let off = if phases_all { phase * line } else { 0 };
            out.push(base + r as u64 * size + off + code);
        }
        base += 7 * size;
    }
    if layout.cols {
        let size = if phases_cols { 5 * line } else { line };
        for c in 0..BOARD_SIZE {
            // nibble n = row 6-n is the least significant digit.
            let mut code = 0u64;
            for r in 0..BOARD_SIZE {
                code = code * 10 + get(r, c);
            }
            let off = if phases_cols { phase * line } else { 0 };
            out.push(base + c as u64 * size + off + code);
        }
        base += 7 * size;
    }
    if layout.win23 {
        let size = if phases_all { 5 * window } else { window };
        let mut placement = 0u64;
        for c in 0..BOARD_SIZE - 1 {
            for top in 0..=4usize {
                // chunk3 of a column: rows top, top+1, top+2 with top+2 least
                // significant; the right column is scaled by 1000.
                let left = get(top, c) * 100 + get(top + 1, c) * 10 + get(top + 2, c);
                let right = get(top, c + 1) * 100 + get(top + 1, c + 1) * 10 + get(top + 2, c + 1);
                let off = if phases_all { phase * window } else { 0 };
                out.push(base + placement * size + off + left + 1000 * right);
                placement += 1;
            }
        }
        base += 30 * size;
    }
    if layout.win32 {
        let size = if phases_all { 5 * window } else { window };
        let mut placement = 0u64;
        for c in 0..BOARD_SIZE - 2 {
            for top in 0..=5usize {
                let a = get(top, c) * 10 + get(top + 1, c);
                let b = get(top, c + 1) * 10 + get(top + 1, c + 1);
                let d = get(top, c + 2) * 10 + get(top + 1, c + 2);
                let off = if phases_all { phase * window } else { 0 };
                out.push(base + placement * size + off + a + 100 * b + 10_000 * d);
                placement += 1;
            }
        }
    }
    out
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut probe_start = 0xa527_7000u32;
    let mut layout_spec = "rows,cols,win23,win32,phase=cols".to_string();
    let mut i = 1;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--probe-start" => probe_start = u32::from_str_radix(args[i + 1].trim_start_matches("0x"), 16).expect("hex"),
            "--layout" => layout_spec = args[i + 1].clone(),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    let mut gates = Gates { failures: 0 };
    println!("gate: probe block 0x{probe_start:08x}, candidate layout {layout_spec}");

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
    for spec in ["rows", "cols", "win23", "win32", "rows,cols,win23,win32,phase=none", "rows,cols,win23,win32,phase=cols", "rows,cols,win23,win32,phase=all"] {
        let layout = Layout::parse(spec).unwrap();
        let model = Model::new(layout, 1.0, false);
        let mut out = [0u32; MAX_ACTIVE];
        let mut mismatches = 0usize;
        let mut duplicates = 0usize;
        let mut out_of_range = 0usize;
        for state in &states {
            for mtr in 1..=5 {
                let n = model.features(&state.board, mtr, &mut out);
                let expected = features_ref(&layout, &state.board, mtr);
                let got: Vec<u64> = out[..n].iter().map(|&x| x as u64).collect();
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

    let layout = Layout::parse(&layout_spec).unwrap();
    let model = Arc::new(Model::new(layout, 20.0, false));
    let params = PolicyParams::default();

    // 4. Information boundary: score, level, moves played and the visible
    // next disc do not change the leaf value; score/level/moves played do
    // not change the direct decision.
    {
        let mut leaf = NTupleLeaf::new(model.clone());
        let mut scratch = [0u32; MAX_ACTIVE];
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
        let mut scratch = [0u32; MAX_ACTIVE];
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
        let mut scratch = [0u32; MAX_ACTIVE];
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
    // worker-count independent.
    {
        let seeds: Vec<u32> = (0..4).map(|g| probe_start.wrapping_add(0x100 + g)).collect();
        let arm = Arm::NTupleD3(model.clone());
        let one = evaluate_arm(&arm, &seeds, 1, 80);
        let four = evaluate_arm(&arm, &seeds, 4, 80);
        let again = evaluate_arm(&arm, &seeds, 4, 80);
        let same = one.iter().zip(four.iter()).zip(again.iter()).all(|((a, b), c)| {
            a.score == b.score && a.moves == b.moves && a.work == b.work && b.score == c.score && b.work == c.work
        });
        let clean = one.iter().all(|g| g.illegal_decisions == 0 && g.incomplete_decisions == 0);
        gates.report("leaf-in-search-determinism", same && clean, format!("4 games x (1, 4, 4 workers): identical {same}, illegal/incomplete-free {clean}; mean {:.0} over 80-move caps", one.iter().map(|g| g.score as f64).sum::<f64>() / 4.0));
    }

    // 8. Serial training determinism: two identical single-thread runs give
    // identical tables; the optimistic start values every board at the
    // declared total.
    {
        let small = Layout::parse("rows,win32").unwrap();
        let mut fingerprints = Vec::new();
        for _ in 0..2 {
            let trained = Model::new(small, 20.0, true);
            let mut scratch = [0u32; MAX_ACTIVE];
            let mut prev = [0u32; MAX_ACTIVE];
            let mut next = [0u32; MAX_ACTIVE];
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
        let mut scratch = [0u32; MAX_ACTIVE];
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
        let mut scratch = [0u32; MAX_ACTIVE];
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
