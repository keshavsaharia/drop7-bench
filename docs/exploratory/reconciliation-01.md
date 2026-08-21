# Reconciliation 01 — Two concurrent contributors in one working tree

**Status:** bookkeeping. No gameplay, no seed opened, no scientific claim.
**Written:** 2026-08-21T00:38Z (2026-08-20 17:38 local, UTC−7).
**Nothing was modified to produce this memo.** It is a read-only map of who
owns what, plus the contradictions and gaps found while reading. Where a
document should change, the change is proposed in
[`proposed-status-corrections.md`](proposed-status-corrections.md), not applied.

## Why this exists

Two independent model contributors worked in this checkout on 2026-08-20 at the
same time. Neither one's records describe the other's files, the two use
different namespaces and different record systems, and the single Git commit
that contains most of the work bundles both tracks under one human author with
no attribution trailers. Whoever picks this up next needs to know which half of
the tree is which before touching anything.

Read this together with [`lease-map.md`](lease-map.md), which is contributor A's
own allocation table, and with `research/contributions/`, which is the
machine-readable attribution layer.

## Method and its limits

Ownership below is inferred from four kinds of evidence, in decreasing strength:

1. **A contribution, theory, experiment, run, result, dataset, or lease record
   that names the artifact.** Strongest; this is direct self-attribution.
2. **A file's own header.** Most `docs/exploratory/*.md` files declare their
   namespace, run directory and seed lease in the first ten lines.
3. **Naming convention.** Contributor A writes `runs/RUN-A51D-*`,
   `runs/RUN-A52-*`, `runs/RUN-FLOW-*`, `build/lifetime*`, `build/scenario`,
   `build/flow-ceiling`, and `README.md`. Contributor B writes
   `runs/RUN-<ISO8601>Z-<hex>` matching its own record IDs, `build/afterstate`,
   and `README.mdx`.
4. **Modification time.** Weakest, and only used to corroborate. Local time is
   UTC−7 in this checkout, so a file stamped `03:02` corresponds to a record
   `recordedAt` of `10:02Z`.

**Git does not attribute anything here.** There are two commits, `ac7f04e`
(initial workspace, 2026-08-20 00:33 local) and `9f464d8` (2026-08-20 11:05
local, 76 files, 18,133 insertions). `9f464d8` bundles both contributors' source
into one commit, is authored by the human Git identity, and carries no
`Contribution-ID` trailer, contrary to the convention in
[`contributions-and-commits.md`](../agents/contributions-and-commits.md). A
further nine files are modified in the working tree and roughly 100 `README.mdx`
files are untracked. Do not read commit authorship as model authorship.

**The tree was still moving while this was written.** `runs/RUN-A52-LEAF/eval`
and `runs/RUN-A52-LEAF/stage2.log` carry mtimes of 17:32–17:33 local, minutes
before this memo. At least one work package was still executing. Treat every
listing below as a snapshot.

## The two contributors

| | Contributor A | Contributor B |
| --- | --- | --- |
| Platform | `claude-code` | `OpenCode` |
| Model string | `claude-opus-5[1m]` | `moonshotai/Kimi-K3` |
| Structure | one coordinator plus delegated work packages, each with its own `agentId` | single actor, `agentId` null in both records |
| Contribution records | 11 — `CT-…T1002…` ×8 and `CT-…T100914Z` written by A's own coordinator, plus `CT-20260821T003851Z-1134f3d2` (flow-ceiling) and `CT-20260821T004637Z-e804cca0` (learned-leaf) transcribed by this session | 2 (`CT-20260820T140540Z-f8458dc0`, `CT-20260820T190031Z-48aff910`) |
| Record system used | prose under `docs/exploratory/`; contribution records only | full machine-readable `research/` records: theory, experiment, run, result, dataset, seed lease |
| Namespace | `approaches/lifetime-objective/`, `docs/exploratory/` | `approaches/afterstate-learning/`, `src/bench/`, `web/` |
| Seed family | `0xa51d…`, `0xa51e…`, `0xa52…` | `0x5da7…` gameplay, `0x5eed…` scripted rounds |

