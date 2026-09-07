You are the coordinator of one preregistered Drop7 research experiment in the
repository at /home/keshav/Developer/drop7-bench. Read AGENTS.md first, then
.agents/skills/million-point-research/SKILL.md and every reference it selects,
.agents/skills/drop7-artifact-publishing/SKILL.md, .agents/skills/drop7-writing-style/SKILL.md,
docs/research/status.md, docs/methodology.md and docs/benchmarks.md. Follow them
exactly. You own experiment IDs, seed leases and integration for this
experiment only. This supersedes prompts/treestrap-d4.md: same goal, same
tooling, but built to run for days on this workstation without an automatic
short-window rule that can end a session on a local dip. The owner has
explicitly authorized multi-day wall time on the AMD Ryzen AI MAX+ 395 box
for this experiment.

GOAL. Test whether training the row-and-column n-tuple leaf with a DEPTH-4
fair search as the actor and teacher (TreeStrap: every internal node of the
search tree trained toward its own score-only backup) produces a leaf that
beats the frozen tables inside the same depth-3 and depth-4 searches on
never-read development games, given training time measured in days rather
than hours. The target that matters is the mean score over complete games
under the public information boundary; a single game, and any scripted round
such as gauntlet-01, is a playground demonstration and never tier evidence.

STATE OF THE RECORD YOU MUST NOT CONTRADICT. Frozen tables:
runs/RUN-20260905T193006Z-4fbeb4e5/ntuple-scale/main/best-weights.bin
(SHA-256 0ade9d4e4080ebdd52a1474b1a13410dc8dfb77f5eba24b078aa7703c92ace0b,
dataset DS-20260906-ntuple-scale-frozen-tables-ff977178); they beat the fair
leaf by 155,000-170,000 at depth 3 on six blocks and score 516,000-532,000 at
depth 4. Wider tables, fill buckets, full-step depth-3 TreeStrap
(EX-20260907-ntuple-treestrap-warm-start-c33ba880, -108,970 at depth 3, a
measurable loss) did not improve the leaf. A gentle-step depth-3 TreeStrap
run (EX-20260907-ntuple-treestrap-gentle-step-087a3a63, run
RUN-20260907T041853Z-70c90dc9) trained three arms (alpha 0.05, alpha 0.2,
and a visited-states-only ablation at alpha 0.05) for roughly one hour each
and stopped each one by an automatic plateau rule (window of three
validation points). Every arm's validation curve was noisy across that short
window: the alpha-0.2 arm's margin swung by 40,000-50,000 points between
consecutive validation reads, well inside the range a genuine improving
trend could hide in. That noise, not a considered judgment that training had
stopped helping, is what the automatic rule was reacting to, and it is the
reason this experiment does not use that rule.

STEP 0, BEFORE ANYTHING ELSE. Check whether RUN-20260907T041853Z-70c90dc9's
screen has finished: `pgrep -fa 'pipeline6.sh chain'` and
runs/RUN-20260907T041853Z-70c90dc9/ntuple-scale/pipeline.log for
'analyze: written'. Read its analysis.md and result record once it exists.
State in your theory record how its reading changes the prior: if its
TreeStrap candidate (arm treestrap20, alpha 0.2) beat the frozen tables at
depth 3 with the bootstrap lower bound above zero, warm-start this
experiment from that candidate's frozen file instead of the raw frozen
tables, and say so with its SHA-256. If it did not pass, warm-start from the
frozen tables as below and state plainly that depth-3 search-target training
has not produced a passing candidate at three step sizes; that does not
block this depth-4 experiment, because depth-4 backups are a materially
different training signal (three plies of exact chance-averaged play beyond
each imagined move, not two), but you must not claim the depth-3 runs
support a depth-4 hypothesis they did not test.

