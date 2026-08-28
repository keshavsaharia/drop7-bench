// Stage B: supervised initialisation.  Distills the depth-5 teacher's root
// values into the NNUE (the warm start for evolution), with a whole-origin
// split: a game and all its roots are either train or validation, never
// both.
//
// The target is the teacher's state value in rise units: max over legal
// columns of the column value, divided by VALUE_SCALE.  The NNUE is a state
// evaluator (the search::Leaf contract), so per-action supervision enters
// only through the state value; the within-root *ordering* diagnostic is
// measured separately and deployment-faithfully: after training, the probe
// runs the full d3s7 search with the distilled leaf on held-out roots and
// reports agreement with the teacher's chosen column (top-1) and the teacher
// -value regret of the student's chosen column.  That is the same quantity
// the repository's learned-ranker gates report, measured at the deployed
// configuration rather than in a surrogate model.
//
// Usage:
//   pretrain --corpus FILE --out DIR [--epochs 8] [--batch 64]
//            [--lr 3e-4] [--seed 0x...] [--probe-roots 128] [--threads T]

use drop7_nnue_evolution::features;
use drop7_nnue_evolution::json::{parse, Json};
use drop7_nnue_evolution::nnue::{Nnue, NnueLeaf, HIDDEN, MID, VALUE_SCALE};
use drop7_nnue_evolution::{deployment_params, DEPLOYMENT_TABLE};
use drop7_rs::board::Board;
use drop7_rs::engine::State;
use drop7_rs::rng::{mix32, Mulberry32};
use drop7_rs::search::{DepthTable, Searcher};
use std::sync::atomic::{AtomicUsize, Ordering};

struct RootRow {
    seed: u32,
    board: [u8; 49],
    next: u8,
    moves_remaining: i32,
    columns: Vec<(usize, f64)>,
    chosen: usize,
}

fn load_corpus(path: &str) -> Result<(Vec<RootRow>, usize), String> {
    let text = std::fs::read_to_string(path).map_err(|e| e.to_string())?;
    let mut roots = Vec::new();
    let mut games = 0usize;
    for (line_number, line) in text.lines().enumerate() {
        if line.is_empty() {
            continue;
        }
        let value = parse(line).map_err(|e| format!("line {}: {e}", line_number + 1))?;
        match value.get("type").and_then(Json::as_str) {
            Some("game") => games += 1,
            Some("root") => {
                let seed_hex = value
                    .get("seed")
                    .and_then(Json::as_str)
                    .ok_or("root missing seed")?;
                let seed = u32::from_str_radix(seed_hex.trim_start_matches("0x"), 16)
                    .map_err(|_| "bad seed hex")?;
                let board_json = value
                    .get("board")
                    .and_then(Json::as_array)
                    .ok_or("root missing board")?;
                if board_json.len() != 49 {
                    return Err(format!("line {}: board length != 49", line_number + 1));
                }
                let mut board = [0u8; 49];
                for (i, cell) in board_json.iter().enumerate() {
                    board[i] = cell.as_f64().ok_or("bad cell")? as u8;
                }
                let columns_json = value
                    .get("columns")
                    .and_then(Json::as_array)
                    .ok_or("root missing columns")?;
                let mut columns = Vec::new();
                for pair in columns_json {
                    let pair = pair.as_array().ok_or("bad column pair")?;
                    columns.push((
                        pair[0].as_f64().ok_or("bad column")? as usize,
                        pair[1].as_f64().ok_or("bad column value")?,
                    ));
                }
                let chosen = value.get("chosen").and_then(Json::as_f64).ok_or("missing chosen")?
                    as usize;
                // Frame-consistency invariant: the corpus is recorded in the
                // canonical frame, so `chosen` must be among the recorded
                // columns and carry the maximum value (the search breaks ties
                // by column order, so any maximizer is valid — the check is
                // on membership and value, not on which maximizer).  A
                // mismatch means the coordinate frames diverged (the failure
                // Greptile flagged on the first version of this generator).
                let max_value = columns
                    .iter()
                    .map(|(_, v)| *v)
                    .fold(f64::NEG_INFINITY, f64::max);
                let chosen_value = columns.iter().find(|(c, _)| *c == chosen).map(|(_, v)| *v);
                if chosen_value != Some(max_value) {
                    return Err(format!(
                        "line {}: chosen {chosen} is not a recorded maximizer (value {chosen_value:?} vs max {max_value})",
                        line_number + 1
                    ));
                }
                roots.push(RootRow {
                    seed,
                    board,
                    next: value.get("next").and_then(Json::as_f64).ok_or("missing next")? as u8,
                    moves_remaining: value
                        .get("movesRemaining")
                        .and_then(Json::as_f64)
                        .ok_or("missing movesRemaining")? as i32,
                    columns,
                    chosen,
                });
            }
            _ => {}
        }
    }
    Ok((roots, games))
}

