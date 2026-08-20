# Preregistration — rollout-veto-17k SCREEN

Written and frozen **before** any cohort seed in `0xa51e0000..0xa51e001f` was
read. The only seeds read before this file existed are the two runtime-probe
seeds `0xa51e3f00` (candidate, capped at 40 moves) and `0xa51e3f10`
(comparator), which are excluded from the cohort and from every gate below.

## Candidate

`approaches/lifetime-objective/rollout-veto-17k/veto.cpp`, built with
`clang++ -O3 -std=c++20 -pthread`.

Fair D4 (full width, completed depth 4, five stratified chance samples) decides
every move. A decision is **routed** when the maximum column height is >= 4.
At a routed decision every legal root action is rolled forward 25 synthetic
moves under 7 common, exactly stratified scenarios; after the candidate move
every continuation move is a fresh completed full-width fair D2 search fed only
the public observable state. An alternative may veto the D4 action only if it
jointly satisfies

1. surviving scenarios >= D4's surviving scenarios,
2. mean numbered clears >= D4's mean numbered clears,
3. paired one-sided t lower bound (t(0.975, df=6) = 2.446912) on the 7-scenario
   return difference > 0, and
4. D4 root-Q loss <= 17,000.

Among passing alternatives the largest return lower bound wins.

## Comparator

Unmodified fair D4 (`--baseline-only`, identical binary, rollout disabled),
same ordered seeds, same move cap.

## Cohort

- Tier: `SCREEN` (32 paired games).
- Seeds: `0xa51e0000` .. `0xa51e001f`, lease `SEEDLEASE-A51D-VETO`.
- Data role: exploratory development diagnostic. Once read, never confirmation.
- Move cap: **600**, a declared diagnostic cap below the 2,000-move contract
  cap, applied identically to both arms. Justification: the longest fair-D4
  game anywhere in the ledger is 285 moves and the longest rollout-veto game is
  250, so 600 gives >2x headroom while bounding the worst-case cohort wall time.
  Any censored game is reported and its score treated as a lower bound.
- Threads: 8. Bootstrap: 20,000 percentile resamples over whole games, RNG
  domain `0xa51e5eed` (score) and `0xa51e6eed` (moves).

## Pass / fail rule

**PASS** requires all five:

| ID | Condition |
| --- | --- |
| G1 | paired mean score delta (candidate - D4) > 0 |
| G2 | one-sided 95% whole-game bootstrap lower bound on the paired score delta > 0 |
| G3 | paired mean move delta > 0 |
| G4 | score wins >= 20 of 32 |
| G5 | candidate numbered clears/move >= D4's **and** candidate covered reveals/move >= D4's |

**FAIL** if G1 or G2 fails.

**FAIL-on-design** (a distinct, still-valid negative): if fewer than 1 veto is
taken per 50 veto opportunities across the cohort, the deployed policy is
effectively unmodified fair D4 and the *architecture* — not the idea of a long
rollout — is what the cohort rejects. No strength claim is made in either
direction in that case.

**INCONCLUSIVE** only if runner failures > 0, score-decomposition identity
failures > 0, or the cohort cannot be completed.

## Root-Q band ablation (secondary, non-gating)

`kMaximumRootQLoss` was written in the original as "one canonical level bonus"
and evaluated to 7,000; the corrected band is 17,000. Both are exposed as
`--root-q-loss`. Because the four veto conditions are ANDed, the two bands
produce **identical** policies whenever every alternative already fails
condition 3. The cohort records `returnRejections` and `alternativesConsidered`
separately, so this equality is checkable rather than assumed. If it does not
hold, a separate 8-game candidate arm at `--root-q-loss 7000` is run on
`0xa51e0000..0xa51e0007` and reported as an ablation.

## Explicit anti-tuning statement

The 404,047-point / 250-move pilot on `0x3ded0000` is a **single game** under a
wrong level bonus. It is the reason this retest exists and it is **not** a
tuning target. No parameter of this candidate was chosen by looking at any seed
in the cohort above, and the cohort is fresh within its lease.
