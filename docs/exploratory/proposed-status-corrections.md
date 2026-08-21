# Proposed status corrections — a proposal only, nothing applied

**Status:** proposal. **No file outside `docs/exploratory/` was modified to
produce this document, and none of the edits below has been applied.**
**Written:** 2026-08-21T00:38Z.

[`docs/research/status.md`](../research/status.md) and
[`docs/research/experiment-index.md`](../research/experiment-index.md) were last
substantively written before this session's audits and findings existed.
`status.md` carries a mtime of 2026-08-20 00:35 local, which is before the first
exploratory measurement in this checkout. Seven of its statements are now either
contradicted, superseded, or derivable rather than empirical.

This document writes each proposed edit out in full so that a coordinator can
apply, reject, or defer it as a deliberate act. Per `AGENTS.md`, only the
coordinator edits shared status tables, and only after merging evidence.

## How to read the tiers

Evidence tiers use the vocabulary of [`methodology.md`](../methodology.md) and
[`benchmarks.md`](../benchmarks.md): *proposal/mechanics*, *pilot*,
*development*, *independently replicated development*, *protected validation*,
*final confirmation*. Two of the proposals below rest on recomputation of the
existing record rather than on any new cohort; those are marked
**record audit**, and they open no seeds.

Each proposal ends with one of two verdicts:

- **SAFE NOW** — the evidence is arithmetic, derivational, or a recomputation of
  the retained record, or the edit only *adds a correctly labelled* datapoint
  and *weakens* a claim. Applying it cannot make the record less true.
- **NEEDS REPLICATION** — the evidence is a single development cohort, or the
  edit would change which policy the repository calls its reference. Applying it
  now would promote a 64-game result into a position the benchmark contract
  reserves for replicated evidence.

Summary: **four SAFE NOW, three NEEDS REPLICATION.**

---

## P1 — The 64-game fair-D4 reference mean

**Verdict: SAFE NOW.**

**File:** [`docs/research/status.md`](../research/status.md), lines 7–9 and the
evidence-snapshot row at line 22. The same figure appears at
[`experiment-index.md`](../research/experiment-index.md) line 41 and in
`research/benchmarks/baselines-v1.json`.

**Current wording (status.md:6–10):**

> Corrected-score fair depth-4 expectimax is the strongest dependable reference
> found so far. Its broader 64-game recorded mean is about 308,296 points, far
> below the required one-million-point average.

**Current wording (status.md:22):**

> `| Fair D4: 308,295.578 points and 90.031 moves over 64 games | Detailed ledger | Recorded broader reference cohort |`

**Proposed wording (status.md:6–10):**

> Corrected-score fair depth-4 expectimax is the strongest dependable reference
> found so far. Its broader 64-game ledger mean is about 308,296 points, and an
> independent 64-game reproduction on fresh exploratory seeds measured 321,992
> points at 94.06 moves. Both are far below the required one-million-point
> average.

**Proposed wording (status.md:22)** — two rows replacing one:

> `| Fair D4: 308,295.578 points and 90.031 moves over 64 games | Ledger-recorded as an internal bootstrap comparator inside the regenerative-expert-iteration run (history.md:4234); the 64 seeds, dispersion, censoring, and flow statistics required by methodology.md were not retained | Provisional reference mean pending a re-run under the benchmark contract |`
>
> `| Fair D4 reproduced: 321,991.7 points and 94.06 moves over 64 games, sd 187,502, 0 censored, 0 score-identity violations | Reproduced in this checkout on fresh seeds 0xa51d0000–0xa51d003f | Development tier; single cohort |`

**Evidence:**
[`finding-01-score-is-survival.md`](finding-01-score-is-survival.md) (measured
cohort table; unmodified frozen fair-D4 source, 2,000-move cap, run
`runs/RUN-A51D-d4/`) and
[`audit-03-claim-arithmetic.md`](audit-03-claim-arithmetic.md) proposal D2 (the
provenance and missing-statistics annotation).

**Evidence tier:** *development* for the 321,992 reproduction, single 64-game
cohort. **Record audit** for the annotation of the 308,295.578 figure.

**Why this is safe.** The edit does not replace the ledger number, does not
claim the ledger number is wrong, and does not raise any claim about fair D4's
strength. It adds a second measurement with its own label and cohort, and it
attaches to the ledger number the provenance caveat that audit-03 established by
reading the ledger. Every change is in the direction of weaker, better-labelled
claims. The two numbers are consistent: finding-01 says so explicitly, and the
gap is well inside the cohort's own standard error of roughly ±46,000 at one
standard error given sd = 187,502 over 64 games.

