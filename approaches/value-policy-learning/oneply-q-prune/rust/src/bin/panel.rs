// Build the sibling-complete root panel.
//
//   panel --seeds-start 0xHEX --games N [--depth 4] [--strata 7]
//         [--table 65536] [--move-cap 2000] [--threads T]
//         --parts-dir DIR --out panel.ndjson --games-out panel-games.json
//
// Every finished game is written to DIR at once and a rerun with the same
// arguments resumes from the games already there.

use std::time::Instant;

use drop7_oneply_q::panel::build_panel_resumable;

fn json_number(line: &str, key: &str) -> f64 {
    let pattern = format!("\"{key}\":");
    let start = line.find(&pattern).unwrap_or_else(|| panic!("missing {key}")) + pattern.len();
    let rest = &line[start..];
    let end = rest
        .find(|c: char| !(c.is_ascii_digit() || c == '-' || c == '.'))
        .unwrap_or(rest.len());
    rest[..end].parse().unwrap_or_else(|_| panic!("bad number for {key}"))
}

fn parse_hex(s: &str) -> u32 {
    u32::from_str_radix(s.trim_start_matches("0x"), 16).expect("hex")
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut seeds_start: Option<u32> = None;
    let mut games = 48usize;
    let mut depth = 4i32;
    let mut strata = 7i32;
    let mut table = 65_536usize;
    let mut move_cap = 2_000i32;
    let mut threads = 8usize;
    let mut out = String::new();
    let mut games_out = String::new();
    let mut parts_dir = String::new();
    let mut i = 1;
    while i + 1 < args.len() {
        let v = args[i + 1].as_str();
        match args[i].as_str() {
            "--seeds-start" => seeds_start = Some(parse_hex(v)),
            "--games" => games = v.parse().expect("games"),
            "--depth" => depth = v.parse().expect("depth"),
            "--strata" => strata = v.parse().expect("strata"),
            "--table" => table = v.parse().expect("table"),
            "--move-cap" => move_cap = v.parse().expect("move-cap"),
            "--threads" => threads = v.parse().expect("threads"),
            "--out" => out = v.to_string(),
            "--games-out" => games_out = v.to_string(),
            "--parts-dir" => parts_dir = v.to_string(),
            other => panic!("unknown argument {other}"),
        }
        i += 2;
    }
    if i < args.len() {
        panic!("dangling argument {:?}: every option takes a value", args[i]);
    }
    let seeds_start = seeds_start.expect("--seeds-start required");
    assert!(
        !out.is_empty() && !games_out.is_empty() && !parts_dir.is_empty(),
        "--out, --games-out and --parts-dir required"
    );
    let seeds: Vec<u32> = (0..games as u32).map(|g| seeds_start.wrapping_add(g)).collect();
    eprintln!(
        "panel: seeds 0x{:08x}..0x{:08x} (exclusive), fair d{depth}s{strata}, table {table}, cap {move_cap}, threads {threads}",
        seeds_start,
        seeds_start as u64 + games as u64
    );
    let started = Instant::now();
    let panel = build_panel_resumable(&seeds, depth, strata, table, move_cap, threads, &parts_dir, true);
    let mut lines: Vec<String> = Vec::new();
    let mut rows: Vec<String> = Vec::new();
    let mut roots = 0usize;
    let mut score_sum = 0.0f64;
    let mut moves_sum = 0.0f64;
    for (game_row, root_lines) in &panel {
        rows.push(game_row.clone());
        roots += root_lines.len();
        lines.extend(root_lines.iter().cloned());
        score_sum += json_number(game_row, "score");
        moves_sum += json_number(game_row, "moves");
    }
    std::fs::write(&out, lines.join("\n") + "\n").expect("write panel");
    let mean_score = score_sum / panel.len().max(1) as f64;
    let mean_moves = moves_sum / panel.len().max(1) as f64;
    let text = format!(
        "{{\n  \"format\": \"drop7-oneply-q-panel-games-v1\",\n  \"seedsStartHex\": \"0x{seeds_start:08x}\",\n  \"games\": {games},\n  \"depth\": {depth},\n  \"strata\": {strata},\n  \"tableEntries\": {table},\n  \"moveCap\": {move_cap},\n  \"threads\": {threads},\n  \"roots\": {roots},\n  \"meanScore\": {mean_score},\n  \"meanMoves\": {mean_moves},\n  \"wallSeconds\": {},\n  \"rows\": [\n    {}\n  ]\n}}\n",
        started.elapsed().as_secs_f64(),
        rows.join(",\n    ")
    );
    std::fs::write(&games_out, text).expect("write games");
    eprintln!(
        "panel: {} games, {roots} roots, mean score {mean_score:.0}, mean moves {mean_moves:.2}, wall {:.1}s",
        panel.len(),
        started.elapsed().as_secs_f64()
    );
}
