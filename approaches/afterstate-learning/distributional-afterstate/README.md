# Distributional afterstate ranker

Candidate for experiment
[`EX-20260820-afterstate-pilot-h40-29b8588a`](../../../research/experiments/EX-20260820-afterstate-pilot-h40-29b8588a.json),
testing theory
[`TH-20260820-distributional-afterstate-ranker-7aba7fb3`](../../../research/theories/TH-20260820-distributional-afterstate-ranker-7aba7fb3.json).

## Idea

Train one **action-free** evaluator of the fully resolved public afterstate
(board, next visible disc, moves until rise) on a **successor-closed** corpus:
at every harvested root, *every* legal sibling is labeled under the same
aligned chance scenarios with a fixed public continuation policy. Because the
same network scores every candidate afterstate, action identity cannot act as
a shortcut and no deployment-time action is out of support: the two failure
modes that defeated earlier played-action and sparse-sibling learners here.

Deployment (later tier, only after the offline gate passes): 1-ply
chance-averaged greedy over legal afterstates with exact fair-D4 fallback.

## Layout

- `generate-corpus.cpp` — native corpus builder: harvests roots from fair-D1
  development games on the leased seed range, labels every legal sibling with
  K aligned H-move continuation scenarios.
- `label-d4.cpp` — exact fair-D4 root rankings for comparator folds.
- `self-test.cpp` — seed-free CHECK-tier mechanics, legality, determinism,
  reflection, and information-boundary tests.
- `train.py` — PyTorch (ROCm) trainer: quantile + within-root ranking losses,
  whole-origin splits, frozen gate evaluation.
- `build.sh` — namespaced build into `build/afterstate/`.

## Isolation

Build output goes to `build/afterstate/`; run output goes to
`runs/<run-id>/`. Seeds come only from lease
[`SL-20260820T083000Z-5da70000`](../../../research/seeds/leases/SL-20260820T083000Z-5da70000.json)
(public-development). No existing approach directory, historical artifact, or
other agent's file is modified.
