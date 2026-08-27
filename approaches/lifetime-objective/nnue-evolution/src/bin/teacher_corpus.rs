// Stage A: the teacher corpus.  Plays complete games with the frozen fair
// leaf at the teacher configuration (depth 5, seven strata — one full rise
// cycle of lookahead) on training-lease seeds, and records every root with
// the teacher's per-column search values.  Because the teacher's root
// evaluation already prices every legal column (search.rs column_values),
// the corpus is sibling-complete by construction: no played-action bias.
//
// OUTPUT LAYOUT (resumable).  Each completed game is written atomically to
// <out>/parts/0x<seed>.jsonl (write-temp-then-rename), so a killed run loses
// nothing finished and a rerun skips seeds whose part file exists.  The
// canonical corpus is the concatenation of part files in seed order, written
// to <out>/corpus.jsonl at the end (or rebuildable at any time with
// --assemble).  Root records:
//   {"type":"root","seed":"0x...","move":m,"board":[49],"next":d,
//    "movesRemaining":r,"columns":[[c,v]...],"chosen":c}
// Game records:
//   {"type":"game","seed":"0x...","score":s,"moves":m,"censored":b,
//    "wallSeconds":t}
//
// Usage:
//   teacher_corpus --seeds-start 0x... --games N --threads T --out DIR
//                  [--wall-seconds N] [--move-cap 2000] [--depth 5]
//   teacher_corpus --assemble --out DIR

use drop7_rs::engine::{play_headless_move, MinimalWaveSink, State};
use drop7_rs::search::{FairLeaf, Searcher};
use drop7_rs::search::DepthTable;
use drop7_nnue_evolution::{teacher_params, TEACHER_TABLE};
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Mutex;

fn f64_json(value: f64) -> String {
    // Display for finite f64 is shortest round-trip decimal, valid JSON.
    debug_assert!(value.is_finite());
    format!("{value}")
}

fn play_teacher_game(seed: u32, move_cap: i32, depth: i32) -> (String, i64, i32) {
    let mut params = teacher_params();
    // column_values evaluates every root column at the full fixed depth and
    // does not reset the work counter between roots, so the corpus searcher
    // runs with an effectively unbounded budget.  This changes no value: the
    // budget only guards runtime, the transposition table is value-neutral,
    // and the gate suite pins that the argmax of column_values equals
    // choose_action's action under the completion-guaranteeing bound.
    params.maximum_work = u64::MAX;
    if depth > 0 {
        params.depth = depth;
    }
    let mut searcher = Searcher::new(params, FairLeaf::default(), DepthTable::new(TEACHER_TABLE, 1));
    let mut state = State::initial_headless(seed);
    // The corpus records root values, not wave detail: the minimal sink.
    let mut sink = MinimalWaveSink::default();
    let mut lines = String::new();
    let started = std::time::Instant::now();
    while !state.game_over && state.moves_played < move_cap {
        let (values, action) = searcher.column_values(&state, params.depth);
        if action < 0 {
            break;
        }
        let board = state.board.to_bytes();
        let columns: Vec<String> = values
            .iter()
            .map(|(c, v)| format!("[{c},{}]", f64_json(*v)))
            .collect();
        lines.push_str(&format!(
            "{{\"type\":\"root\",\"seed\":\"0x{seed:08x}\",\"move\":{},\"board\":[{}],\"next\":{},\"movesRemaining\":{},\"columns\":[{}],\"chosen\":{}}}\n",
            state.moves_played,
            board.iter().map(|b| b.to_string()).collect::<Vec<_>>().join(","),
            state.next_disc,
            state.moves_remaining,
            columns.join(","),
            action,
        ));
        if play_headless_move(&mut state, seed, action as usize, &mut sink).is_none() {
            break;
        }
    }
    let censored = !state.game_over && state.moves_played >= move_cap;
    lines.push_str(&format!(
        "{{\"type\":\"game\",\"seed\":\"0x{seed:08x}\",\"score\":{},\"moves\":{},\"censored\":{},\"wallSeconds\":{}}}\n",
        state.score,
        state.moves_played,
        censored,
        started.elapsed().as_secs_f64(),
    ));
    (lines, state.score, state.moves_played)
}

fn part_path(out: &str, seed: u32) -> String {
    format!("{out}/parts/0x{seed:08x}.jsonl")
}