/// Adam over the network's tensors, in tensors() order (b2 included).
struct Adam {
    m: Vec<Vec<f32>>,
    v: Vec<Vec<f32>>,
    step: u64,
    lr: f32,
}

impl Adam {
    fn new(net: &Nnue, lr: f32) -> Adam {
        Adam {
            m: net.tensors().map(|(_, t)| vec![0.0; t.len()]).to_vec(),
            v: net.tensors().map(|(_, t)| vec![0.0; t.len()]).to_vec(),
            step: 0,
            lr,
        }
    }

    fn apply(&mut self, net: &mut Nnue, grads: &[Vec<f32>]) {
        self.step += 1;
        let b1 = 0.9f32;
        let b2 = 0.999f32;
        let eps = 1e-8f32;
        let bias1 = 1.0 - b1.powi(self.step as i32);
        let bias2 = 1.0 - b2.powi(self.step as i32);
        let mut tensors = net.tensors_mut();
        for (index, (_, tensor)) in tensors.iter_mut().enumerate() {
            let grad = &grads[index];
            let m = &mut self.m[index];
            let v = &mut self.v[index];
            for i in 0..tensor.len() {
                m[i] = b1 * m[i] + (1.0 - b1) * grad[i];
                v[i] = b2 * v[i] + (1.0 - b2) * grad[i] * grad[i];
                tensor[i] -= self.lr * (m[i] / bias1) / ((v[i] / bias2).sqrt() + eps);
            }
        }
        // b2 (scalar) rides along as the last gradient entry.
        let last = grads.len() - 1;
        let m = &mut self.m[last];
        let v = &mut self.v[last];
        let g = grads[last][0];
        m[0] = b1 * m[0] + (1.0 - b1) * g;
        v[0] = b2 * v[0] + (1.0 - b2) * g * g;
        net.b2 -= self.lr * (m[0] / bias1) / ((v[0] / bias2).sqrt() + eps);
    }
}

fn zero_grads(net: &Nnue) -> [Vec<f32>; 6] {
    let tensors = net.tensors();
    [
        vec![0.0; tensors[0].1.len()],
        vec![0.0; tensors[1].1.len()],
        vec![0.0; tensors[2].1.len()],
        vec![0.0; tensors[3].1.len()],
        vec![0.0; tensors[4].1.len()],
        vec![0.0; 1],
    ]
}

/// One training example: feature indices plus the teacher's state value in
/// rise units.
struct Example {
    index: [u16; features::ACTIVE],
    target: f32,
}

fn huber_grad(pred: f32, target: f32, delta: f32) -> (f32, f32) {
    let diff = pred - target;
    if diff.abs() <= delta {
        (0.5 * diff * diff, diff)
    } else {
        (delta * (diff.abs() - 0.5 * delta), diff.signum() * delta)
    }
}