**Limitation to carry into the edit:** the 321,992 figure is one 64-game cohort
on one lease. It is not a replication of the historical cohort — those seeds
were never recorded — and it must not be described as one.

---

## P2a — Record the chance-stratum result as a development finding

**Verdict: SAFE NOW.**

**File:** [`docs/research/status.md`](../research/status.md), the "Most useful
conclusions so far" list, under the existing bullet **"Fair chance handling
matters."**

**Current wording (status.md, conclusions list):**

> - **Fair chance handling matters.** Optimistic, worst-case, or tiny reused
>   reveal samples can rank moves incorrectly.

**Proposed wording — append to that bullet:**

> Quantified in this checkout: the frozen reference draws five stratified
> samples at a chance node while the next visible disc is uniform on seven
> values, so on average 2.41 of the seven values receive zero weight at every
> node, deterministically. A candidate differing from the frozen reference in
> **nothing but the chance-sample count** (five to seven, which makes the
> next-disc expectation exact) measured a mean of 398,498 against 297,327 on a
> previously unread 64-game cohort — a paired delta of +101,171 points and
> +27.50 moves with a one-sided 95% lower bound of **+47,457**, 41–0–23 by game.
> The headline arm is replicated on a second, earlier-read 64-game cohort
> (+71,138, lower bound +5,826). Median, lower quartile and minimum all improve,
> so the gain is not one tail game. The change costs **3.82× the logical work
> per move** and does not fit the frozen 3,200,000 work bound, so it is a new
> candidate under the benchmark contract, not a tuning of the reference.

**Evidence:** [`finding-05-chance-strata.md`](finding-05-chance-strata.md), runs
`runs/RUN-A51D-risk/` and `runs/RUN-A51D-s7confirm/`; the underlying bias
measurement is [`audit-02-fair-d4.md`](audit-02-fair-d4.md) H1.

**Evidence tier:** *development*, with the headline arm replicated across two
64-game cohorts of which **one** was previously unread. Under
[`methodology.md`](../methodology.md) the earlier-read cohort cannot be
confirmation evidence, so this is "development, reproduced once on fresh seeds"
and not yet *independently replicated development*.

**Why this is safe.** It records a measured, well-labelled development result in
a bullet that already says chance handling matters. It changes no status label,
promotes no policy, and states its own work cost, which is the fact most likely
to be lost if it is summarised later.

---

## P2b — Changing which policy the repository calls its reference

**Verdict: NEEDS REPLICATION. Do not apply yet.**

**File:** [`docs/research/status.md`](../research/status.md) line 7, and every
downstream comparator definition including
`research/benchmarks/baselines-v1.json`.

**Current wording:**

> Corrected-score fair depth-4 expectimax is the strongest dependable reference
> found so far.

**Wording that the evidence is trending toward but does not yet support:**

> Corrected-score fair depth-4 expectimax at seven chance strata is the
> strongest dependable reference found so far.

**Why not yet.** Three separate reasons, any one of which is sufficient:

1. **One fresh cohort.** 64 paired games with a score standard deviation of
   38–64% of the mean. finding-05's own limitations section says every row of
   its interaction table other than the headline arm rests on a single cohort.
2. **It is a different work budget, not a different policy setting.** The
   comparison is fixed-depth, not fixed-work. `benchmarks.md` requires that a
   fixed-work strength view and a resource-performance view be published
   separately, and finding-05 shows depth 3 at seven strata matches depth 4 at
   five strata at one-eighth the work. Which configuration is "the reference"
   depends on which view is authoritative, and that has not been decided.
3. **The comparator is pinned by hash.** `baselines-v1.json` pins the current
   comparator and its transitive source hashes. Changing the reference means
   issuing a new baselines manifest and re-pinning, which is a coordinator
   action with its own review, not a documentation edit.

**What would make it safe:** a second fresh 64-game paired cohort under the
benchmark contract with a machine profile attached, a declared work bound, and
both the fixed-work and the resource-frontier views published. finding-05's
`Reproduce` block is exact enough to run this directly.

---

## P3 — The 2.4 / 1.4 flow targets are derivable, not empirical

**Verdict: SAFE NOW. This is the strongest and cheapest correction in this
document.**

**File:** [`docs/research/status.md`](../research/status.md), lines 97–100.

**Current wording:**