A third, earlier contributor (`codex`, models `GPT-5` / `gpt-5.6-terra` /
`unknown`) authored the governance layer in commit `ac7f04e` — `AGENTS.md`,
`docs/agents/`, `docs/benchmarks.md`, `research/schemas/`, `researchctl.py`. It
is not active in this session and is listed here only so its files are not
mistaken for either current track.

## File and directory map

### Contributor A — `claude-code` / `claude-opus-5[1m]`

| Path | Evidence | Confidence |
| --- | --- | --- |
| `docs/exploratory/audit-01…05`, `finding-01…07`, `design-01`, `gpu-01…03`, `lease-map.md`, `README.md` | named in contribution records `CT-…T1002{50,51,52,53,54,55}Z`, `CT-…T100914Z`, `CT-…T100248Z`, `CT-…T100249Z`; each file's own header declares the namespace | high |
| `approaches/lifetime-objective/common/`, `score-decomposition/`, `risk-calibration/`, `sibling-corpus/`, `afterstate-net/` | `CT-20260820T100249Z-b1ea4b1a` `artifactPaths` | high |
| `approaches/lifetime-objective/scenario/` | `CT-20260820T100255Z-a47bcb43` `artifactPaths` | high |
| `approaches/lifetime-objective/gpu/` | `CT-20260820T100914Z-27d66f30` `artifactPaths` | high |
| `approaches/lifetime-objective/flow-ceiling/` | finding-06/07 headers; `CT-20260821T003851Z-1134f3d2` (transcribed) | high |
| `approaches/lifetime-objective/rollout-veto-17k/` | `finding-03-rollout-veto-17k.md` header; same lease family | high |
| `approaches/lifetime-objective/learned-leaf/`, `docs/exploratory/finding-08-learned-leaf.md`, `docs/exploratory/gpu-03-onednn-conv-nondeterminism.md` | finding-08 header declares the namespace, run and lease; `PREREGISTRATION.md` declares `SEEDLEASE-A52-LEAF`; `CT-20260821T004637Z-e804cca0` (transcribed) | high |
| `runs/RUN-A51D-*`, `runs/RUN-A52-LEAF`, `runs/RUN-FLOW-*` | naming convention plus explicit citation in the matching finding | high |
| `build/lifetime/`, `build/lifetime-leaf/`, `build/scenario/`, `build/flow-ceiling/` | build scripts in the matching approach directories write there | high |
| `.venv-rocm`, `.venv-rocm-therock` | named in `CT-20260820T100914Z-27d66f30` summary | high |
| `runs/torch-install.log`, `runs/torch-install2.log` | same package, same timestamps as the two ROCm venvs | medium |

**Contributor A wrote no `research/` record other than contribution records.**
Its theories, protocols, gates, cohorts and results exist only as prose in
`docs/exploratory/`. That is a deliberate isolation choice stated in
`docs/exploratory/README.md`, but it means A's work is invisible to
`make research-validate` and to any index built from `research/`.

### Contributor B — `OpenCode` / `moonshotai/Kimi-K3`

