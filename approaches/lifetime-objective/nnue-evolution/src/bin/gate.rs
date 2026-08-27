// CHECK-tier gates for the NNUE leaf and its deployment search.  No gameplay
// tier may start until every gate passes.  Probe states are harvested from
// the already-opened development block 0xa5276000-0xa5277fff (the range the
// Rust engine's own gates and benchmarks used); these gates open no new
// seeds and make no strength claim.
//
// Gates:
//   features        135 active indices, in bounds, deterministic build
//   info-boundary   score/level/moves_played blindness: identical features
//                   and identical value bits when only non-public fields differ
//   reflection      mirrored roots give mirrored actions and identical
//                   canonical per-column value bits under the NNUE leaf
//   determinism     fresh searchers reproduce per-column value bits; the
//                   parallel evaluator reproduces scores at 1 and 4 workers
//   legality        random, all-zero and saturated weights all complete
//                   depth 3 and choose legal columns on every probe root;
//                   two complete games with random weights stay legal
//   finiteness      leaf values are finite across fuzzed mid-game states
//   roundtrip       save/load preserves value bits
//
// Usage: gate <probe-seed-base-hex> [--quick]

use drop7_nnue_evolution::features::{self, ACTIVE};
use drop7_nnue_evolution::game::{evaluate_tasks, EvalLeaf, EvalTask};
use drop7_nnue_evolution::nnue::{Nnue, NnueLeaf};
use drop7_nnue_evolution::{deployment_params, DEPLOYMENT_TABLE};
use drop7_rs::board::{Board, BOARD_SIZE};
use drop7_rs::engine::{play_headless_move, FullWaveSink, State};
use drop7_rs::rng::Mulberry32;
use drop7_rs::search::{work_bound_for, Searcher};

struct Probe {
    states: Vec<State>,
}

/// Harvest real mid-game states: play a fixed-seed random legal-column
/// policy from each probe seed and snapshot the state before every decision.
/// Random play breaks board symmetry early (the reflection gate wants
/// asymmetric boards) and covers early, mid and post-rise states without
/// opening anything beyond the already-read probe block.
fn harvest_probes(base: u32, seeds: usize, moves_each: usize) -> Probe {
    let mut states = Vec::new();
    let mut rng = Mulberry32::new(0x0e701_e56);
    for index in 0..seeds {
        let seed = base + index as u32;
        let mut state = State::initial_headless(seed);
        let mut sink = FullWaveSink::new();
        for _ in 0..moves_each {
            if state.game_over {
                break;
            }
            states.push(state);
            let legal: Vec<usize> = (0..BOARD_SIZE).filter(|&c| state.board.is_legal(c)).collect();
            if legal.is_empty() {
                break;
            }
            let column = legal[(rng.next_bits() as usize) % legal.len()];
            if play_headless_move(&mut state, seed, column, &mut sink).is_none() {
                break;
            }
        }
    }
    Probe { states }
}

fn gate_features(probe: &Probe) -> Result<(), String> {
    for state in &probe.states {
        let mut a = [0u16; ACTIVE];
        let mut b = [0u16; ACTIVE];
        features::build(state, &mut a);
        features::build(state, &mut b);
        if a != b {
            return Err("feature build is not deterministic".into());
        }
        if a.iter().any(|&f| f as usize >= features::FEATURES) {
            return Err("feature index out of range".into());
        }
    }
    println!("gate features: ok ({} states)", probe.states.len());
    Ok(())
}

fn gate_info_boundary(probe: &Probe, net: &Nnue) -> Result<(), String> {
    for state in &probe.states {
        let mut disguised = *state;
        disguised.score = 9_999_999;
        disguised.level = 42;
        disguised.moves_played = 4242;
        let mut fa = [0u16; ACTIVE];
        let mut fb = [0u16; ACTIVE];
        features::build(state, &mut fa);
        features::build(&disguised, &mut fb);
        if fa != fb {
            return Err("features read a non-public field".into());
        }
        if net.value_of_state(state).to_bits() != net.value_of_state(&disguised).to_bits() {
            return Err("NNUE value reads a non-public field".into());
        }
    }
    println!(
        "gate info-boundary: ok ({} states, score/level/moves_played blind)",
        probe.states.len()
    );
    Ok(())
}

