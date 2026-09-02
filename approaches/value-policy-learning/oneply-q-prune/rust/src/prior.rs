// Sibling-ranking priors.  `rank` orders a node's legal columns best-first;
// ties keep the search's centre-first column order because the sort is
// stable over a list already in that order.

use drop7_kf_linear_q::features::{features, FEATURE_COUNT};
use drop7_kf_linear_q::learn::{load_weights, q_of};
use drop7_kf_linear_q::view::PublicView;
use drop7_rs::engine::State;
use drop7_rs::leaf::LeafScratch;
use drop7_rs::search::SearchParams;

use crate::oneply::{dot, load_oneply_weights, oneply, ONEPLY_COUNT};

#[derive(Clone, Debug)]
pub enum Prior {
    /// The search's own column order (3, 2, 4, 1, 5, 0, 6): the null prior.
    Center,
    /// The six Klein-Friedmann drop features with frozen weights; no engine
    /// calls, so zero logical work.
    Kf([f64; FEATURE_COUNT]),
    /// The exact depth-1 value under the node's own chance scenarios.
    D1,
    /// A fitted linear Q over the 32 one-ply features.
    Lq(Box<[f64; ONEPLY_COUNT]>),
}

impl Prior {
    /// `center` | `kf=FILE` | `d1` | `lq=FILE`
    pub fn parse(spec: &str) -> Result<Prior, String> {
        if spec == "center" {
            return Ok(Prior::Center);
        }
        if spec == "d1" {
            return Ok(Prior::D1);
        }
        if let Some(path) = spec.strip_prefix("kf=") {
            return Ok(Prior::Kf(load_weights(path)?));
        }
        if let Some(path) = spec.strip_prefix("lq=") {
            return Ok(Prior::Lq(Box::new(load_oneply_weights(path)?)));
        }
        Err(format!("unknown prior {spec:?}; expected center, d1, kf=FILE or lq=FILE"))
    }

    pub fn name(&self) -> &'static str {
        match self {
            Prior::Center => "center",
            Prior::Kf(_) => "kf",
            Prior::D1 => "d1",
            Prior::Lq(_) => "lq",
        }
    }

    /// Fill `order` with `legal` best-first.  Returns the logical work spent.
    pub fn rank(
        &self,
        state: &State,
        legal: &[usize],
        params: &SearchParams,
        depth: i32,
        scratch: &mut LeafScratch,
        order: &mut Vec<usize>,
    ) -> u64 {
        order.clear();
        let mut scored: Vec<(f64, usize)> = Vec::with_capacity(legal.len());
        let mut work = 0u64;
        match self {
            Prior::Center => {
                order.extend_from_slice(legal);
                return 0;
            }
            Prior::Kf(weights) => {
                let view = PublicView::from_state(state);
                for &column in legal {
                    scored.push((q_of(weights, &features(&view, column)), column));
                }
            }
            Prior::D1 => {
                for &column in legal {
                    let one = oneply(state, column, params, depth, scratch);
                    work += one.work;
                    scored.push((one.d1_value, column));
                }
            }
            Prior::Lq(weights) => {
                for &column in legal {
                    let one = oneply(state, column, params, depth, scratch);
                    work += one.work;
                    scored.push((dot(weights, &one.values), column));
                }
            }
        }
        // Stable descending sort: equal scores keep the centre-first order.
        scored.sort_by(|a, b| b.0.partial_cmp(&a.0).expect("finite prior scores"));
        order.extend(scored.iter().map(|(_, column)| *column));
        work
    }
}