| Path | Evidence | Confidence |
| --- | --- | --- |
| `approaches/afterstate-learning/distributional-afterstate/` | `CT-20260820T140540Z-f8458dc0`; the directory README cites B's own theory and experiment records | high |
| `research/theories/TH-20260820-distributional-afterstate-ranker-7aba7fb3.json` | named in `CT-…T140540Z` `theoryIds` | high |
| `research/experiments/EX-20260820-afterstate-pilot-h40-*`, `EX-20260820-d4-toptwo-override-gate-*` | same experiment family, referenced from B's results | high |
| `research/results/RS-20260820T{0945,1145,1425,1845}00Z-*` | each names an `EX-…afterstate…` experiment | high |
| `research/runs/RUN-20260820T{0825,0904,1116,1757}*Z-*` | each names an `EX-…afterstate…` experiment | high |
| `research/datasets/DS-20260820-afterstate-h40-*` | naming plus experiment reference | high |
| `research/seeds/leases/SL-20260820T083000Z-5da70000.json` | `experimentId` is B's pilot experiment | high |
| `runs/RUN-20260820T*` | run-record IDs match exactly | high |
| `build/afterstate/`, `.venv-afterstate` | `distributional-afterstate/build.sh` targets `build/afterstate/`; the venv name matches the family | high |
| `src/bench/`, `web/`, `docs/d7p-protocol.md` | `CT-20260820T190031Z-48aff910` `artifactPaths` | high |
| `src/core/typescript/engine.ts` latent-mode change (uncommitted, +114/−…) | same record claims "optional latent-board mode in the TypeScript engine" | high |
| ~100 untracked `approaches/**/README.mdx` files | same record claims "12 hand-written family guides, 3 visual learn pages, 91 generated approach starter pages"; all carry mtime 11:56 local, including files placed inside contributor A's directories | high |
| Working-tree edits to `AGENTS.md`, `README.md`, `.gitignore`, `package.json` | all four diffs add `src/bench`, `web/`, or the D7P protocol | high |
| Working-tree edit to `docs/research/experiment-index.md` (+1 row) | the added row cites B's own override-gate experiment and result | high |

### Shared or undetermined

| Path | What is known | Confidence |
| --- | --- | --- |
| `research/system-profiles/MACH-20260820T080056Z-376ada90.json` | captured `2026-08-20T08:00:56Z`; referenced by **both** tracks — by B's four run records and one result record, and by A's `docs/exploratory/gpu-01-rocm-enablement.md`. No record claims authorship. | **low — genuinely undetermined.** Treat as shared infrastructure; do not delete or regenerate. |
| `build/fair-depth4`, `build/native-suite` | built 00:40–00:41 local, before either track's first artifact; these are the repository's own reference builds | medium |

## Cross-track writes

Three places where one contributor wrote inside the other's territory. All are
recorded here as facts, not as complaints.

1. **Contributor B placed `README.mdx` files inside contributor A's approach
   directories** (`approaches/lifetime-objective/scenario/README.mdx`,
   `.../gpu/README.mdx`, `.../afterstate-net/README.mdx`, and others, all at
   11:56 local). They are generated console pages, they do not modify A's
   sources, and A's own hand-written `README.md` files in `flow-ceiling/` and
   `learned-leaf/` sit beside them untouched. The two extensions are a reliable
   discriminator: `.md` is A, `.mdx` is B.
2. **Contributor B edited `docs/research/experiment-index.md`**, a shared status
   document. `AGENTS.md` states that only the coordinator edits shared status
   tables after merging evidence. The edit is a single well-sourced row about
   B's own override-gate result and is uncommitted. Flagged, not judged.
3. **Contributor B edited `AGENTS.md`**, adding a "Tooling: benchmark playground
   and web console" section. `AGENTS.md` is the agent contract both
   contributors are bound by. Also uncommitted.

Contributor A wrote nothing inside contributor B's namespace. A's finding-07
header states explicitly that `runs/RUN-20260820T*` was not modified.

## Seed leases

Every range claimed by anyone in this checkout, plus the frozen banks.

