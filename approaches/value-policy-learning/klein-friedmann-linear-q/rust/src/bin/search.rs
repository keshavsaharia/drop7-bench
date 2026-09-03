// Cross-entropy search over the six feature weights against whole-game
// lifetime (EX kf-six-weight-cem).  The policy is invariant to positive
// scaling of the weights, so the frozen result is unit-L2-normalised.
//
//   search --seeds-start 0xHEX --generations G --population P --elite E
//          --games-per-gen K --select-games N [--move-cap 2000] [--threads T]
//          [--policy-seed 0xHEX] --weights OUT.txt --log OUT.json
//
// Generation g evaluates every candidate on the SAME K seeds
// (seeds-start + g*K ..), common random numbers across the population.  After
// G generations the final elites and the running best are re-selected on a
// fresh N-seed block that starts where the generations ended.

use std::time::Instant;

use drop7_kf_linear_q::features::FEATURE_COUNT;
use drop7_kf_linear_q::game::{evaluate_arm, GameRecord};
use drop7_kf_linear_q::learn::save_weights;
use drop7_kf_linear_q::policy::{LinearQ, Policy};
use drop7_rs::rng::{mix32, Mulberry32};

const SEARCH_DOMAIN: u32 = 0x4345_4d31; // "CEM1"

fn parse_hex(s: &str) -> u32 {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).expect("hex")
}

fn gaussian(rng: &mut Mulberry32) -> f64 {
    // Box-Muller on two unit draws; the first is kept away from zero.
    let u1 = (rng.next_unit()).max(1e-12);
    let u2 = rng.next_unit();
    (-2.0 * u1.ln()).sqrt() * (2.0 * std::f64::consts::PI * u2).cos()
}

fn normalised(w: &[f64; FEATURE_COUNT]) -> [f64; FEATURE_COUNT] {
    let norm = w.iter().map(|x| x * x).sum::<f64>().sqrt();
    let mut out = *w;
    if norm > 0.0 {
        for x in out.iter_mut() {
            *x /= norm;
        }
    }
    out
}

fn mean_moves(rows: &[GameRecord]) -> f64 {
    rows.iter().map(|g| g.moves as f64).sum::<f64>() / rows.len().max(1) as f64
}

fn mean_score(rows: &[GameRecord]) -> f64 {
    rows.iter().map(|g| g.score as f64).sum::<f64>() / rows.len().max(1) as f64
}

fn fitness(w: [f64; FEATURE_COUNT], seeds: &[u32], threads: usize, move_cap: i32) -> (f64, f64, i32) {
    let make = move || -> Box<dyn Policy> { Box::new(LinearQ::new("cem", w)) };
    let rows = evaluate_arm(&make, seeds, threads, move_cap);
    let illegal: i32 = rows.iter().map(|g| g.illegal_decisions).sum();
    (mean_moves(&rows), mean_score(&rows), illegal)
}