> - **Flow matters before spectacular chains.** The task record repeatedly used
>   roughly 2.4 numbered clears and 1.4 reveals per move as the region
>   associated with stable long games. Treat these as diagnostic targets from
>   limited runs, not proven universal thresholds.

**Proposed wording:**

> - **Flow matters before spectacular chains.** The 2.400 numbered clears and
>   1.400 covered reveals per move that the task record used as a diagnostic
>   region are **not empirical and not approximate**. They are 12/5 and 7/5: a
>   five-move cycle inserts 5 placed discs plus 7 risen covered discs onto a
>   49-cell board, so any policy that survives indefinitely must clear at least
>   12 numbered discs and reveal at least 7 covered discs every 5 moves. They
>   are a **necessary** condition for unbounded survival, derived from the rise
>   cadence and board size, not a sufficient one — a policy can meet them
>   locally and still die by stacking a single column into the top row.
>   Fair D4 sustains 1.973 clears and 1.090 reveals per move, a structural
>   deficit of 18% and 22%.

**Evidence:** [`finding-01-score-is-survival.md`](finding-01-score-is-survival.md),
"Lifetime is a conservation law" and the flow-quartile table; the rules being
counted are stated in [`methodology.md`](../methodology.md) §"Game and score
model".

**Evidence tier:** **derivation from the frozen rules.** The arithmetic follows
from the 7-by-7 board and the five-move rise cadence alone; it does not depend
on any cohort. finding-01's 64-game cohort confirms the engine obeys it and
supplies fair D4's measured rates.

**Why this is safe.** The current wording is a *hedge against a number that
needs no hedge*. Replacing "diagnostic targets from limited runs" with the
derivation strictly increases the document's accuracy, and the replacement
introduces its own, correct, hedge — necessary but not sufficient — where the
old one was misplaced. No cohort is being promoted.

**Optional companion, also safe:** the status quo does not say whether the
requirement is *reachable*. [`finding-06-flow-ceiling.md`](finding-06-flow-ceiling.md)
measured that a **privileged** clairvoyant receding-horizon planner sustains
2.3875 clears and 1.3963 reveals per move over 6,000 moves and holds occupancy
flat, so the requirement is not an arithmetic impossibility; and
[`finding-07-fair-planning-ceiling.md`](finding-07-fair-planning-ceiling.md)
measured that **no legal policy tested reached it** — all 87 fair-planner games
ended and every fair arm's board filled monotonically, with the best legal arm
at 2.2309 clears per move, 93.0% of the requirement. If that companion sentence
is added it must carry both halves; the first without the second is the exact
overstatement finding-07 was written to retract.

---

## P4a — The score decomposition

**Verdict: SAFE NOW, as a labelled measurement.**

**File:** [`docs/research/status.md`](../research/status.md), section
"### 2. Hand-written policies and shallow lookahead" or the conclusions list.

**Current wording (status.md:41–44):**

> They showed that immediate score alone is a poor guide: a policy must keep
> revealing covered numbers and preserve future chain structure.

**Proposed addition (a new conclusions bullet, leaving the narrative intact):**

> - **Hardcore score is survival time, measured.** Across 64 fair-D4 games in
>   this checkout, **94.29% of all points came from the flat 17,000-point
>   row-rise bonus, 5.71% from chain waves, and 0.00% from board clears — there
>   were none in 64 games.** Final score correlated with moves survived at
>   **r = 0.9995**. The engine's score identity
>   `score = 17,000·rises + 70,000·boardClears + Σ popperCount·floor(7·depth^2.5)`
>   was asserted on every game and held 64/64. A five-deep seven-disc wave — a
>   spectacular play — is worth 2,737 points, or 0.8 of one row rise. This is a
>   single 64-game development cohort under this repository's simulator
>   semantics; it does not by itself establish that chain-seeking play cannot
>   change the economics.

**Evidence:** [`finding-01-score-is-survival.md`](finding-01-score-is-survival.md),
score-decomposition table and correlation figures, run `runs/RUN-A51D-d4/`.

**Evidence tier:** *development*, single 64-game cohort. The decomposition
percentages are exact *given those games* — the identity is checked per game —
but their generality across policies is not established by one cohort.

**Why this is safe.** It adds a measurement with its cohort, its size and its
scope limit stated in the same sentence, and it explicitly declines the stronger
inference.

---

## P4b — Rewriting the chains-versus-survival framing

**Verdict: NEEDS REPLICATION. Do not apply yet.**

**Wording that the evidence is trending toward but does not yet support:**

