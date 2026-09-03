// Mutation-size schedule and the preregistered plateau stop for long
// evolution runs (the continuation experiment that resumes from a checkpointed
// population).  Pure functions, unit-tested, no I/O: evolve.rs calls them and
// records every decision in plateau.jsonl.

/// Relative mutation sigma for continuation generation `g` (0-based within the
/// run): `sigma0 * exp(-g / tau)`, floored at `floor`.  `tau <= 0` means a
/// constant sigma (the first run's behaviour).
pub fn sigma_for(generation: usize, sigma0: f32, tau: f32, floor: f32) -> f32 {
    if tau <= 0.0 {
        return sigma0;
    }
    let decayed = sigma0 as f64 * (-(generation as f64) / tau as f64).exp();
    (decayed as f32).max(floor)
}

/// Ordinary least-squares slope of `ys` against 0, 1, 2, ... and its standard
/// error from the residual variance (n - 2 degrees of freedom).  Returns
/// `None` when fewer than three points are given or the design is degenerate.
pub fn ols_slope(ys: &[f64]) -> Option<(f64, f64)> {
    let n = ys.len();
    if n < 3 {
        return None;
    }
    let nf = n as f64;
    let x_mean = (nf - 1.0) / 2.0;
    let y_mean = ys.iter().sum::<f64>() / nf;
    let mut sxx = 0.0;
    let mut sxy = 0.0;
    for (i, &y) in ys.iter().enumerate() {
        let dx = i as f64 - x_mean;
        sxx += dx * dx;
        sxy += dx * (y - y_mean);
    }
    if sxx <= 0.0 {
        return None;
    }
    let slope = sxy / sxx;
    let intercept = y_mean - slope * x_mean;
    let rss: f64 = ys
        .iter()
        .enumerate()
        .map(|(i, &y)| {
            let r = y - (intercept + slope * i as f64);
            r * r
        })
        .sum();
    let sigma2 = rss / (nf - 2.0);
    Some((slope, (sigma2 / sxx).sqrt()))
}

/// One-sided 95% normal quantile used for the plateau lower bound.
pub const Z_95: f64 = 1.644_853_626_951;

#[derive(Clone, Debug, PartialEq)]
pub struct PlateauCheck {
    pub generation: usize,
    pub window: usize,
    pub slope_per_generation: f64,
    pub standard_error: f64,
    pub lower_bound_95: f64,
    pub window_mean_first_half: f64,
    pub window_mean_second_half: f64,
    pub stop: bool,
}

/// Whether the run should stop after `generation` (0-based, just completed)
/// given the full series of the tracked margin, one value per completed
/// generation.  A check happens only when at least `min_generations` have
/// completed and the count is a multiple of `check_every`; it stops when the
/// one-sided 95% lower bound of the OLS slope over the last `window` values is
/// not positive, i.e. when the last `window` generations show no detectable
/// improvement.
pub fn plateau_check(
    series: &[f64],
    window: usize,
    check_every: usize,
    min_generations: usize,
) -> Option<PlateauCheck> {
    let completed = series.len();
    if window == 0 || check_every == 0 || completed < min_generations.max(window) || completed % check_every != 0 {
        return None;
    }
    let tail = &series[completed - window..];
    let (slope, se) = ols_slope(tail)?;
    let lower = slope - Z_95 * se;
    let half = window / 2;
    let first = tail[..half].iter().sum::<f64>() / half.max(1) as f64;
    let second = tail[half..].iter().sum::<f64>() / (window - half).max(1) as f64;
    Some(PlateauCheck {
        generation: completed - 1,
        window,
        slope_per_generation: slope,
        standard_error: se,
        lower_bound_95: lower,
        window_mean_first_half: first,
        window_mean_second_half: second,
        stop: lower <= 0.0,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sigma_schedule_is_continuous_with_the_first_run_and_floors() {
        assert_eq!(sigma_for(0, 0.05, 400.0, 0.01), 0.05);
        assert_eq!(sigma_for(123, 0.05, 0.0, 0.01), 0.05);
        let g400 = sigma_for(400, 0.05, 400.0, 0.01);
        assert!((g400 - 0.05 / std::f32::consts::E).abs() < 1e-6);
        assert_eq!(sigma_for(2_000, 0.05, 400.0, 0.01), 0.01);
        // monotone non-increasing
        let mut last = f32::INFINITY;
        for g in 0..1_000 {
            let s = sigma_for(g, 0.05, 400.0, 0.01);
            assert!(s <= last);
            last = s;
        }
    }

    #[test]
    fn ols_recovers_an_exact_line_with_zero_error() {
        let ys: Vec<f64> = (0..50).map(|i| 3.0 + 2.5 * i as f64).collect();
        let (slope, se) = ols_slope(&ys).unwrap();
        assert!((slope - 2.5).abs() < 1e-9);
        assert!(se.abs() < 1e-9);
        assert!(ols_slope(&ys[..2]).is_none());
    }

    #[test]
    fn plateau_stops_on_a_flat_noisy_series_and_not_on_a_rising_one() {
        // Deterministic pseudo-noise (no rand crate): a fixed zig-zag of +-5.
        let noise = |i: usize| if i % 2 == 0 { 5.0 } else { -5.0 };
        let flat: Vec<f64> = (0..100).map(|i| 100.0 + noise(i)).collect();
        let check = plateau_check(&flat, 100, 50, 100).expect("check fires at 100");
        assert_eq!(check.generation, 99);
        assert!(check.stop, "flat series must stop: {check:?}");

        let rising: Vec<f64> = (0..100).map(|i| 100.0 + 2.0 * i as f64 + noise(i)).collect();
        let check = plateau_check(&rising, 100, 50, 100).unwrap();
        assert!(!check.stop, "rising series must continue: {check:?}");
        assert!(check.window_mean_second_half > check.window_mean_first_half);
    }

    #[test]
    fn plateau_check_only_fires_on_the_schedule() {
        let series: Vec<f64> = (0..150).map(|i| i as f64).collect();
        assert!(plateau_check(&series[..149], 100, 50, 100).is_none(), "149 is not a multiple of 50");
        assert!(plateau_check(&series[..50], 100, 50, 100).is_none(), "fewer than the window");
        assert!(plateau_check(&series[..100], 100, 50, 100).is_some(), "first check at 100");
        assert!(plateau_check(&series, 100, 50, 100).is_some(), "and again at 150");
        assert!(plateau_check(&series, 0, 50, 100).is_none(), "window 0 disables the rule");
    }
}