fn fmt(xs: &[f64]) -> String {
    xs.iter().map(|x| format!("{x}")).collect::<Vec<_>>().join(",")
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut seeds_start: Option<u32> = None;
    let mut generations = 30u32;
    let mut population = 64usize;
    let mut elite = 8usize;
    let mut games_per_gen = 256u32;
    let mut select_games = 1024u32;
    let mut move_cap = 2_000i32;
    let mut threads = 8usize;
    let mut policy_seed = 0x6b66_0010u32;
    let mut weights_out = String::new();
    let mut log_out = String::new();
    let mut i = 1;
    while i + 1 < args.len() {
        let v = args[i + 1].as_str();
        match args[i].as_str() {
            "--seeds-start" => seeds_start = Some(parse_hex(v)),
            "--generations" => generations = v.parse().expect("generations"),
            "--population" => population = v.parse().expect("population"),
            "--elite" => elite = v.parse().expect("elite"),
            "--games-per-gen" => games_per_gen = v.parse().expect("games-per-gen"),
            "--select-games" => select_games = v.parse().expect("select-games"),
            "--move-cap" => move_cap = v.parse().expect("move-cap"),
            "--threads" => threads = v.parse().expect("threads"),
            "--policy-seed" => policy_seed = parse_hex(v),
            "--weights" => weights_out = v.to_string(),
            "--log" => log_out = v.to_string(),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    if i < args.len() {
        panic!("dangling argument {:?}: every option takes a value", args[i]);
    }
    let seeds_start = seeds_start.expect("--seeds-start required");
    assert!(!weights_out.is_empty() && !log_out.is_empty(), "--weights and --log required");
    assert!(elite >= 2 && elite <= population, "elite must be in 2..=population");
    let search_end = seeds_start as u64 + generations as u64 * games_per_gen as u64;
    let select_end = search_end + select_games as u64;
    eprintln!(
        "search: fitness seeds 0x{seeds_start:08x}..0x{search_end:08x}, re-selection 0x{search_end:08x}..0x{select_end:08x}, population {population} elite {elite} generations {generations} cap {move_cap}"
    );

    let started = Instant::now();
    let mut rng = Mulberry32::new(mix32(policy_seed ^ SEARCH_DOMAIN));
    let mut mean = [0.0f64; FEATURE_COUNT];
    let mut sd = [1.0f64; FEATURE_COUNT];
    let mut best_ever: Option<([f64; FEATURE_COUNT], f64)> = None;
    let mut generation_log: Vec<String> = Vec::new();
    let mut last_elites: Vec<([f64; FEATURE_COUNT], f64)> = Vec::new();
    for g in 0..generations {
        let seeds: Vec<u32> = (0..games_per_gen)
            .map(|k| seeds_start.wrapping_add(g * games_per_gen + k))
            .collect();
        let mut scored: Vec<([f64; FEATURE_COUNT], f64, f64)> = Vec::with_capacity(population);
        for _ in 0..population {
            let mut w = [0.0f64; FEATURE_COUNT];
            for d in 0..FEATURE_COUNT {
                w[d] = mean[d] + sd[d] * gaussian(&mut rng);
            }
            let (moves, score, illegal) = fitness(w, &seeds, threads, move_cap);
            assert_eq!(illegal, 0, "illegal decision during search");
            scored.push((w, moves, score));
        }
        scored.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap());
        let elites = &scored[..elite];
        let gen_mean = scored.iter().map(|s| s.1).sum::<f64>() / population as f64;
        for d in 0..FEATURE_COUNT {
            let m = elites.iter().map(|e| e.0[d]).sum::<f64>() / elite as f64;
            let v = elites.iter().map(|e| (e.0[d] - m) * (e.0[d] - m)).sum::<f64>() / (elite as f64 - 1.0);
            mean[d] = m;
            sd[d] = v.sqrt().max(1e-3);
        }
        if best_ever.map_or(true, |(_, f)| scored[0].1 > f) {
            best_ever = Some((scored[0].0, scored[0].1));
        }
        last_elites = elites.iter().map(|e| (e.0, e.1)).collect();
        eprintln!(
            "gen {g:2}: best {:.2} moves ({:.0} pts) mean {:.2}; elite mean {:?}",
            scored[0].1,
            scored[0].2,
            gen_mean,
            normalised(&mean).map(|x| (x * 1000.0).round() / 1000.0)
        );
        generation_log.push(format!(
            "{{\"generation\":{g},\"seedsStartHex\":\"0x{:08x}\",\"bestMoves\":{},\"bestScore\":{},\"meanMoves\":{},\"eliteMean\":[{}],\"eliteSd\":[{}],\"bestWeights\":[{}]}}",
            seeds[0],
            scored[0].1,
            scored[0].2,
            gen_mean,
            fmt(&mean),
            fmt(&sd),
            fmt(&scored[0].0)
        ));
    }

    // Re-selection on a fresh block: final elites plus the running best.
    let select_seeds: Vec<u32> = (0..select_games)
        .map(|k| (search_end as u32).wrapping_add(k))
        .collect();
    let mut finalists: Vec<([f64; FEATURE_COUNT], f64)> = last_elites.clone();
    if let Some(b) = best_ever {
        finalists.push(b);
    }
    let mut selected: Vec<String> = Vec::new();
    let mut chosen: Option<([f64; FEATURE_COUNT], f64, f64, f64)> = None;
    for (index, (w, gen_fit)) in finalists.iter().enumerate() {
        let (moves, score, illegal) = fitness(*w, &select_seeds, threads, move_cap);
        assert_eq!(illegal, 0, "illegal decision during re-selection");
        selected.push(format!(
            "{{\"finalist\":{index},\"generationFitness\":{gen_fit},\"selectMeanMoves\":{moves},\"selectMeanScore\":{score},\"weights\":[{}]}}",
            fmt(w)
        ));
        if chosen.map_or(true, |c| moves > c.2) {
            chosen = Some((*w, *gen_fit, moves, score));
        }
    }
    let (w, gen_fit, sel_moves, sel_score) = chosen.expect("a finalist");
    let frozen = normalised(&w);
    save_weights(&weights_out, &frozen).expect("write weights");
    let wall = started.elapsed().as_secs_f64();
    let log = format!(
        "{{\"format\":\"drop7-kf-linear-q-search-v1\",\"seedsStartHex\":\"0x{seeds_start:08x}\",\"searchEndExclusiveHex\":\"0x{search_end:08x}\",\"selectEndExclusiveHex\":\"0x{select_end:08x}\",\"generations\":{generations},\"population\":{population},\"elite\":{elite},\"gamesPerGeneration\":{games_per_gen},\"selectGames\":{select_games},\"moveCap\":{move_cap},\"policySeedHex\":\"0x{policy_seed:08x}\",\"generationLog\":[{}],\"finalists\":[{}],\"chosen\":{{\"generationFitness\":{gen_fit},\"selectMeanMoves\":{sel_moves},\"selectMeanScore\":{sel_score},\"rawWeights\":[{}],\"frozenUnitWeights\":[{}]}},\"wallSeconds\":{wall}}}\n",
        generation_log.join(","),
        selected.join(","),
        fmt(&w),
        fmt(&frozen)
    );
    std::fs::write(&log_out, log).expect("write log");
    eprintln!(
        "search: done in {wall:.1}s; chosen finalist generation fitness {gen_fit:.2}, re-selection {sel_moves:.2} moves / {sel_score:.0} pts; frozen unit weights {:?}",
        frozen
    );
}