/// Concatenate all part files in seed order into <out>/corpus.jsonl and
/// write <out>/corpus-summary.json.  Returns (games, roots).
fn assemble(out: &str) -> Result<(usize, usize), String> {
    let parts_dir = format!("{out}/parts");
    let mut names: Vec<String> = std::fs::read_dir(&parts_dir)
        .map_err(|e| e.to_string())?
        .filter_map(|entry| entry.ok().map(|e| e.file_name().to_string_lossy().into_owned()))
        .filter(|name| name.ends_with(".jsonl"))
        .collect();
    names.sort();
    let mut body = String::new();
    let mut roots = 0usize;
    let mut games = 0usize;
    for name in &names {
        let text = std::fs::read_to_string(format!("{parts_dir}/{name}")).map_err(|e| e.to_string())?;
        for line in text.lines() {
            if line.contains("\"type\":\"root\"") {
                roots += 1;
            } else if line.contains("\"type\":\"game\"") {
                games += 1;
            }
        }
        body.push_str(&text);
    }
    std::fs::write(format!("{out}/corpus.jsonl"), &body).map_err(|e| e.to_string())?;
    std::fs::write(
        format!("{out}/corpus-summary.json"),
        format!("{{\"games\":{games},\"roots\":{roots},\"parts\":{}}}\n", names.len()),
    )
    .map_err(|e| e.to_string())?;
    Ok((games, roots))
}

fn main() -> Result<(), String> {
    let mut seeds_start: Option<u32> = None;
    let mut games = 0usize;
    let mut threads = 16usize;
    let mut out = None;
    let mut move_cap = drop7_nnue_evolution::game::MOVE_CAP;
    let mut depth = 0i32; // 0 = the preregistered teacher depth (5)
    let mut wall_seconds = u64::MAX;
    let mut assemble_only = false;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let key = args[i].as_str();
        if key == "--assemble" {
            assemble_only = true;
            i += 1;
            continue;
        }
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match key {
            "--seeds-start" => {
                seeds_start = Some(
                    u32::from_str_radix(value.trim_start_matches("0x"), 16)
                        .map_err(|_| "bad --seeds-start")?,
                )
            }
            "--games" => games = value.parse::<usize>().map_err(|_| "bad --games")?,
            "--threads" => threads = value.parse::<usize>().map_err(|_| "bad --threads")?,
            "--move-cap" => move_cap = value.parse::<i32>().map_err(|_| "bad --move-cap")?,
            "--depth" => depth = value.parse::<i32>().map_err(|_| "bad --depth")?,
            "--wall-seconds" => {
                wall_seconds = value.parse::<u64>().map_err(|_| "bad --wall-seconds")?
            }
            "--out" => out = Some(value.to_string()),
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let out = out.ok_or("--out required")?;
    if assemble_only {
        let (games, roots) = assemble(&out)?;
        println!("assembled {games} games, {roots} roots into {out}/corpus.jsonl");
        return Ok(());
    }
    if games == 0 {
        return Err("--games required".into());
    }
    // Never default the seed origin: an omitted --seeds-start must fail, not
    // silently play from 0x00000000 (outside every lease).
    let seeds_start = seeds_start.ok_or("--seeds-start required")?;
    std::fs::create_dir_all(format!("{out}/parts")).map_err(|e| e.to_string())?;

    // Resume: skip seeds whose part file already exists.
    let pending: Vec<u32> = (0..games as u32)
        .map(|index| seeds_start + index)
        .filter(|seed| !std::path::Path::new(&part_path(&out, *seed)).exists())
        .collect();
    println!(
        "teacher corpus: {} games requested, {} already complete, {} to play (depth {})",
        games,
        games - pending.len(),
        pending.len(),
        if depth > 0 { depth } else { 5 },
    );

    let cursor = AtomicUsize::new(0);
    let done = AtomicUsize::new(0);
    let started = std::time::Instant::now();
    let write_lock = Mutex::new(());
    std::thread::scope(|scope| {
        for _ in 0..threads.max(1) {
            let pending = &pending;
            let cursor = &cursor;
            let done = &done;
            let write_lock = &write_lock;
            let out = &out;
            scope.spawn(move || loop {
                if started.elapsed().as_secs() > wall_seconds {
                    break;
                }
                let index = cursor.fetch_add(1, Ordering::Relaxed);
                if index >= pending.len() {
                    break;
                }
                let seed = pending[index];
                let (lines, score, moves) = play_teacher_game(seed, move_cap, depth);
                // Atomic-ish publish: write temp, fsync-free rename.
                let body = lines;
                let tmp = format!("{}.tmp", part_path(out, seed));
                let fin = part_path(out, seed);
                {
                    let _guard = write_lock.lock().unwrap();
                    if std::fs::write(&tmp, &body).is_ok() {
                        std::fs::rename(&tmp, &fin).ok();
                    }
                }
                let finished = done.fetch_add(1, Ordering::Relaxed) + 1;
                eprintln!(
                    "[{finished}/{}] seed {seed:#010x} score {score} moves {moves} ({:.0}s elapsed)",
                    pending.len(),
                    started.elapsed().as_secs_f64()
                );
            });
        }
    });

    let (games_done, roots) = assemble(&out)?;
    println!(
        "corpus stage done: {games_done} games, {roots} roots, {:.0}s wall",
        started.elapsed().as_secs_f64()
    );
    Ok(())
}