> Chain construction is not a lever on Hardcore score. Survival is the only
> objective, and the three jobs the documentation describes — stay alive, open
> the board, prepare chains — are one job with three names.

**Why not yet.** finding-01 measured *fair D4's* score composition on *one*
cohort. A policy that deliberately engineers chains would have a different
composition by construction, and nothing in this repository has measured one at
whole-game scale. The nearest evidence cuts both ways:
[`finding-06-flow-ceiling.md`](finding-06-flow-ceiling.md) found that a
clairvoyant planner *maximising score* over the same windows earns 1.75× the
points per move and reaches wave depth 22, yet **loses on the mean** because it
cuts lifetime by 58% (1,123,130 vs 1,544,461 points; 165.0 vs 396.9 moves). That
is a real and directly relevant observation, but it is 6–8 privileged games
inside the scenario engine, which is pilot-tier evidence, and it says nothing
about a legal chain-seeking policy.

Additionally, the board-clear term is **not** safely measured as zero: see P6
below. audit-01 H2 shows this engine *overpays* a fifth-drop board clear by
70,000 points relative to the cited reference, and the 0.00% board-clear share
observed here means only that no policy tested reached one.

**What would make it safe:** a whole-game comparison of a deliberately
chain-seeking legal policy against fair D4 on a fresh paired cohort, with score
decomposition reported for both arms.

---

## P5 — Two historical rejections reverse sign under corrected scoring

**Verdict: SAFE NOW.**

**File:** [`docs/research/experiment-index.md`](../research/experiment-index.md),
lines 124 and 125. A companion edit to `docs/strategies.md:157–162` is proposed
by audit-03 as D8.

**Current wording (experiment-index.md:125, seven-stratum D4 row):**

> **Rejected — ledger-recorded;** it was score-neutral to worse, reduced flow,
> and cost about 3.8 times more work.

**Proposed wording:**

> **Rejected under 7,000-point Sequence scoring — ledger-recorded.** Its
> −163.63-point mean gap **reverses to +4,336 when rescored to the corrected
> 17,000-point bonus** (identical trajectories), so the score-mean rejection
> does not hold under Hardcore rules. The flow regression (clears 1.8875→1.8598,
> reveals 1.0349→0.9923) and the 3.79× work cost are scoring-independent and
> remain valid grounds for not adopting it. **A corrected-scoring re-test is a
> live open question**, and an independent re-test at depth 4 in this checkout
> found seven strata worth +101,171 points (95% lower bound +47,457) on a fresh
> 64-game cohort — see [`finding-05-chance-strata.md`](finding-05-chance-strata.md).

**Current wording (experiment-index.md:124, fair-leaf CEM row):**

> **Rejected — ledger-recorded;** the D3 screen and D4 heldout/tail gates
> failed.

**Proposed wording:**

> **Rejected under 7,000-point Sequence scoring — ledger-recorded.** The D3
> fresh screen's −905-point mean gap **reverses to +2,845 under corrected
> scoring**, so that screen no longer rejects the candidate; the independent D4
> interaction heldout (−17,835 points, −10,938 moves, worst-quartile regression)
> fails under both scoring modes and is the surviving ground for rejection.

**Evidence:** [`audit-03-claim-arithmetic.md`](audit-03-claim-arithmetic.md),
proposals D4 and D5, derived from an exact paired-rescoring identity applied to
the retained ledger rows at `history.md:1287–1294` and `history.md:1158–1163`.
The trajectories are identical between scoring modes, so the rescoring is
arithmetic, not re-simulation.

**Evidence tier:** **record audit.** No cohort was opened; the audit recomputes
numbers the ledger already retains. The finding-05 sentence appended to the
first row is *development* tier and is labelled as such by its link.

**Why this is safe.** The proposed text does not claim either candidate is good.
It corrects a *scoring-mode label* on a rejection and states precisely which
grounds survive the correction — for the seven-stratum row, the flow regression
and work cost; for the CEM row, the D4 interaction heldout. Both rows still read
"Rejected". This is the narrowest possible repair of a claim that is currently
stated without its scoring mode.

**Do not overreach.** audit-03 is explicit that a sign reversal on a rejection
gate is not evidence the candidate would have passed a full protocol; it means
that specific gate no longer rejects. `status.md`'s existing line "A failed
experiment rejects only the exact tested configuration" applies in both
directions.

---

## P6 — The permanently unsatisfiable frozen protocol gate

**Verdict: SAFE NOW — as a superseding record. Never as an edit.**