| Lease | Range | Games | Role | Owner | Recorded state | Where recorded |
| --- | --- | ---: | --- | --- | --- | --- |
| `SEEDLEASE-A51D` eval | `0xa51d0000`–`0xa51d003f` | 64 | development, paired evaluation | A | opened | `lease-map.md` |
| `SEEDLEASE-A51D` parity | `0xa51d0100`–`0xa51d0101` | 2 | development, CHECK probes | A | opened | `lease-map.md` |
| *(unnamed)* confirmation | `0xa51d1000`–`0xa51d103f` | 64 | development, fresh paired confirmation | A | opened | `finding-05`, `learned-leaf/PREREGISTRATION.md` — **not in `lease-map.md`** |
| `SEEDLEASE-A51D` smoke | `0xa51d8000`–`0xa51d800f` | 16 | development, smoke | A | opened | `lease-map.md` |
| `SEEDLEASE-A51D` withdrawn | `0xa51d9000`–`0xa51dcfff` | 16,384 | withdrawn after the overlap incident | A | opened, output destroyed | `lease-map.md` |
| `SEEDLEASE-A51D-SCEN` | `0xa51dc000`–`0xa51dffff` | 16,384 | development, scenario minting | A | opened | `lease-map.md` |
| `SEEDLEASE-A51D-VETO` | `0xa51e0000`–`0xa51e3fff` | 16,384 | development, paired SCREEN | A | opened | `lease-map.md` |
| `SEEDLEASE-A52` d2 | `0xa5200000`–`0xa520ffff` | 65,536 | training | A | opened | `lease-map.md` |
| `SEEDLEASE-A52` d1 | `0xa5210000`–`0xa5213fff` | 16,384 | training | A | opened | `lease-map.md` |
| `SEEDLEASE-A52` d3 | `0xa5214000`–`0xa5214fff` | 4,096 | training | A | opened | `lease-map.md` |
| `SEEDLEASE-A52` d4 | `0xa5215000`–`0xa52152ff` | 768 | training | A | opened | `lease-map.md` |
| `SEEDLEASE-A52` reserve | `0xa5216000`–`0xa52fffff` | — | training, unallocated | A | reserved | `lease-map.md` |
| `SEEDLEASE-A52-FLOW` | `0xa5230000`–`0xa5233fff` | 16,384 | development, flow-ceiling game seeds | A | opened | `flow-ceiling/README.md`, finding-06/07 — **not in `lease-map.md`** |
| `SEEDLEASE-A52-FLOW2` | `0xa5234000`–`0xa5237fff` | 16,384 | development, hidden-board sampler | A | opened | `flow-ceiling/README.md`, finding-07 — **not in `lease-map.md`** |
| `SEEDLEASE-A52-LEAF` | `0xa5240000`–`0xa5247fff` | 32,768 | development, learned-leaf tuning | A | opened | `learned-leaf/PREREGISTRATION.md`, `learned-leaf/search.cpp` — **not in `lease-map.md`** |
| `SL-20260820T083000Z-5da70000` | `0x5da70000`–`0x5da70fff` | 4,096 | public-development | B | **contradictory — see below** | `research/seeds/leases/` |
| *(declared lease-free)* | `0x5eed0001`–`0x5eed0008` | 8 rounds | scripted-round generator seeds | B | n/a | `src/bench/rounds/*.json`; `AGENTS.md` edit says scripted rounds "consume no seed lease" |
| Frozen protected bank | `0x7d000000`–`0x7d00ffff` | 65,536 | protected validation | — | **unopened** | `research/README.md` |
| Frozen final bank | `0xd7000000`–`0xd70000ff` | 256 | final confirmation | — | **unopened** | `research/README.md` |

### Overlap between the two contributors: none

Contributor A's claims are entirely inside `0xa51d0000`–`0xa52fffff`.
Contributor B's claims are `0x5da70000`–`0x5da70fff` and the eight `0x5eed000…`
round generators. The largest value B claims, `0x5eed0008`, is below the
smallest value A claims, `0xa51d0000`. **The two contributors' ranges are
disjoint. No seed is claimed by both. Neither contributor's range touches the
frozen protected or final banks.**

The one recorded overlap in this checkout is *internal to contributor A* and is
already disclosed by A: the 4,096-seed collision between the first training
corpus (`0xa51d9000`+16,384 games ⇒ `0xa51dcfff`) and scenario minting
(`0xa51dc000`–`0xa51dffff`). Contributor A destroyed rather than filtered the
contaminated corpus and regenerated from `SEEDLEASE-A52`. See the incident
section of [`lease-map.md`](lease-map.md).