BRANCH AND WORKTREE. `git fetch`, then branch from the tip of
claude/n-tuple-search-targets (or from dev once PRs #31 and #32 have merged)
as claude/n-tuple-deep-teacher-continuation. Inspect `git status` before
editing; this checkout is shared with other sessions, and a multi-day
background job makes that more likely, not less: before launching, run
`ps -eo pid,etime,pcpu,cmd | grep -E 'target/release/(train|screen|gate)'`
and confirm nothing else is using the 32 threads this job needs. Re-check
before every later action that touches the shared checkout (new commits,
branch switches); do not disturb a running training arm's files. Never edit
a stage driver while a stage is executing it.

WHAT ALREADY EXISTS (use, do not reinvent). Crate
approaches/ntuple-rl/ntuple-scale (build with build.sh, test with
`cargo test --release`, PATH needs $HOME/.cargo/bin). src/search_train.rs is
a training-time fair expectimax proven bit-identical to the engine's search
at any depth by the gate `train-search-vs-engine`; the trainer
(src/bin/train.rs) takes `--actor search --targets tree --search-depth N
--alpha A --init-from FILE --validate-at-start`. The screen binary takes
`--arm NAME=FILE` (depth 3 plus one-ply), `--arm-d3`, `--arm-d4`,
`--games N --games-d4 M`, `--skip-d4`. scripts/analyze.py understands the
arm names treestrap/searchtd/treestrapalt/control/prior; scripts/write-tree-result-record.py
drafts the result; scripts/promote-artifacts.py promotes compact artifacts;
scripts/with-rusage.py wraps every stage.

THE CONTINUATION MECHANISM (this is what changes from the prior prompt).
The trainer's checkpoint/resume machinery already works with the search
actor; it was written for the one-ply actor and the code path is shared.
Use it instead of a fixed single invocation:
  - Pass `--checkpoint-every 1` and do NOT pass `--no-checkpoint`.
    checkpoint.bin (weights plus both temporal-coherence accumulator arrays,
    about 12 GB for this layout) is then written after every validation
    point and at the end. This is what makes the run genuinely resumable:
    restarting from checkpoint.bin continues training exactly where it left
    off, not from a fresh-accumulator shock the way restarting from
    best-weights.bin would.
  - Pass `--plateau-window 0`. This disables the automatic stop entirely; the
    trainer then only stops on its move budget, its wall-time budget, or a
    STOP file placed in the arm's output directory. Confirm in train.rs that
    plateau_window 0 short-circuits plateau_check before relying on it.
  - Set a large `--moves` ceiling (e.g. 2e8) and a `--wall-seconds` sized to
    one sitting (e.g. 21,600-43,200 for a single day's session); when that
    budget is spent the process exits cleanly with checkpoint.bin intact.
  - To continue in a later session, run the identical command plus
    `--resume`. Do not change --layout, --alpha, --seeds-start, or
    --seeds-count between sessions; the trainer rejects a changed
    config.json. Log each session as its own contribution record under the
    same experiment and run IDs, citing the moves trained in that session.
  - Raise the validation cohort from the prior runs' 256 games to 512, to
    roughly halve the standard error of each validation point and make a
    real trend easier to tell from noise.
  - You, the coordinator, decide when an arm is done by reviewing the FULL
    validation curve (every point in progress.jsonl, not a fixed trailing
    window) at explicit review points: after each day of wall time, or every
    20 validation points, whichever comes first. Compare the mean of the
    last 15 points against the mean of the 15 before them, with the
    bootstrap lower bound of that paired difference, not a raw point
    comparison. Stop an arm only when that comparison is not positive AND
    you have at least 40 validation points AND at least 12 hours of wall
    time on that arm; write the reasoning into the run record's stopReason
    the same way an automatic rule would, including the numbers compared.
    Do not extend an arm past a genuine plateau just because checkpointing
    makes it cheap to keep going; do not stop one early because a short
    window looked flat when the longer comparison has not been read yet.
  - Poll for progress on a schedule matched to how fast the curve actually
    moves (validation every N visited moves; at the depth-4 actor's expected
    throughput, tens of minutes between points), not by busy-waiting; if you
    are an autonomous agent scheduling your own wake-ups, size the interval
    to that, and use a long fallback if you are unsure.

CODE CHANGES ALLOWED. (a) Run the gate binary with the depth-4 training
search against Searcher::column_values at depth 4 on at least 40 sampled
states (bit-identical values, identical decisions); add that gate if it does
not exist. (b) If the depth-4 tree's roughly 120,000 internal nodes per
decision make per-move training time dominated by table updates rather than
search, add a trainer option to subsample internal-node targets per
decision (record the fraction; the gate must show the sampled set is
deterministic given the state and a fixed sampling seed). (c) The checkpoint
and plateau-window-0 mechanics above already exist; confirm them with a
short smoke run before committing to a multi-day arm, rather than assuming.
Nothing else in the crate; the engine crate is never modified.

THE EXPERIMENT (preregister before any lease opens). One theory record
(claim, mechanism, falsifiers, dependency on
TH-20260907-ntuple-search-target-treestrap-leaf-b1c9989f) and one experiment
record, frozen with researchctl.py freeze. Two arms, both --actor search
--targets tree --search-depth 4 --validate-at-start --checkpoint-every 1
--plateau-window 0, warm-started per STEP 0:
  deep05 : alpha 0.05
  deep20 : alpha 0.2
Both run concurrently only if the machine has the memory and thread headroom
(confirm with the doctor profile and a smoke run's peak RSS before deciding;
otherwise run deep20 first, since the gentle-step depth-3 result favored the
larger step, and deep05 second). Measure the depth-4 actor's throughput on
the already-open probe block 0xa5277000 in a smoke run before freezing the
protocol, and use it to set --validate-every so each arm gets at least one
validation point per hour of wall time. Preregister the total wall budget as
days, not hours (state the exact number you and the owner agree to before
freezing; do not choose it unilaterally without flagging it), and preregister
the stop-decision procedure above verbatim so it is fixed before any
training data is read.

SEEDS. Do the conservative import exactly as the lease notes of
SL-20260907T041853Z-3b4f397d describe (enumerate every 8-hex constant under
docs, approaches, src, research, artifacts, web/content, scripts) and reserve
three fresh leases disjoint from every listed n-tuple block: a training block
of 2,031,616 seeds above the highest 0xa5 prefix already listed (state the
exact free range you find; do not assume 0xa5e00000 is still free without
re-checking), a 512-game validation block, and a 2,048-game screen block from
the allocator pool. A training-role block that a multi-day run reads in order
may wrap; record the wrap count. Open the training and validation leases at
launch; the screen lease is opened exactly once, by the screen stage, after
every table file's SHA-256 is on disk and the gates have passed on it. Never
open a protected (0x7d) or final (0xd7) seed.

SCREEN. One fresh 2,048-game block at depth 3, with the depth-4 arms on its
first 512 seeds (screen --games-d4 512), opened once after both arms have
been stopped by the reviewed procedure above (not by wall-time exhaustion
mid-curve unless the owner explicitly ends the experiment there). Arms:
prior (frozen tables), the winning depth-4 candidate as treestrap (at both
depth 3 and depth 4), the other depth-4 arm as treestrapalt (depth 3 only),
the depth-3 gentle-step candidate from RUN-20260907T041853Z-70c90dc9 as
control if its file still exists on the workstation, fair-d3s7. Gate:
treestrap-d3s7 minus prior-d3s7, bootstrap and Student-t 95% lower bounds
> 0, both halves positive, no lower-quartile regression, zero illegal or
incomplete decisions. Persistence reading beside the gate, not part of it:
treestrap-d4s7 minus prior-d4s7 on the 512-seed subset, same four criteria,
three-way verdict.

BUDGET AND STOPS. The preregistered multi-day wall budget from THE
EXPERIMENT above is the hard cap; state it explicitly in the experiment
record's resources.wallSeconds. 32 threads; size the memory bound from the
smoke run's peak RSS with headroom, not a guess. Stop the whole run on any
gate failure, illegal or incomplete decision, or memory breach, and record
it as interrupted or invalid, never as a candidate failure. A STOP file in
an arm's directory ends that arm at its next chunk boundary; use it if the
owner needs the machine back before the reviewed stop criterion fires. Never
re-run a screen on the same or another block without a new experiment
record. Never change a gate, or the stop-decision procedure, after reading
data it controls.

RECORDS AND DELIVERABLES. Machine profile (researchctl.py doctor). Run
record: lifecycle running at launch, one resourceObserved update per session
(cumulative wall, CPU, peak RSS from rusage.jsonl, and every session's
command line), completed at the end with the full history. Result record
(valid/partial/invalid; pass/fail/inconclusive; never upgrade the tier).
Contribution record per session (exact platform and model identifiers as
your runtime exposes them, else `unknown`; level L3; self-reported), lease
completion notes, the theory's assessment. Publish every non-table artifact
with `npm run artifact:publish -- --run-id <run> --dir runs/<run>/ntuple-scale
--key ntuple-scale --exclude '*.bin' --exclude '*.zst' --manifest
runs/<run>/published.jsonl --public` (dry-run first and read the list) and
the winning candidate's table file compressed with zstd; do not publish
every intermediate checkpoint, only the final frozen candidate. Cite the
references in the run and result records; promote compact artifacts with
scripts/promote-artifacts.py. Refresh the web snapshot with
`cd web && node --experimental-strip-types scripts/extract-ntuple-scale.ts --run <run>`,
add a section to approaches/ntuple-rl/ntuple-scale/README.mdx under "What
happened" in the site voice, update docs/research/status.md,
docs/research/experiment-index.md and docs/exploratory/lease-map.md, and
append (or extend, if the same UTC day) the research log at
web/content/log/YYYY-MM-DD.mdx (negative results as prominently as positive
ones; no number that is not in a result record or run artifact; if the run
spans multiple days, the log entry belongs on the day the result was
recorded, and earlier days should get a short interim note if substantive
progress happened, not silence). Run `make research-validate` (23
pre-existing errors from other contributors are the baseline; add none),
`cargo test --release`, `npm test`, and under web/ `npx tsc --noEmit`,
`npm run lint`, `node scripts/check-mdx.mjs <files>`,
`node scripts/check-figures.mjs`, `node scripts/check-tokens.mjs`. Commit
with the repository template and trailers, linted by researchctl.py
commit-lint: one preregistration commit before the leases open, one result
commit after; a mid-run status commit is fine if a session boundary is a
natural point to record progress, but never invent a number ahead of an
artifact that produced it. Push the branch and open a pull request against
dev (or against claude/n-tuple-search-targets if #32 is still open)
describing the outcome, including the training duration actually used, in
the same plain register.

WHAT TO WRITE IF IT FAILS. A valid negative rejects only this configuration.
State the reviewed stop-decision numbers for both arms (the paired
15-vs-15-point comparison and its bound), how many validation points and
how much wall time each arm actually got, and what the persistence reading
at depth 4 says. If depth-4 search-target training also does not pass after
a multi-day budget with a noise-aware stop rule, say plainly that the
training-target axis of this leaf family has now been tested at three step
sizes, two depths, and both a short and a long duration without a measurable
gain, and that the next lever is a different one, not a fourth variant of
this recipe.

OPTIONAL PLAYGROUND DEMONSTRATION, ONLY AFTER THE EXPERIMENT IS RECORDED. If
the owner asks for a leaderboard entry, play the frozen tables (or the
winning candidate) at depth 5 through the benchmark playground per
.agents/skills/drop7-benchmark-playground/SKILL.md and, for the competition
ledger, .agents/skills/drop7-competition-publishing/SKILL.md; expect about
two hours for gauntlet-01 at depth 5; label it a scripted-round
demonstration everywhere it appears.