**File:**
[`artifacts/protocols/optimistic-phase-ntuple/protocol.json`](../../artifacts/protocols/optimistic-phase-ntuple/protocol.json),
line 101.

**Current content (line 101, inside `at100000000Transitions.qualificationRequires`):**

> `"corrected D4 reproduces frozen means 176925.25 score and 116.375 moves"`

**The defect.** 176,925.25 is the **7,000-point Sequence-scored** mean of the
eight-game confirmation cohort `0x3e9c0000…0x3e9c0007` (`history.md:970`). The
same cohort's corrected 17,000-point Hardcore mean is **400,675.25**
(`audit-03`, line 58; also `status.md:21`). A run under corrected scoring can
never reproduce 176,925.25, so this clause of the gate is **permanently false**
and the qualification condition it belongs to can never be satisfied. The move
figure, 116.375, is scoring-independent and is correct.

**Proposed action — do NOT edit the artifact.** `research/README.md` states that
a preregistered protocol is immutable and that corrections create a successor
record; `AGENTS.md` states that frozen protocol artifacts must not be edited and
that historical hashes must not be rewritten. Instead:

1. **Register a superseding, versioned protocol** — a new file alongside the
   original, for example
   `artifacts/protocols/optimistic-phase-ntuple/protocol-v2.json`, or a new
   `research/experiments/EX-…` record produced through
   `researchctl new experiment` and frozen with `researchctl freeze` so it
   carries a correct self-excluding `protocolSha256`. Its corresponding clause
   reads:

   > `"corrected D4 reproduces frozen means 400675.25 score and 116.375 moves on 0x3e9c0000-0x3e9c0007 (history.md:3337)"`

2. **Have the successor name its predecessor explicitly** and state that v1 is
   retained unaltered and is unsatisfiable as written.
3. **Record the v1 defect in [`provenance.md`](../provenance.md)** alongside the
   existing note about relocated source hashes, so that anyone resuming the
   optimistic-phase-ntuple line finds the defect before spending compute on a
   gate that cannot pass.
4. **Leave `protocol.json` byte-identical.** Its hash is the record.

**Evidence:** [`audit-03-claim-arithmetic.md`](audit-03-claim-arithmetic.md),
proposal D17 and the surrounding derivation at lines 589–594; the mode
classification of the source row at line 142.

**Evidence tier:** **record audit.** This is an internal-consistency defect in a
frozen artifact, established by comparing two retained figures for the same
cohort. No cohort was opened and no policy claim is involved.

**Why this is safe.** Nothing is modified. A successor record is created and the
predecessor is retained, which is exactly the mechanism the repository
prescribes for correcting an immutable record.

---

## P7 — Two rise-boundary scoring divergences from the cited reference

**Verdict: NEEDS REPLICATION — for the *engine change*. The *caveat* is SAFE
NOW.**

**File:** [`docs/research/status.md`](../research/status.md), section
"### 1. Establishing a trustworthy simulator", lines 34–38, and the
evidence-snapshot row at line 18.

**Current wording (status.md:34–38):**

> The work first aligned TypeScript and native Hardcore rules, chain scoring,
> gray-disc reveals, row rises, and deterministic seed behavior. A scoring audit
> corrected the five-move mode's level award from 7,000 to 17,000.

**Proposed addition (SAFE NOW) — append to that paragraph:**

> An independent engine-fidelity audit in this checkout found two **remaining**
> scoring divergences from the reference implementation this repository itself
> cites (`drop7.es6`), both at the rise boundary, both reproduced with
> executable evidence:
>
> - **H1, the terminating rise.** When the fifth drop of a level completes and
>   the rise is refused because row 0 is occupied, this engine sets game-over
>   and awards nothing. The reference adds the level bonus *first*, then shifts,
>   then detects overflow afterwards — its grid has an 8th buffer row precisely
>   so the rise always succeeds. **254 of 256 parity games (99.2%) terminate
>   exactly this way**, so every mean, lower bound and paired delta in this
>   repository is biased **down** by one 17,000-point level bonus per game. The
>   bias is one-sided and conservative: it cannot manufacture a false
>   million-point claim.
> - **H2, the fifth-drop board clear.** This engine tests board-clear *before*
>   the rise; the reference tests it *after* the rise has already inserted a new
>   gray row, so a board cleared on the fifth drop is not empty when the
>   reference looks. Measured on an empty board: this engine scores 87,007 where
>   reference ordering scores 17,007. The divergence is **+70,000, upward, and
>   policy-controllable** — one board clear in five falls on a fifth drop by
>   position alone, and a search that can see the rise coming has a direct
>   incentive to schedule clears there. That is the dangerous direction for a
>   qualification claim.
>
> Both engines agree with each other exactly; they agree on rules that differ
> from the cited reference. Until this is resolved, an **absolute** score from
> this engine is a score in this repository's dialect of Drop7. Within-checkout
> paired comparisons are not affected by H1, which is constant per game.