One caution that is **not** an overlap: B's `0x5da7…` lease sits in the `0x5d`
byte family, and `history.md:156,179` records a historical tuning range
`0x5d700000…` **whose span is not stated** (`audit-03-claim-arithmetic.md:527`
marks it `CONSUMED (span not stated)`, and its recommendation D15 is to reserve
whole byte families generously). B's lease note is literally correct that the
eight-hex constant `0x5da7…` appears nowhere in the repository. Whether
`0x5da70000` falls inside an over-reserved `0x5d70…` block cannot be decided
from the retained record. Flagged for the coordinator; not resolvable here.

### The contradiction in `SL-20260820T083000Z-5da70000`

The record states machine-readably that the lease is **open**:

```json
"state": "opened",
"reservedAt": "2026-08-20T08:35:00Z",
"openedAt": "2026-08-20T08:45:00Z",
"runIds": ["RUN-20260820T082542Z-7866d15c", "RUN-20260820T090411Z-73e93859",
           "RUN-20260820T111656Z-f771f173"]
```

The third entry of the same record's own `notes` array states the opposite:

> "Draft pending owner/coordinator authorization. Not opened."

Both statements are in the same file. They cannot both be true. Corroborating
facts, stated without resolving the contradiction:

- `runs/RUN-20260820T082542Z-7866d15c/corpus/corpus.ndjson` and the two later
  corpora exist on disk with mtimes 01:26, 02:06 and 04:40 local, so gameplay
  output derived from *some* seeds exists;
- the validator accepts the record as-is — an `opened` lease is required to
  carry an `openedAt`, and it does;
- `research/README.md` says seed allocation is deliberately closed at bootstrap
  and that "a new experiment uses no gameplay, a documented previously evaluated
  development cohort, or an owner/coordinator-assigned lease created after the
  conservative import", which is what the note appears to be acknowledging.

**This memo does not resolve it.** Only contributor B or the repository owner
can say whether the note is stale text left over from the draft or whether the
`opened` state was set prematurely. The distinction matters: if the seeds were
opened without authorization, the four results built on them are still valid
runs but were produced outside the allocation procedure; if the note is stale,
the record is simply carrying a false statement about itself. Either way the
fix is a successor record, since `research/README.md` makes an opened lease
immutable.

### Leases documented only in prose

Four of contributor A's ranges — the `0xa51d1000` confirmation cohort,
`SEEDLEASE-A52-FLOW`, `SEEDLEASE-A52-FLOW2` and `SEEDLEASE-A52-LEAF` — are
declared in approach READMEs, preregistrations and finding headers but do not
appear in `lease-map.md`, which was last written at 03:21 local, before that
work existed. FLOW, FLOW2 and LEAF are carved from the `SEEDLEASE-A52` reserve
row and are mutually disjoint and consistent with it. The `0xa51d1000`
confirmation cohort is inside the `SEEDLEASE-A51D` umbrella but matches no row
in the table. None of contributor A's ranges has a `research/seeds/leases/`
record at all. This is a documentation gap, not a conflict.

## `make research-validate` — before and after

**Before** (17:20 local, and again after this session's first contribution
record was written): **2 errors.**

```text
ERROR approaches/lifetime-objective/learned-leaf/README.md:38: missing link target ../../../docs/exploratory/finding-08-learned-leaf.md
ERROR docs/exploratory/gpu-03-onednn-conv-nondeterminism.md:5: missing link target finding-08-learned-leaf.md
research validation failed: 2 error(s)
```

| Error | Owner | Cause |
| --- | --- | --- |
| `approaches/lifetime-objective/learned-leaf/README.md:38` | **Contributor A**, work package `learned-leaf` | The README linked forward to `docs/exploratory/finding-08-learned-leaf.md`, which did not yet exist. |
| `docs/exploratory/gpu-03-onednn-conv-nondeterminism.md:5` | **Contributor A**, same work package | Same forward link, from the header line "while building the parity gate for [`finding-08`](finding-08-learned-leaf.md)". `gpu-03` links to it again in its "What to do" section. |