fn main() -> Result<(), String> {
    let mut corpus = None;
    let mut out = None;
    let mut epochs = 8usize;
    let mut batch = 64usize;
    let mut lr = 3e-4f32;
    let mut seed = 0x0e70_1e57u32;
    let mut probe_roots = 128usize;
    let mut threads = 16usize;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        let value = args.get(i + 1).map(|s| s.as_str()).unwrap_or("");
        match args[i].as_str() {
            "--corpus" => corpus = Some(value.to_string()),
            "--out" => out = Some(value.to_string()),
            "--epochs" => epochs = value.parse().map_err(|_| "bad --epochs")?,
            "--batch" => batch = value.parse().map_err(|_| "bad --batch")?,
            "--lr" => lr = value.parse().map_err(|_| "bad --lr")?,
            "--seed" => {
                seed = u32::from_str_radix(value.trim_start_matches("0x"), 16)
                    .map_err(|_| "bad --seed")?
            }
            "--probe-roots" => probe_roots = value.parse().map_err(|_| "bad --probe-roots")?,
            "--threads" => threads = value.parse().map_err(|_| "bad --threads")?,
            other => return Err(format!("unknown argument {other}")),
        }
        i += 2;
    }
    let corpus = corpus.ok_or("--corpus required")?;
    let out = out.ok_or("--out required")?;
    std::fs::create_dir_all(&out).map_err(|e| e.to_string())?;

    let (roots, games) = load_corpus(&corpus)?;
    println!("loaded {} roots from {} games", roots.len(), games);

    // Whole-origin split: a game's roots are all train or all validation.
    let mut train: Vec<Example> = Vec::new();
    let mut val: Vec<Example> = Vec::new();
    let mut val_roots: Vec<&RootRow> = Vec::new();
    for root in &roots {
        let target = root
            .columns
            .iter()
            .map(|(_, v)| *v)
            .fold(f64::NEG_INFINITY, f64::max)
            / VALUE_SCALE;
        let mut index = [0u16; features::ACTIVE];
        features::build_from_bytes(&root.board, root.next, root.moves_remaining, &mut index);
        let example = Example {
            index,
            target: target as f32,
        };
        if mix32(root.seed) % 5 == 0 {
            val.push(example);
            val_roots.push(root);
        } else {
            train.push(example);
        }
    }
    println!("whole-origin split: {} train / {} val roots", train.len(), val.len());

    let mut net = Nnue::random(seed);
    let mut adam = Adam::new(&net, lr);
    let mut rng = Mulberry32::new(seed ^ 0x5eed_0001);
    let mut best_val = f64::INFINITY;
    let mut best_epoch = 0usize;

    for epoch in 0..epochs {
        // Deterministic Fisher-Yates shuffle per epoch.
        let mut order: Vec<usize> = (0..train.len()).collect();
        for i in (1..order.len()).rev() {
            let j = (rng.next_bits() as usize) % (i + 1);
            order.swap(i, j);
        }
        let mut train_loss = 0.0f64;
        let mut batches = 0usize;
        for chunk in order.chunks(batch) {
            let mut grads = zero_grads(&net);
            for &index_in_train in chunk {
                let example = &train[index_in_train];
                // Forward with activations kept for the backward pass.
                let mut acc = [0.0f32; HIDDEN];
                acc.copy_from_slice(&net.ft_bias);
                for &slot in example.index.iter() {
                    let row = &net.ft_weight[slot as usize * HIDDEN..(slot as usize + 1) * HIDDEN];
                    for unit in 0..HIDDEN {
                        acc[unit] += row[unit];
                    }
                }
                let mut mid = [0.0f32; MID];
                for m in 0..MID {
                    let row = &net.w1[m * HIDDEN..(m + 1) * HIDDEN];
                    let mut sum = net.b1[m];
                    for h in 0..HIDDEN {
                        sum += row[h] * acc[h].max(0.0);
                    }
                    mid[m] = sum;
                }
                let mut pred = net.b2;
                for m in 0..MID {
                    pred += net.w2[m] * mid[m].max(0.0);
                }
                let (loss, d_pred) = huber_grad(pred, example.target, 1.0);
                train_loss += loss as f64;
                // Backward.
                grads[5][0] += d_pred;
                let mut d_mid = [0.0f32; MID];
                for m in 0..MID {
                    grads[4][m] += d_pred * mid[m].max(0.0);
                    d_mid[m] = d_pred * net.w2[m] * if mid[m] > 0.0 { 1.0 } else { 0.0 };
                }
                let mut d_acc = [0.0f32; HIDDEN];
                for m in 0..MID {
                    grads[3][m] += d_mid[m];
                    for h in 0..HIDDEN {
                        grads[2][m * HIDDEN + h] += d_mid[m] * acc[h].max(0.0);
                        d_acc[h] += d_mid[m] * net.w1[m * HIDDEN + h];
                    }
                }
                for h in 0..HIDDEN {
                    let d = d_acc[h] * if acc[h] > 0.0 { 1.0 } else { 0.0 };
                    grads[1][h] += d;
                    for &slot in example.index.iter() {
                        grads[0][slot as usize * HIDDEN + h] += d;
                    }
                }
            }
            let scale = 1.0 / chunk.len() as f32;
            for grad in grads.iter_mut() {
                for g in grad.iter_mut() {
                    *g *= scale;
                }
            }
            adam.apply(&mut net, &grads);
            batches += 1;
        }
        train_loss /= (train.len() as f64).max(1.0);

        // Validation Huber + Pearson.
        let mut val_loss = 0.0f64;
        let mut pairs: Vec<(f32, f32)> = Vec::with_capacity(val.len());
        for example in &val {
            let pred = net.value(&example.index);
            val_loss += huber_grad(pred, example.target, 1.0).0 as f64;
            pairs.push((pred, example.target));
        }
        val_loss /= (val.len() as f64).max(1.0);
        let pearson = pearson(&pairs);
        println!(
            "epoch {epoch}: trainHuber {train_loss:.6} valHuber {val_loss:.6} valPearson {pearson:.4} ({batches} batches)"
        );
        net.save(std::path::Path::new(&format!("{out}/epoch-{epoch}.bin")))
            .map_err(|e| e.to_string())?;
        if val_loss < best_val {
            best_val = val_loss;
            best_epoch = epoch;
        }
    }
    println!("best epoch by validation Huber: {best_epoch} ({best_val:.6})");
    let best = Nnue::load(std::path::Path::new(&format!("{out}/epoch-{best_epoch}.bin")))?;
    best.save(std::path::Path::new(&format!("{out}/init.bin")))
        .map_err(|e| e.to_string())?;

    // Deployment-faithful ordering probe: the full d3s7 search with the
    // distilled leaf on held-out roots, against the teacher's chosen column.
    if probe_roots > 0 && !val_roots.is_empty() {
        let params = deployment_params();
        let step = (val_roots.len() / probe_roots).max(1);
        let probes: Vec<&RootRow> = val_roots.iter().step_by(step).copied().take(probe_roots).collect();
        let cursor = AtomicUsize::new(0);
        let agree = AtomicUsize::new(0);
        let regrets: Vec<std::sync::Mutex<f64>> = probes.iter().map(|_| std::sync::Mutex::new(0.0)).collect();
        std::thread::scope(|scope| {
            for _ in 0..threads.max(1) {
                let net = best.clone();
                let probes = &probes;
                let cursor = &cursor;
                let agree = &agree;
                let regrets = &regrets;
                scope.spawn(move || loop {
                    let index = cursor.fetch_add(1, Ordering::Relaxed);
                    if index >= probes.len() {
                        break;
                    }
                    let root = probes[index];
                    // Corpus boards are recorded at decision points, where
                    // the engine invariant (columns bottom-packed) holds, so
                    // the serialize/from_serialized round trip is exact.
                    let text: String = root.board.iter().map(|c| (b'0' + c) as char).collect();
                    let board = Board::from_serialized(&text)
                        .ok_or("corpus board failed to parse")
                        .unwrap();
                    let state = State {
                        board,
                        next_disc: root.next,
                        score: 0,
                        level: 1,
                        moves_remaining: root.moves_remaining,
                        moves_played: 0,
                        game_over: false,
                    };
                    let mut searcher = Searcher::new(
                        params,
                        NnueLeaf { net: net.clone() },
                        DepthTable::new(DEPLOYMENT_TABLE, 1),
                    );
                    let (action, _metrics) = searcher.choose_action(&state);
                    let teacher_best = root
                        .columns
                        .iter()
                        .map(|(_, v)| *v)
                        .fold(f64::NEG_INFINITY, f64::max);
                    let student_value = root
                        .columns
                        .iter()
                        .find(|(c, _)| *c == action as usize)
                        .map(|(_, v)| *v)
                        .unwrap_or(f64::NEG_INFINITY);
                    if action == root.chosen as i32 {
                        agree.fetch_add(1, Ordering::Relaxed);
                    }
                    *regrets[index].lock().unwrap() = teacher_best - student_value;
                });
            }
        });
        let n = probes.len() as f64;
        let top1 = agree.load(Ordering::Relaxed) as f64 / n;
        let mean_regret: f64 =
            regrets.iter().map(|r| *r.lock().unwrap()).sum::<f64>() / n;
        println!(
            "ordering probe: {} held-out roots, top-1 agreement {top1:.4}, mean teacher-value regret {mean_regret:.1} points",
            probes.len()
        );
        std::fs::write(
            format!("{out}/probe.json"),
            format!(
                "{{\"probeRoots\":{},\"top1\":{},\"meanTeacherValueRegret\":{}}}\n",
                probes.len(),
                top1,
                mean_regret
            ),
        )
        .map_err(|e| e.to_string())?;
    }

    std::fs::write(
        format!("{out}/report.json"),
        format!(
            "{{\"roots\":{},\"games\":{},\"trainRoots\":{},\"valRoots\":{},\"epochs\":{},\"batch\":{},\"lr\":{},\"seed\":\"0x{seed:08x}\",\"bestEpoch\":{},\"bestValHuber\":{}}}\n",
            roots.len(),
            games,
            train.len(),
            val.len(),
            epochs,
            batch,
            lr,
            best_epoch,
            best_val,
        ),
    )
    .map_err(|e| e.to_string())?;
    println!("wrote {out}/init.bin and report.json");
    Ok(())
}

fn pearson(pairs: &[(f32, f32)]) -> f64 {
    if pairs.len() < 2 {
        return 0.0;
    }
    let n = pairs.len() as f64;
    let mean_x = pairs.iter().map(|p| p.0 as f64).sum::<f64>() / n;
    let mean_y = pairs.iter().map(|p| p.1 as f64).sum::<f64>() / n;
    let mut cov = 0.0;
    let mut var_x = 0.0;
    let mut var_y = 0.0;
    for (x, y) in pairs {
        let dx = *x as f64 - mean_x;
        let dy = *y as f64 - mean_y;
        cov += dx * dy;
        var_x += dx * dx;
        var_y += dy * dy;
    }
    if var_x <= 0.0 || var_y <= 0.0 {
        return 0.0;
    }
    cov / (var_x.sqrt() * var_y.sqrt())
}