**Proposed addition to the evidence-snapshot table (SAFE NOW)** — one row:

> `| Engine fidelity vs. the cited reference implementation | Independent audit, executable reproductions | Two rise-boundary scoring divergences (H1 terminal level bonus, H2 fifth-drop board clear) plus an opening-position divergence; see audit-01 |`

**Evidence:** [`audit-01-engine-fidelity.md`](audit-01-engine-fidelity.md),
findings H1 and H2, each with cited `engine.hpp` / `engine.ts` line ranges,
cited `drop7.es6` line ranges, and a measured single-move reproduction. The
audit's own verdict is `valid` / `fail` / *proposal-mechanics*.

**Evidence tier:** *proposal/mechanics*. This is a mechanics-level verification
against public source; no development, protected or final cohort was opened.
That is a **strong** tier for this kind of claim — it is a reproducible
statement about code, not a statistical estimate — which is why the caveat is
safe to add immediately.

**What NEEDS REPLICATION, and why it is not proposed here.** Changing the engine
to match the reference is *not* proposed and should not be done as a
documentation action. It would:

- change every score in the repository, breaking comparability with the entire
  historical ledger and with `baselines-v1.json`'s pinned comparator;
- require a decision the repository has never recorded — namely whether
  `drop7.es6` is normative for this research program or merely cited; and
- under [`benchmarks.md`](../benchmarks.md), make every existing policy a new
  candidate, since the change alters floating-point action ranking and the
  selected column near rise boundaries.

The correct sequence is: record the caveat now (safe), then decide normativity,
then, if the engine is changed, do it as a new versioned engine with a fresh
parity gate and re-baselined comparator — never as a fix applied in place.

---

## Not proposed, and why

Several other corrections are live in the audits but are deliberately **not**
raised here, to keep this document to the seven items in scope:

- `audit-03` proposals D1, D3, D6, D7, D9–D16 — further ledger and
  `strategies.md` annotations, a seed registry, and a `game-result-v1` schema
  change adding a required `scoringMode`. D16 is worth a coordinator's attention
  because it makes the score identity machine-checkable per game and retires a
  whole class of the defects audit-03 found.
- `audit-01` finding H3 (opening position) and H4 (a `benchmarks.md:31`
  protocol violation).
- `audit-04` and `audit-05` conclusions about confounded rejections and the
  never-ablated curriculum ordering.
- **[`finding-08-learned-leaf.md`](finding-08-learned-leaf.md)**, which was
  published at 17:41 local **while this document was being written** and is
  therefore outside its scope. Two things in it bear directly on the proposals
  above and should be read before applying them. First, it **strengthens P2a**:
  its two `w = 0` reference arms are the frozen policy bit-for-bit and reproduce
  finding-05's published confirmation cohort *to every reported digit* for both
  the five- and seven-stratum arms, with 0 score-identity violations in 26,934
  decisions and 0 censored games in 256, and its measured 4,956,614 work per
  move independently confirms the seven-stratum arms completed depth 4 rather
  than silently degrading to depth 3. Second, it **bears on how P2b should
  eventually be worded**: a learned leaf and an exact chance estimator behave as
  *substitutes*, not complements — seven strata is worth +101,171 over five with
  the frozen leaf but only +79,347 with a learned one — so "which configuration
  is the reference" is entangled with "which leaf", and P2b should not be
  applied without that. finding-08's own gameplay arms are `development` tier on
  a **doubly read** cohort and support no status edit on their own.
- Anything concerning the concurrent contributor's afterstate line. Its records
  are complete, self-consistent and passing validation, and its own row in
  `experiment-index.md` is already written; it needs nothing from this document.
  See [`reconciliation-01.md`](reconciliation-01.md).

## Application checklist for the coordinator

If P1, P2a, P3, P4a, P5 and P7's caveat are applied, `AGENTS.md`'s definition of
done requires that the following agree afterwards: the edited document, the
finding or audit each edit cites, the run directory each finding cites, and a
contribution record for whoever applies the edit. P6 additionally requires a new
frozen protocol record and a `provenance.md` note, and must leave
`protocol.json` untouched.