Both failures belonged to contributor A, not to contributor B. **Contributor B's
`research/` records passed validation throughout**: every one of B's theory,
experiment, run, result, dataset and lease records is accepted, including the
`protocolSha256` canonical-hash check on the frozen experiments and the
seed-range overlap check. This session verified that by running the validator,
which collects all categories of error before reporting rather than stopping at
the first.

**After: passing.** Not because anything here repaired it. `finding-08-learned-leaf.md`
was published by its own author at **17:41 local, during this session**, four
minutes after the file map above was compiled, which resolved both broken links
at once.

```text
research validation passed: 36 live record(s)
Ran 6 tests in 0.373s
OK
```

This session deliberately did **not** create `finding-08-learned-leaf.md` to
silence the validator while it was still missing. Inventing a finding document
in order to satisfy a link check would have been worse than the failing check,
and a contribution record must not attribute a result that has not been written.
Once the real document appeared, the owed contribution record for that package
was written from it (`CT-20260821T004637Z-e804cca0`).

**Caveat on the passing state.** The `learned-leaf` package appears to have been
active minutes earlier (`runs/RUN-A52-LEAF/eval` and `stage2.log` at 17:32–17:33
local), and nine files remain modified in the working tree with roughly 100
untracked `README.mdx` files. A pass at this instant is not a pass on a quiesced
tree. Re-run the validator before relying on it.

## Duplicated effort: two afterstate/value-learning tracks

Both contributors independently built a learned evaluator of the public
afterstate, on the same day, in the same tree, for the same reason — the
repository's documented sibling-extrapolation failure mode. They differ in what
they predict and in what their training data covers. Laid out below so a
coordinator can decide; **no judgement of which is better is offered here, and
neither has been compared against the other on any common cohort.**

### Contributor B — `approaches/afterstate-learning/distributional-afterstate/`

**Training target.** `scoreGained`, rescaled by a fixed data-independent
constant of 10,000 (`train.py:28`). `scoreGained` is defined in
`common.hpp:51` as "placement + continuation score over horizon": the score
delta of the placement itself plus the score of up to `kHorizon = 40`
subsequent moves played by a fixed public continuation policy (phase-greedy
depth 1) inside one aligned chance scenario. The primary loss is a **pinball
(quantile) loss** over `N_QUANTILES` heads, so the model learns a
*distribution* over that 40-move score, not a point estimate. Three auxiliary
heads are trained alongside it: a within-root pairwise ranking loss over
siblings, a BCE head on `survived` (`not terminal`), and an MSE head on flow
(`clears / played`, `reveals / played`). Total loss is
`pinball + rank + 0.3·bce + 0.1·flow` (`train.py:201`).

**Model shape.** Action-free. The input planes encode the resolved
**afterstate** — board, next visible disc, moves remaining — and never the
action identity, so column identity cannot be used as a shortcut.

**Sibling coverage: complete, and structurally so.** `common.hpp:153`,
`labelRoot`, iterates `root.legal_actions` in full, crossed with
`scenario_count` scenarios, and the source comments the guarantee: "Successor
closure is structural: the loop covers `root.legal_actions` completely, so a
missing label indicates an engine failure, not a gap." Roots with fewer than two
legal actions are dropped as offering no decision (`makeRoot`). The K=8 result
record reports 11,379 roots and 616,048 sibling labels at **100% action
completeness**. Folds are split by whole origin game with cross-fold dedup, and
the offline gate is measured against exact fair-D4 comparator labels produced by
`label-d4.cpp` reusing the pinned reference byte-identically.

