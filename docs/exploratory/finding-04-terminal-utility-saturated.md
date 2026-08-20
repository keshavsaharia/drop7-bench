# Finding 04 — The fair-D4 risk constant is saturated; it is not a lever

**Status:** exploratory, evidence tier `development`. Valid negative result.
**Namespace:** `approaches/lifetime-objective/risk-calibration`, run
`runs/RUN-A51D-risk/`, lease `SEEDLEASE-A51D`, seeds `0xa51d0000`–`0xa51d003f`.
**No existing file was modified.** The frozen reference is untouched; this is a
new parameterized candidate that reproduces it exactly at default settings.

## Question

`research/benchmarks/baselines-v1.json` pins `terminalUtility = -1000000`. An
independent audit of the reference
([`audit-02-fair-d4.md`](audit-02-fair-d4.md)) established that leaf points are
*score points* — `kImmediateScoreWeight = 1.0` — so −1,000,000 is 58.8 row rises,
or 294 moves, or **3.2× the entire recorded mean score**. Against a measured
non-death sibling spread of 217–10,924 leaf points, the audit concluded the
decision rule is effectively lexicographic: minimize modeled four-ply death
probability first, maximize the heuristic leaf only as a tie-break.

Since ~94% of Hardcore score is survival
([`finding-01`](finding-01-score-is-survival.md)), how the search prices death
is the single constant most directly aimed at the objective — and it had never
been varied. This experiment varies it.

## Method

A parameterized re-implementation of the depth-4 driver exposes
`terminalUtility`, `depth`, and `chanceSamples` as runtime options. Everything
else — the leaf, chance stratification, canonicalization, cache keying, column
order, work accounting, iterative deepening, and legal fallback — is the
unmodified frozen code, included as a library.

**CHECK gate before any gameplay:** at default parameters the parameterized
driver must select the identical column as `chooseDepth4Action` on every move.
Result: **50 moves compared, 0 mismatches, PARITY OK**.

Six arms, 64 paired games each, same seeds as the reference cohort, 2,000-move
cap, common seeds across arms.

## Result

| Terminal utility | Mean score | Mean moves | Median | Clears/move | Paired Δ vs reference | W–T–L | 95% lower bound on Δ |
| ---: | ---: | ---: | ---: | ---: | ---: | :---: | ---: |
| −50,000,000 | 321,992 | 94.06 | 266,282 | 1.973 | **0** | 0–64–0 | 0 |
| −10,000,000 | 321,992 | 94.06 | 266,282 | 1.973 | **0** | 0–64–0 | 0 |
| −3,000,000 | 321,992 | 94.06 | 266,282 | 1.973 | **0** | 0–64–0 | 0 |
| −1,000,000 *(frozen)* | 321,992 | 94.06 | 266,282 | 1.973 | 0 | — | — |
| −300,000 | 321,652 | 93.98 | 266,282 | 1.973 | −340 | 1–62–1 | −1,039 |
| −100,000 | 320,161 | 92.92 | 264,942 | 1.987 | −1,831 | 4–29–31 | −4,563 |

## Interpretation

**The constant is saturated.** Every value at or beyond −1,000,000 produces
**byte-identical games** — 0 wins, 64 ties, 0 losses, across a fiftyfold
increase in magnitude. The search is already at its risk-averse limit: making
death 50× more expensive changes no decision anywhere in 64 games.

Moving in the other direction only costs. At −300,000 the policy still agrees
with the reference on 62 of 64 games. At −100,000 it finally disagrees often
(35 of 64 games differ) and is worse: −1,831 mean, with a 95% lower bound of
−4,563, losing 31 games and winning 4.

Note the direction of the flow rates at −100,000: clears per move rise slightly
(1.987 vs 1.973) while lifetime *falls* (92.92 vs 94.06 moves). A less
death-averse policy clears marginally more per move and dies sooner. Throughput
alone is not the objective; throughput sustained without dying is.

## What this rules out, and what it points at

This closes a cheap hypothesis: **fair D4 cannot be made to survive longer by
re-pricing death.** The knob is at its stop. Any remaining gap between D4's
94-move mean and the ~294 moves a million-point mean requires must come from
somewhere else.

That "somewhere else" is now better constrained. The search already minimizes
death within its horizon as its first priority. Its horizon is four plies, and a
row rise occurs every five moves, so it observes at most one rise boundary. The
quantity that actually kills it — a clearance deficit of 1.973 against the
required 2.400 clears per move, accumulating over the forty-plus rise cycles
that separate a 94-move game from a 294-move one — is invisible at that horizon
and is carried entirely by a hand-tuned leaf that was calibrated to nothing.

The lever is therefore the leaf's long-horizon content, not the search's risk
appetite. Two follow-ups are motivated directly by this result and by
[`audit-02`](audit-02-fair-d4.md):

1. **Chance strata.** Five stratified samples cannot represent a seven-atom disc
   distribution; the audit measured that an average 2.41 of 7 disc values
   receive zero weight at each node, deterministically. Independently,
   [`audit-03`](audit-03-claim-arithmetic.md) found that the historical
   seven-stratum rejection *reverses sign* under corrected scoring (−164 → +4,336),
   because it lost on score while gaining moves. Both point at the same arm.
2. **A learned long-horizon leaf** predicting survival rather than score. This
   is where the remaining headroom must be, since the risk term is exhausted and
   the horizon is structurally too short.

## Limitations

- 64 paired games on one exploratory lease; the identical-game result for
  magnitudes ≥1,000,000 is exact and needs no statistics, but the −300,000 and
  −100,000 deltas are single-cohort estimates.
- Only the terminal utility was varied. The leaf weights are held at their frozen
  values; a jointly re-tuned leaf could in principle move the saturation point.
- These are the repository simulator's semantics, including the two rise-boundary
  scoring discrepancies documented in
  [`audit-01-engine-fidelity.md`](audit-01-engine-fidelity.md).

## Reproduce

```sh
./approaches/lifetime-objective/risk-calibration/build.sh
./build/lifetime/risk-calibration --parity --seed-start 0xa51d0100 --parity-games 2 --parity-moves 25
for tu in -100000 -300000 -1000000 -3000000 -10000000 -50000000; do
  ./build/lifetime/risk-calibration --terminal-utility $tu --depth 4 \
    --seed-start 0xa51d0000 --games 64 --max-moves 2000 --threads 32 \
    --output runs/RUN-A51D-risk/tu${tu}.json
done
```
