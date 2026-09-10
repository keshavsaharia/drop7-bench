You are the coordinator of one preregistered Drop7 research experiment in the
repository at /home/keshav/Developer/drop7-bench. Read AGENTS.md first, then
.agents/skills/million-point-research/SKILL.md and every reference it selects,
.agents/skills/drop7-artifact-publishing/SKILL.md, .agents/skills/drop7-writing-style/SKILL.md,
docs/research/status.md, docs/methodology.md and docs/benchmarks.md. Follow them
exactly. You own experiment IDs, seed leases and integration for this
experiment only.

GOAL. Test whether training the row-and-column n-tuple leaf with a DEPTH-4
fair search as the actor and teacher (TreeStrap: every internal node of the
search tree trained toward its own score-only backup), at a gentle step size,
for a long training period, produces a leaf that beats the frozen tables
inside the same depth-3 and depth-4 searches on never-read development games.
The target that matters is the mean score over complete games under the
public information boundary; a single game, and any scripted round such as
gauntlet-01, is a playground demonstration and never tier evidence.

STATE OF THE RECORD YOU MUST NOT CONTRADICT. Frozen tables: runs/RUN-20260905T193006Z-4fbeb4e5/ntuple-scale/main/best-weights.bin
(SHA-256 0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b,
dataset DS-20260906-ntuple-scale-frozen-tables-ff977178); they beat the fair
leaf by 155,000-170,000 at depth 3 on five blocks and score 516,000-532,000 at
depth 4. Wider tables (EX-20260906-ntuple-scale-replication-wide-plateau-f627f07a),
fill buckets (EX-20260906-ntuple-fill-conditioned-continuation-a9e5cbd3) and
full-step TreeStrap with a depth-3 actor (EX-20260907-ntuple-treestrap-warm-start-c33ba880,
-108,970 at depth 3) did not improve the leaf. A gentle-step depth-3 TreeStrap
run (EX-20260907-ntuple-treestrap-gentle-step-087a3a63, RUN-20260907T041853Z-70c90dc9)
was launched on 2026-09-07 at 06:31Z on branch claude/n-tuple-search-targets.

STEP 0, BEFORE ANYTHING ELSE. Do not start while that run is executing:
check `pgrep -fa 'pipeline6.sh chain'` and the run's
runs/RUN-20260907T041853Z-70c90dc9/ntuple-scale/pipeline.log; wait until
'analyze: written' appears (expected about 12:30Z on 2026-09-07). Then read
its analysis.md and, if present, its result record. Record in your theory how
its readings change the prior: if its TreeStrap candidate beat the frozen
tables at depth 3 (bootstrap lower bound above zero), your warm start is that
candidate's file and its step size is your default; if it did not, your warm
start is the frozen tables and you state that search targets at depth 3 have
not helped at three step sizes.