**Recorded outcome.** Three preregistered iterations, all `runValidity: valid`,
all `evidenceTier: pilot`. K=8 and K=64 `inconclusive` on a frozen label-
stability floor (split-half Spearman 0.246 and 0.446 against a 0.5 floor); K=256
passed stability (0.818 decisive) and returned a `fail` — a valid negative: the
model beat its D1 teacher (top-1 0.424 vs 0.319) but trailed fair D4 (0.424 vs
0.499; regret 0.241 vs 0.178). A fourth experiment, a top-two near-tie override
gate over fair D4, also returned `valid` + `fail`, missing its frozen margin in
one of two half-folds while improving eligible-root regret in both.

### Contributor A — `approaches/lifetime-objective/afterstate-net/`

**Training target.** Explicitly *not* score. `train.py`'s own header states the
reasoning: score is ~94% flat row-rise bonus and correlates with lifetime at
r = 0.9995, so predicting score is predicting survival through a 17,000-point
quantiser with a heavy tail. The heads are:

- a **hazard** head, `P(the game survives k more row rises)` for `k = 1..12`;
- a **lifetime** head, `log1p(moves remaining)`;
- two **flow** heads, numbered clears and covered reveals produced by the move.

Labels come from completed games: `movesToDeath` and `risesToDeath` are stored
per move in the corpus record (`dataset.py`), so every move of every game yields
one labelled observation.

**Model shape.** Also action-free, and for the same stated reason. The
deployment plan is to score every legal successor with the same state-only
function inside the existing audited chance-averaging search.

**Sibling coverage: played action only, as actually generated.** The corpus
generator `sibling-corpus/generate.cpp` defines *two* record types: a
`StateRecord` (72 bytes, one per played move, carrying `chosenColumn` and
`legalMask`) and a `PanelRecord` (445 bytes, one per sampled root, carrying the
resolved afterstate and immediate effects of **every** legal column under a
common tape). The panel is gated by a `--panel-stride` option that **defaults to
0, meaning disabled**. Every corpus summary in `runs/RUN-A51D-corpus/` —
`mix-d1` through `mix-d4`, the four files that make up the training data —
records `"panelStride": 0` and `"panelRecords": 0`. Only the 16-game smoke run
produced a `smoke.panel` file. And `afterstate-net/dataset.py` defines and loads
only `STATE_DTYPE`; it contains no panel reader at all.

So the all-sibling capability exists in contributor A's generator and was
deliberately built, but **the corpus this trainer consumes contains one row per
played action, not one row per legal sibling.** Diversity across siblings comes
instead from behaviour mixing: four depths (D1–D4) with ε-random legal
deviations at ε = 0.15, 0.08, 0.05 and 0.03 respectively, so unplayed columns
appear in the data as *played* columns of other games rather than as siblings of
the same root.

**Recorded outcome.** None. There is no result record, no finding document, and
no numbered finding in `docs/exploratory/` for `afterstate-net`. Its state is
`runs/RUN-A51D-net/` plus source. Under
[`methodology.md`](../methodology.md)'s evidence labels this is at most
*task-record only*, and this memo does not assign it an outcome.

### Side by side

| | B: `distributional-afterstate` | A: `afterstate-net` |
| --- | --- | --- |
| Predicts | distribution over 40-move score under a fixed D1 continuation | hazard of surviving `k` more rises; lifetime; per-move clears and reveals |
| Loss | pinball + within-root pairwise rank + survival BCE + flow MSE | not read in detail for this memo |
| Conditions on action identity | no | no |
| Training rows | every legal sibling × K aligned scenarios | one per played move |
| Sibling coverage | 100%, structurally guaranteed, verified in the record | not present in the generated corpus; the generator supports it but `panelStride` was 0 |
| Sibling diversity source | successor closure at each root | ε-random deviation across four behaviour depths |
| Chance handling | K aligned scenarios, common random numbers within a root | corpus is single-trajectory per game |
| Split unit | whole origin game, cross-fold dedup | whole origin (`split_by_origin`) |
| Offline gate | against exact fair-D4 comparator labels, preregistered | none recorded |
| Machine-readable records | theory, 4 experiments, 4 runs, 4 results, 4 datasets, 1 lease | none |
| Status | 3 pilots + 1 override gate, all `valid`, outcomes `inconclusive`/`fail` | no recorded result |