fn gate_reflection(probe: &Probe, net: &Nnue, roots: usize) -> Result<(), String> {
    let params = deployment_params();
    let step = (probe.states.len() / roots).max(1);
    let mut checked = 0;
    let mut symmetric = 0;
    for state in probe.states.iter().step_by(step).take(roots) {
        let mut mirrored = *state;
        mirrored.board = state.board.mirrored();
        // finding-13's exclusion: on a horizontally symmetric board the
        // mirror *is* the board, canonicalisation returns the same state and
        // the deterministic column-order tie-break returns the same column
        // rather than its reflection.  The frozen reference behaves
        // identically, so symmetric boards are excluded from the 6-action
        // check and counted.
        if mirrored.board == state.board {
            symmetric += 1;
            continue;
        }
        let mut a = Searcher::new(params, NnueLeaf { net: net.clone() }, drop7_rs::search::NoTable);
        let mut b = Searcher::new(params, NnueLeaf { net: net.clone() }, drop7_rs::search::NoTable);
        let (values_a, action_a) = a.column_values(state, params.depth);
        let (values_b, action_b) = b.column_values(&mirrored, params.depth);
        if values_a.len() != values_b.len() {
            return Err("mirrored root has a different legal-column count".into());
        }
        for ((ca, va), (cb, vb)) in values_a.iter().zip(values_b.iter()) {
            if ca != cb || va.to_bits() != vb.to_bits() {
                return Err("canonical per-column values differ under reflection".into());
            }
        }
        let expect = if action_a >= 0 {
            BOARD_SIZE as i32 - 1 - action_a
        } else {
            -1
        };
        if action_b != expect {
            return Err(format!(
                "mirrored action {action_b} != {expect} (from {action_a})"
            ));
        }
        checked += 1;
    }
    println!(
        "gate reflection: ok ({checked} asymmetric mirrored root pairs, {symmetric} symmetric excluded, d3s7)"
    );
    Ok(())
}

fn gate_determinism(probe: &Probe, net: &Nnue, seed_base: u32) -> Result<(), String> {
    let params = deployment_params();
    // Fresh searchers, same root: identical value bits.
    let state = &probe.states[probe.states.len() / 2];
    let mut a = Searcher::new(params, NnueLeaf { net: net.clone() }, drop7_rs::search::NoTable);
    let mut b = Searcher::new(params, NnueLeaf { net: net.clone() }, drop7_rs::search::NoTable);
    let (va, aa) = a.column_values(state, params.depth);
    let (vb, ab) = b.column_values(state, params.depth);
    if aa != ab
        || va.len() != vb.len()
        || va.iter().zip(vb.iter()).any(|(x, y)| x.1.to_bits() != y.1.to_bits())
    {
        return Err("fresh searchers disagree on the same root".into());
    }
    // Worker-count independence of the parallel evaluator: one candidate,
    // four probe games, 1 vs 4 workers must give identical records.
    let tasks: Vec<EvalTask> = (0..4)
        .map(|i| EvalTask {
            individual: 0,
            seed: seed_base + 0x40 + i,
        })
        .collect();
    let leaf_for = |_index: usize| EvalLeaf::Nnue(NnueLeaf { net: net.clone() });
    let one = evaluate_tasks(&leaf_for, &params, DEPLOYMENT_TABLE, &tasks, 1, 200);
    let four = evaluate_tasks(&leaf_for, &params, DEPLOYMENT_TABLE, &tasks, 4, 200);
    for (x, y) in one.iter().zip(four.iter()) {
        if x.score != y.score || x.moves != y.moves || x.work != y.work {
            return Err(format!(
                "worker-count dependence at seed {:#x}: {} vs {}",
                x.seed, x.score, y.score
            ));
        }
    }
    println!("gate determinism: ok (fresh-searcher bits, 1-vs-4 worker equality)");
    Ok(())
}

fn gate_legality(probe: &Probe, seed_base: u32) -> Result<(), String> {
    let params = deployment_params();
    let mut variants: Vec<(&str, Nnue)> = vec![
        ("random", Nnue::random(0x0e701_e53)),
        ("zeros", Nnue::from_flat(&vec![0.0; Nnue::parameter_count()])),
        (
            "saturated",
            Nnue::from_flat(&vec![1.0e4; Nnue::parameter_count()]),
        ),
    ];
    let step = (probe.states.len() / 24).max(1);
    for (name, net) in variants.drain(..) {
        for state in probe.states.iter().step_by(step) {
            let mut searcher = Searcher::new(
                params,
                NnueLeaf { net: net.clone() },
                drop7_rs::search::NoTable,
            );
            let (action, metrics) = searcher.choose_action(state);
            if action < 0 || !state.board.is_legal(action as usize) {
                return Err(format!("{name} leaf chose illegal action {action}"));
            }
            if metrics.completed_depth != params.depth {
                return Err(format!(
                    "{name} leaf completed depth {} != {}",
                    metrics.completed_depth, params.depth
                ));
            }
            if metrics.work > work_bound_for(params.depth, params.chance_samples) {
                return Err(format!("{name} leaf exceeded the work bound"));
            }
        }
        // One complete game per variant.
        let mut searcher = Searcher::new(
            params,
            NnueLeaf { net },
            drop7_rs::search::DepthTable::new(DEPLOYMENT_TABLE, 1),
        );
        let game = drop7_nnue_evolution::game::play_game(
            &mut searcher,
            seed_base + 0x50,
            drop7_nnue_evolution::game::MOVE_CAP,
            params.depth,
        );
        if game.illegal_decisions != 0 || game.incomplete_decisions != 0 {
            return Err(format!(
                "{name} leaf game had {} illegal, {} incomplete decisions",
                game.illegal_decisions, game.incomplete_decisions
            ));
        }
        println!(
            "gate legality[{name}]: ok ({} probe roots + 1 game, score {}, moves {})",
            probe.states.iter().step_by(step).count(),
            game.score,
            game.moves
        );
    }
    Ok(())
}