BRANCH AND WORKTREE. `git fetch`, then branch from the tip of
claude/n-tuple-search-targets (or from dev if PRs #31 and #32 have merged) as
claude/n-tuple-deep-teacher. Inspect `git status` before editing; other
sessions share this checkout. Never edit a stage driver while a stage is
executing it; scripts/pipeline5.sh and pipeline6.sh are frozen drivers of
earlier experiments, copy them to pipeline7.sh.

WHAT ALREADY EXISTS (use, do not reinvent). Crate
approaches/ntuple-rl/ntuple-scale (build with build.sh, test with
`cargo test --release`, PATH needs $HOME/.cargo/bin). src/search_train.rs is
a training-time fair expectimax proven bit-identical to the engine's search
at any depth by the gate `train-search-vs-engine`; the trainer
(src/bin/train.rs) takes `--actor search --targets tree --search-depth N
--alpha A --init-from FILE --validate-at-start`. The screen binary takes
`--arm NAME=FILE` (depth 3 plus one-ply), `--arm-d3`, `--arm-d4`,
`--games N --games-d4 M`, `--skip-d4`. scripts/analyze.py understands the
arm names treestrap/searchtd/treestrapalt/control/prior and has
`--select-gentle-arm`; scripts/write-tree-result-record.py drafts the
result; scripts/promote-artifacts.py promotes compact artifacts;
scripts/with-rusage.py wraps every stage. The one-ply continuation from the
same warm start is runs/RUN-20260906T201104Z-a96ea6c8/ntuple-scale/pilot/control/best-weights.bin
(SHA-256 92dd1cb2d2a74b026270606c18c5f0d6e4f4ccc74e64c3c4e0f0d2043cdddd90).

CODE CHANGES ALLOWED. (a) Run the gate binary with the depth-4 training search
against Searcher::column_values at depth 4 on at least 40 sampled states
(bit-identical values, identical decisions); add that gate if it does not
exist. (b) Add a trainer option to subsample internal-node targets per
decision if the depth-4 tree's ~120,000 internal nodes make updates dominate
(record the fraction; the gate must show the sampled set is deterministic).
(c) Optionally a `--checkpoint-every` that works with the search actor so a
multi-hour arm can resume. Nothing else in the crate; the engine crate is
never modified.

THE EXPERIMENT (preregister before any lease opens). One theory record (claim,
mechanism, falsifiers, dependencies on TH-20260907-ntuple-search-target-treestrap-leaf-b1c9989f)
and one experiment record, frozen with researchctl.py freeze. Arms, each a
warm start of the frozen tables (or the gentle-step candidate, per STEP 0)
with fresh coherence accumulators:
  deep05  : --actor search --search-depth 4 --targets tree --alpha 0.05
  deep20  : the same at --alpha 0.2
  shallow : --actor search --search-depth 3 --targets tree at the better of
            the two step sizes (the same-compute shallow control)
Each arm: --validate-at-start; validate every 50,000 visited moves on a
256-game training-role block (ntuple-d3s7 and ntuple-1ply against fair-d3s7);
plateau rule window 3 from the sixth training point; wall cap 21,600 s per
depth-4 arm and 7,200 s for the shallow arm; move cap 10,000,000; STOP file
honoured. The candidate is the depth-4 arm whose best validation margin is
larger, ties to deep05; the other is screened at depth 3 as treestrapalt; the
shallow arm is screened at depth 3 as searchtd (rename in the driver so
analyze.py's readings apply, and document the mapping). Measure the depth-4
actor's throughput on the already-open probe block 0xa5277000 in a smoke
run before freezing, and set the validation cadence from it so an arm gets
at least eight validation points.

SEEDS. Do the conservative import exactly as the lease notes of
SL-20260907T041853Z-3b4f397d describe (enumerate every 8-hex constant under
docs, approaches, src, research, artifacts, web/content, scripts) and reserve
three fresh leases disjoint from every listed n-tuple block: a training block
of 2,031,616 seeds above 0xa5df0000 (the next free 0xa5 prefix, e.g.
0xa5e00000-0xa5ff0000; if it is not free, take 0xa6000000 upward and say so),
a 256-game validation block and a 2,048-game screen block from the allocator
pool after 0xa52f3680 (0xa52f3680-0xa52f3780 and 0xa52f3780-0xa52f3f80 if
free). Open the training and validation leases at launch; the screen lease is
opened exactly once by the screen stage through scripts/open-screen-lease.py,
after every table file's SHA-256 is on disk and the gates have passed on each
new file. Never open a protected (0x7d) or final (0xd7) seed.

SCREEN. One fresh block, opened once. Depth-3 and one-ply arms on all 2,048
games: prior (frozen tables), treestrap (the candidate), treestrapalt,
searchtd (the shallow control), control (the one-ply continuation),
fair-d3s7. Depth-4 arms on the first 512 games (--games-d4 512): prior-d4s7
and treestrap-d4s7. Gate: treestrap-d3s7 minus prior-d3s7 on 2,048 games,
bootstrap and Student-t 95% lower bounds > 0, both halves positive, no
lower-quartile regression, illegal and incomplete decisions zero. Fixed
three-way verdicts beside it (supported / refuted / inconclusive):
treestrap vs searchtd (depth as teacher), treestrap vs treestrapalt (step
size), treestrap vs control (one-ply reference), treestrap-d4s7 vs prior-d4s7
(depth 4, 512 games). Training-signal check at pilot tier: the candidate's
best validation margin exceeds its own point-0 margin.

BUDGET AND STOPS. Total wall 57,600 s; 32 threads; 48 GiB resident; stop the
whole run on any gate failure, illegal or incomplete decis
breach and record it as interrupted or invalid, never as a candidate
failure. Never re-run a screen on the same or another block without a new
experiment record. Never change a gate after reading data it controls.

RECORDS AND DELIVERABLES. Machine profile (researchctl.py
(lifecycle running at launch, completed at the end, with p
and peak RSS from rusage.jsonl and every artifact reference), result record
(valid/partial/invalid; pass/fail/inconclusive; never upgr
contribution record (exact platform and model identifiers as your runtime
exposes them, else `unknown`; level L3; self-reported), le
notes, the theory's assessment. Publish every non-table artifact with
`npm run artifact:publish -- --run-id <run> --dir runs/<run>/ntuple-scale
--key ntuple-scale --exclude '*.bin' --exclude '*.zst' --manifest
runs/<run>/published.jsonl --public` (dry-run first and re
the candidate table file compressed with zstd; cite the re
run and result records; promote compact artifacts with
scripts/promote-artifacts.py. Refresh the web snapshot with
`cd web && node --experimental-strip-types scripts/extract,
add a section to approaches/ntuple-rl/ntuple-scale/README.mdx under
"What happened" in the site voice, update docs/research/st
docs/research/experiment-index.md and docs/exploratory/lea
append the day's research log at web/content/log/YYYY-MM-DD.mdx (negative
results as prominently as positive ones; no number that is
record or run artifact). Run `make research-validate` (23
errors from other contributors are the baseline; add none), `cargo test
--release`, `npm test`, and under web/ `npx tsc --noEmit`,
`node scripts/check-mdx.mjs <files>`, `node scripts/check-figures.mjs`,
`node scripts/check-tokens.mjs`. Commit with the repository template and
trailers, linted by researchctl.py commit-lint: one prereg
before the leases open, one result commit after. Push the branch and open a
pull request against dev (or against claude/n-tuple-search
is still open) describing the outcome in the same plain re

WHAT TO WRITE IF IT FAILS. A valid negative rejects only t
State which leg failed (the depth-4 teacher, the step size, or the whole
search-target idea), what the shallow control says, and what the record now
supports next. Do not open any further cohort for the cand

OPTIONAL PLAYGROUND DEMONSTRATION, ONLY AFTER THE EXPERIME
the owner asks for a leaderboard entry, play the frozen ta
winning candidate) at depth 5 through the benchmark playground per
.agents/skills/drop7-benchmark-playground/SKILL.md and, fo
ledger, .agents/skills/drop7-competition-publishing/SKILL.
two hours for gauntlet-01 at depth 5; label it a scripted-
everywhere it appears.
