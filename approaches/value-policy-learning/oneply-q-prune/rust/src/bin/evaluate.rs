// Diagnostic cohort evaluation of full-width and pruned fair search on
// identical seeds; the per-game rows follow research/schemas/game-result-v1
// as in the Klein-Friedmann crate's evaluate binary (same file layout, so
// its summarize_evaluate.py reads the output).
//
//   evaluate --seeds-start 0xHEX --games N [--move-cap 2000] [--threads T]
//            [--table 65536] --out FILE --arm SPEC [--arm SPEC ...]
//
// SPEC is
//   fair:D:S[:GAMES]              full-width fair expectimax (drop7-rs)
//   pruned:D:S:W:PRIOR[:GAMES]    the pruned searcher (see prune_eval)
//   leaf:D:S:FILE[:GAMES]         full-width fair expectimax with the
//                                 LinearLeaf weights in FILE
// with an optional trailing game limit (first GAMES seeds only).

use std::time::Instant;

use drop7_kf_linear_q::game::{evaluate_arm, game_json, summarize, GameRecord};
use drop7_kf_linear_q::policy::{Decision, FairSearch, Policy};
use drop7_kf_linear_q::view::PublicView;
use drop7_oneply_q::leaf::{LinearLeaf, LinearLeafWeights};
use drop7_oneply_q::prior::Prior;
use drop7_oneply_q::prune::{PrunedSearcher, Widths};
use drop7_rs::board::BOARD_SIZE;
use drop7_rs::search::{work_bound_for, DepthTable, SearchParams, Searcher};

fn parse_hex(s: &str) -> u32 {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).expect("hex")
}

pub struct PrunedPolicy {
    name: String,
    depth: i32,
    searcher: PrunedSearcher<DepthTable>,
}

impl Policy for PrunedPolicy {
    fn name(&self) -> &str {
        &self.name
    }
    fn choose(&mut self, view: &PublicView) -> Decision {
        let d = self.searcher.choose_action(&view.as_search_state());
        Decision {
            column: if d.action < 0 { BOARD_SIZE } else { d.action as usize },
            work: d.work,
            complete: d.completed_depth >= self.depth,
        }
    }
}

pub struct LinearLeafPolicy {
    name: String,
    depth: i32,
    searcher: Searcher<LinearLeaf, DepthTable>,
}

impl LinearLeafPolicy {
    pub fn new(depth: i32, strata: i32, weights: LinearLeafWeights, table: usize, name: String) -> Self {
        let params = SearchParams {
            depth,
            chance_samples: strata,
            terminal_utility: -1_000_000.0,
            maximum_work: work_bound_for(depth, strata) + 1,
            policy_seed: 0xd707_5eed,
        };
        LinearLeafPolicy {
            name,
            depth,
            searcher: Searcher::new(params, LinearLeaf::new(weights), DepthTable::new(table, 1)),
        }
    }
}

impl Policy for LinearLeafPolicy {
    fn name(&self) -> &str {
        &self.name
    }
    fn choose(&mut self, view: &PublicView) -> Decision {
        let (action, metrics) = self.searcher.choose_action(&view.as_search_state());
        Decision {
            column: if action < 0 { BOARD_SIZE } else { action as usize },
            work: metrics.work,
            complete: metrics.completed_depth >= self.depth,
        }
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut seeds_start: Option<u32> = None;
    let mut games = 256usize;
    let mut move_cap = 2_000i32;
    let mut threads = 8usize;
    let mut table = 65_536usize;
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
            "--table" => table = v.parse().expect("table"),
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
        let (name, rows): (String, Vec<GameRecord>) = if let Some(rest) = spec.strip_prefix("fair:") {
            let parts: Vec<&str> = rest.split(':').collect();
            let depth: i32 = parts[0].parse().expect("depth");
            let strata: i32 = parts[1].parse().expect("strata");
            let limit: usize = parts.get(2).map(|s| s.parse().expect("games")).unwrap_or(games);
            let make = move || -> Box<dyn Policy> { Box::new(FairSearch::new(depth, strata, table)) };
            (
                format!("fair-d{depth}s{strata}"),
                evaluate_arm(&make, &seeds[..limit.min(seeds.len())], threads, move_cap),
            )
        } else if let Some(rest) = spec.strip_prefix("pruned:") {
            // PRIOR may itself carry a path, so split off at most four fields
            // and take an optional numeric games suffix from the last one.
            let parts: Vec<&str> = rest.splitn(4, ':').collect();
            assert!(parts.len() == 4, "pruned:D:S:W:PRIOR[:GAMES]");
            let depth: i32 = parts[0].parse().expect("depth");
            let strata: i32 = parts[1].parse().expect("strata");
            let widths = Widths::parse(parts[2], depth).unwrap_or_else(|e| panic!("{e}"));
            let (prior_spec, limit) = match parts[3].rsplit_once(':') {
                Some((p, g)) if g.chars().all(|c| c.is_ascii_digit()) && !g.is_empty() => {
                    (p.to_string(), g.parse::<usize>().expect("games"))
                }
                _ => (parts[3].to_string(), games),
            };
            let prior = Prior::parse(&prior_spec).unwrap_or_else(|e| panic!("{e}"));
            let name = format!("pruned-d{depth}s{strata}-w{}-{}", widths.describe(depth), prior.name());
            let name_owned = name.clone();
            let make = move || -> Box<dyn Policy> {
                Box::new(PrunedPolicy {
                    name: name_owned.clone(),
                    depth,
                    searcher: PrunedSearcher::deployment(depth, strata, widths, prior.clone(), table),
                })
            };
            (name, evaluate_arm(&make, &seeds[..limit.min(seeds.len())], threads, move_cap))
        } else if let Some(rest) = spec.strip_prefix("leaf:") {
            let parts: Vec<&str> = rest.splitn(4, ':').collect();
            assert!(parts.len() >= 3, "leaf:D:S:FILE[:GAMES]");
            let depth: i32 = parts[0].parse().expect("depth");
            let strata: i32 = parts[1].parse().expect("strata");
            let limit: usize = parts.get(3).map(|s| s.parse().expect("games")).unwrap_or(games);
            let weights = LinearLeafWeights::load(parts[2]).unwrap_or_else(|e| panic!("{e}"));
            let stem = std::path::Path::new(parts[2])
                .file_stem()
                .map(|s| s.to_string_lossy().to_string())
                .unwrap_or_else(|| "leaf".to_string());
            let name = format!("leaf-d{depth}s{strata}-{stem}");
            let name_owned = name.clone();
            let make = move || -> Box<dyn Policy> {
                Box::new(LinearLeafPolicy::new(depth, strata, weights.clone(), table, name_owned.clone()))
            };
            (name, evaluate_arm(&make, &seeds[..limit.min(seeds.len())], threads, move_cap))
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
        "{{\n  \"format\": \"drop7-oneply-q-evaluate-v1\",\n  \"seedsStartHex\": \"0x{seeds_start:08x}\",\n  \"games\": {games},\n  \"moveCap\": {move_cap},\n  \"threads\": {threads},\n  \"arms\": [\n{}\n  ]\n}}\n",
        arms_json.join(",\n")
    );
    std::fs::write(&out, text).expect("write out");
}
