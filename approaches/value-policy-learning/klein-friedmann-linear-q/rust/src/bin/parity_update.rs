// CHECK gate G2: Q-learning update parity against the upstream Python class.
//
//   parity_update --input FILE [--tolerance 1e-9]
//
// Each input line (export_parity.py --mode update) is one incorporateFeedback
// call of the upstream learner:
//   S <t> <reward> <mask> <v0..v5> T <w0..w5>
//   S <t> <reward> <mask> <v0..v5> N <7 x (<mask> <v0..v5>)> <w0..w5>
// where t is upstream numIters at the call and w are the weights AFTER the
// update.  The Rust learner replays the arithmetic from the same starting
// weights and compares after every step.  Exit 0 when the largest relative
// difference is within tolerance.

use drop7_kf_linear_q::features::{Features, FEATURE_COUNT};
use drop7_kf_linear_q::learn::Learner;
use drop7_rs::board::BOARD_SIZE;

fn parse_features(t: &[&str]) -> Features {
    let mut f = Features::default();
    let mask = t[0].as_bytes();
    for k in 0..FEATURE_COUNT {
        f.present[k] = mask[k] == b'1';
        f.values[k] = t[1 + k].parse().expect("feature value");
    }
    f
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mut input = String::new();
    let mut tolerance = 1e-9f64;
    let mut i = 1;
    while i + 1 < args.len() {
        match args[i].as_str() {
            "--input" => input = args[i + 1].clone(),
            "--tolerance" => tolerance = args[i + 1].parse().expect("tolerance"),
            _ => {}
        }
        i += 2;
    }
    if input.is_empty() {
        eprintln!("usage: parity_update --input FILE [--tolerance 1e-9]");
        std::process::exit(2);
    }
    let text = std::fs::read_to_string(&input).expect("read input");
    let mut learner = Learner::upstream(0);
    let mut steps = 0u64;
    let mut worst = 0.0f64;
    let mut worst_step = 0u64;
    let width = 1 + FEATURE_COUNT;
    for (line_no, line) in text.lines().enumerate() {
        let t: Vec<&str> = line.split_whitespace().collect();
        if t.is_empty() || t[0] != "S" {
            continue;
        }
        let iters: u64 = t[1].parse().expect("t");
        let reward: f64 = t[2].parse().expect("reward");
        let phi = parse_features(&t[3..3 + width]);
        let mut pos = 3 + width;
        let successors = match t[pos] {
            "T" => {
                pos += 1;
                None
            }
            "N" => {
                pos += 1;
                let mut s = [Features::default(); BOARD_SIZE];
                for a in 0..BOARD_SIZE {
                    s[a] = parse_features(&t[pos..pos + width]);
                    pos += width;
                }
                Some(s)
            }
            other => panic!("{input}:{}: expected T or N, got {other}", line_no + 1),
        };
        let mut expected = [0.0f64; FEATURE_COUNT];
        for k in 0..FEATURE_COUNT {
            expected[k] = t[pos + k].parse().expect("weight");
        }
        learner.num_iters = iters;
        learner.update_with(&phi, reward, successors.as_ref());
        steps += 1;
        for k in 0..FEATURE_COUNT {
            let denom = expected[k].abs().max(1.0);
            let rel = (learner.weights[k] - expected[k]).abs() / denom;
            if rel > worst {
                worst = rel;
                worst_step = steps;
            }
        }
    }
    println!(
        "parity_update: steps {} worst relative difference {:e} at step {} final weights {:?}",
        steps, worst, worst_step, learner.weights
    );
    if steps == 0 || worst > tolerance {
        std::process::exit(1);
    }
}