### The genuinely shared question

Both tracks are attacking the same documented failure — `status.md` §4, "a model
learned the outcome of the action that was played, then deployment asked it to
choose among several actions it had not observed equally well" — and both chose
the same structural answer, an action-free evaluator scored over successors.
They diverge on the two axes the benchmark contract calls out separately:
**what the label is** (long-horizon score distribution versus survival hazard)
and **how sibling coverage is obtained** (successor closure versus behaviour
mixing). `docs/benchmarks.md` is unambiguous that "played-action value error is
not a substitute for sibling ranking" and that model selection requires a
whole-origin manifest fixed before labels are inspected. B's line satisfies that
and has a recorded negative. A's line has not been evaluated against it.

A coordinator has an obvious cheap experiment available and this memo does not
run it: contributor A's generator already supports `--panel-stride`, and
contributor B's gate already exists, so A's hazard target could be evaluated on
a successor-closed panel using B's harness. That would separate "was the target
wrong" from "was the coverage wrong", which no experiment in this repository
currently does. It is offered as an observation, not a preregistration.

## Gaps found while reconciling

These are recorded so they are not rediscovered. None is repaired here.

1. **Resolved during this session.** `approaches/lifetime-objective/learned-leaf/`
   and `docs/exploratory/gpu-03-onednn-conv-nondeterminism.md` had no
   contribution record. The record was deliberately withheld while
   `finding-08-learned-leaf.md` was missing, because a transcription must not
   invent the result it attributes. The finding was published by its author at
   17:41 local and the record was then written from it as
   `CT-20260821T004637Z-e804cca0`. It is a transcription: `selfReported` is
   false and the delegated agent has not reviewed it.
2. **`approaches/lifetime-objective/afterstate-net/` has a contribution record
   but no result.** `CT-20260820T100249Z-b1ea4b1a` lists it as an artifact; no
   finding, result record, or explicit no-run status exists for it. Under
   `AGENTS.md`'s definition of done this is unfinished, and the honest closure
   may be a recorded no-run status rather than a run.
3. **Contributor A's four undocumented leases** (above) should be added to
   `lease-map.md` or, better, given real `research/seeds/leases/` records.
4. **`docs/exploratory/README.md`'s contents table is stale.** It lists through
   finding-03 and marks `gpu-01` "in progress"; findings 05, 06, 07 and 08, the
   `gpu-02` and `gpu-03` notes, and this memo and
   [`proposed-status-corrections.md`](proposed-status-corrections.md) are all
   absent from it.
5. **finding-02's contribution record already exists.** This memo's session was
   asked to write one and found `CT-20260820T100255Z-a47bcb43` already covering
   `approaches/lifetime-objective/scenario/` and
   `docs/exploratory/finding-02-scenario-benchmark.md`, transcribed on the
   delegated agent's behalf with `selfReported: false` and the same
   coordinator-transcription limitation. No duplicate was written.
6. **`MACH-20260820T080056Z-376ada90.json` has no owner.** Both tracks cite it.
   No contribution record claims it.
7. **Commit `9f464d8` has no attribution trailers** and mixes both tracks. The
   convention in `docs/agents/contributions-and-commits.md` asks for
   `Contribution-ID` trailers listing each contributing model; `ac7f04e` does
   this and `9f464d8` does not.

## What this memo is not

It does not resolve the lease-state contradiction, repair any validation error,
rank the two afterstate tracks, promote anything into `docs/research/`, or edit
any file belonging to either contributor. Proposed edits to
`docs/research/status.md` and `docs/research/experiment-index.md` are written
out, unapplied, in
[`proposed-status-corrections.md`](proposed-status-corrections.md).
