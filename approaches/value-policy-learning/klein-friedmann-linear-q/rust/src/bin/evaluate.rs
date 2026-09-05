// Diagnostic cohort evaluation: several policies on identical seeds.
//
//   evaluate --seeds-start 0xHEX --games N [--move-cap 2000] [--threads T]
//            --out FILE --arm SPEC [--arm SPEC ...]
//
// SPEC is one of
//   random[:0xPOLICYSEED]   uniform over legal columns
//   center                  the engine's centre-first fallback
//   kf:NAME=WEIGHTS.txt     the six-feature linear Q policy with frozen weights
//   fair:DEPTH:STRATA[:GAMES]  fair expectimax (65,536-entry depth table),
//                           optionally on only the first GAMES seeds
// Rows are emitted in cohort order; wall time is the only field that can
// differ between repeated runs.

use std::time::Instant;

use drop7_kf_linear_q::game::{evaluate_arm, game_json, summarize, GameRecord};
use drop7_kf_linear_q::learn::load_weights;
use drop7_kf_linear_q::policy::{CenterFirst, FairSearch, LinearQ, Policy, RandomLegal};

fn parse_hex(s: &str) -> u32 {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).expect("hex")
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut seeds_start: Option<u32> = None;
    let mut games = 256usize;
    let mut move_cap = 2_000i32;
    let mut threads = 8usize;
    let mut out = String::new();
    let mut specs: Vec<String> = Vec::new();
    let mut i = 1;
    while i + 1 < args.len() {
        let v = args[i + 1].as_str();
        match args[i].as_str() {
            "--seeds-start" => seeds_start = Some(parse_hex(v)),
            "--games" => games = v.parse().expect("games"),
            "--move-cap" => move_cap = v.parse().expect("move-cap"),
            "--threads" => threads = v.parse().expect("threads"),
            "--out" => out = v.to_string(),
            "--arm" => specs.push(v.to_string()),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    if i < args.len() {
        panic!("dangling argument {:?}: every option takes a value", args[i]);
    }
    let seeds_start = seeds_start.expect("--seeds-start required");
    assert!(!out.is_empty() && !specs.is_empty(), "--out and at least one --arm required");
    let seeds: Vec<u32> = (0..games as u32).map(|g| seeds_start.wrapping_add(g)).collect();
    eprintln!(
        "evaluate: seeds 0x{:08x}..0x{:08x} (exclusive) cap {} threads {} arms {:?}",
        seeds_start,
        seeds_start as u64 + games as u64,
        move_cap,
        threads,
        specs
    );

    let mut arms_json: Vec<String> = Vec::new();
    for spec in &specs {
        let started = Instant::now();
        let (name, rows): (String, Vec<GameRecord>) = if spec == "center" {
            let make = || -> Box<dyn Policy> { Box::new(CenterFirst) };
            ("center".to_string(), evaluate_arm(&make, &seeds, threads, move_cap))
        } else if spec == "random" || spec.starts_with("random:") {
            let seed = if spec == "random" { 0x6b66_1000 } else { parse_hex(&spec[7..]) };
            let make = move || -> Box<dyn Policy> { Box::new(RandomLegal::new(seed)) };
            ("random".to_string(), evaluate_arm(&make, &seeds, threads, move_cap))
        } else if let Some(rest) = spec.strip_prefix("kf:") {
            let (name, path) = rest.split_once('=').expect("kf:NAME=WEIGHTS");
            let weights = load_weights(path).unwrap_or_else(|e| panic!("{e}"));
            let name_owned = name.to_string();
            let make = move || -> Box<dyn Policy> { Box::new(LinearQ::new(&name_owned, weights)) };
            (name.to_string(), evaluate_arm(&make, &seeds, threads, move_cap))
        } else if let Some(rest) = spec.strip_prefix("fair:") {
            let parts: Vec<&str> = rest.split(':').collect();
            let depth: i32 = parts[0].parse().expect("depth");
            let strata: i32 = parts[1].parse().expect("strata");
            let limit: usize = parts.get(2).map(|s| s.parse().expect("games")).unwrap_or(games);
            let make = move || -> Box<dyn Policy> { Box::new(FairSearch::new(depth, strata, 65_536)) };
            (
                format!("fair-d{depth}s{strata}"),
                evaluate_arm(&make, &seeds[..limit.min(seeds.len())], threads, move_cap),
            )
        } else {
            panic!("unknown arm spec {spec}");
        };
        let s = summarize(&rows);
        eprintln!(
            "arm {name}: games {} mean score {:.0} mean moves {:.2} censored {} illegal {} incomplete {} wall {:.1}s",
            s.games,
            s.mean_score,
            s.mean_moves,
            s.censored,
            s.illegal,
            s.incomplete,
            started.elapsed().as_secs_f64()
        );
        let rows_json: Vec<String> = rows.iter().map(game_json).collect();
        arms_json.push(format!(
            "    {{\"name\":\"{name}\",\"spec\":\"{spec}\",\"games\":{},\"meanScore\":{},\"meanMoves\":{},\"censoredGames\":{},\"illegalDecisions\":{},\"incompleteDecisions\":{},\"armWallSeconds\":{},\"rows\":[\n      {}\n    ]}}",
            s.games,
            s.mean_score,
            s.mean_moves,
            s.censored,
            s.illegal,
            s.incomplete,
            started.elapsed().as_secs_f64(),
            rows_json.join(",\n      ")
        ));
    }
    let text = format!(
        "{{\n  \"format\": \"drop7-kf-linear-q-evaluate-v1\",\n  \"seedsStartHex\": \"0x{seeds_start:08x}\",\n  \"games\": {games},\n  \"moveCap\": {move_cap},\n  \"threads\": {threads},\n  \"arms\": [\n{}\n  ]\n}}\n",
        arms_json.join(",\n")
    );
    std::fs::write(&out, text).expect("write out");
}