fn gate_finiteness(probe: &Probe, net: &Nnue) -> Result<(), String> {
    let mut rng = Mulberry32::new(0x0e701_e54);
    let mut checked = 0u32;
    for state in &probe.states {
        // Perturb each probe state: random legal column drops with random
        // next discs, without advancing the real game stream.
        let mut board: Board = state.board;
        for _ in 0..3 {
            let legal: Vec<usize> = (0..BOARD_SIZE).filter(|&c| board.is_legal(c)).collect();
            if legal.is_empty() {
                break;
            }
            let column = legal[(rng.next_bits() as usize) % legal.len()];
            let disc = (rng.next_bits() % 7 + 1) as u8;
            board.place_disc(column, disc);
        }
        let mut fuzzed = *state;
        fuzzed.board = board;
        let value = net.value_of_state(&fuzzed);
        if !value.is_finite() {
            return Err("non-finite NNUE value on a fuzzed state".into());
        }
        checked += 1;
    }
    println!("gate finiteness: ok ({checked} fuzzed states)");
    Ok(())
}

/// The teacher corpus records the argmax of column_values (fixed full depth,
/// unbounded work) as the teacher's action; the deployed references use
/// choose_action (iterative deepening under the completion-guaranteeing
/// bound).  Pin that the two agree on probe roots, at the deployment depth
/// and at the teacher depth.
fn gate_teacher_equivalence(probe: &Probe) -> Result<(), String> {
    use drop7_rs::search::{FairLeaf, SearchParams};
    let step = (probe.states.len() / 6).max(1);
    for (depth, strata) in [(3, 7), (5, 7)] {
        let bounded = SearchParams {
            depth,
            chance_samples: strata,
            terminal_utility: -1_000_000.0,
            maximum_work: work_bound_for(depth, strata) + 1,
            policy_seed: 0xd707_5eed,
        };
        let mut unbounded = bounded;
        unbounded.maximum_work = u64::MAX;
        let roots = if depth == 5 { 1 } else { 6 };
        for state in probe.states.iter().step_by(step).take(roots) {
            let mut a = Searcher::new(bounded, FairLeaf::default(), drop7_rs::search::NoTable);
            let mut b = Searcher::new(unbounded, FairLeaf::default(), drop7_rs::search::NoTable);
            let (action_bounded, metrics) = a.choose_action(state);
            let (_values, action_unbounded) = b.column_values(state, depth);
            if metrics.completed_depth != depth {
                return Err(format!("bounded search did not complete depth {depth}"));
            }
            if action_bounded != action_unbounded {
                return Err(format!(
                    "teacher action mismatch at depth {depth}: {action_bounded} vs {action_unbounded}"
                ));
            }
        }
        println!("gate teacher-equivalence: ok (depth {depth})");
    }
    Ok(())
}

fn gate_roundtrip(probe: &Probe, net: &Nnue) -> Result<(), String> {
    // Per-process unique path: a predictable shared tempfile would let
    // concurrent gate runs overwrite each other's artifact (and the
    // repository forbids new shared /tmp defaults).
    let unique = format!(
        "drop7-nnue-gate-roundtrip-{}-{}.bin",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.subsec_nanos())
            .unwrap_or(0)
    );
    let path = std::env::temp_dir().join(unique);
    net.save(&path).map_err(|e| e.to_string())?;
    let back = Nnue::load(&path);
    std::fs::remove_file(&path).ok();
    let back = back?;
    for state in probe.states.iter().step_by((probe.states.len() / 32).max(1)) {
        if net.value_of_state(state).to_bits() != back.value_of_state(state).to_bits() {
            return Err("save/load changed a value".into());
        }
    }
    println!("gate roundtrip: ok");
    Ok(())
}

fn main() -> Result<(), String> {
    let args: Vec<String> = std::env::args().collect();
    let base = u32::from_str_radix(
        args.get(1).map(|s| s.as_str()).unwrap_or("0xa5277000").trim_start_matches("0x"),
        16,
    )
    .map_err(|_| "bad seed base".to_string())?;
    let quick = args.iter().any(|a| a == "--quick");
    let seeds = if quick { 2 } else { 8 };
    let moves = if quick { 12 } else { 40 };
    let probe = harvest_probes(base, seeds, moves);
    println!("harvested {} probe states from {base:#010x}+", probe.states.len());

    // The gates run against a seeded random-weight network: they check
    // mechanics, not strength, so any fixed weights do.
    let net = Nnue::random(0x0e701_e55);
    gate_features(&probe)?;
    gate_info_boundary(&probe, &net)?;
    gate_reflection(&probe, &net, if quick { 3 } else { 12 })?;
    gate_determinism(&probe, &net, base)?;
    gate_legality(&probe, base)?;
    gate_finiteness(&probe, &net)?;
    gate_teacher_equivalence(&probe)?;
    gate_roundtrip(&probe, &net)?;
    println!("ALL GATES PASSED");
    Ok(())
}
