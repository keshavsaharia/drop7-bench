// ./do-research — an autonomous research loop for the Drop7 program.
//
//   plan      a planner model reads the repository's own status, roadmap,
//             records and recent log, and proposes ONE preregisterable
//             experiment (or abstains)
//   critique  a second model family (opencode, Kimi by default) attacks the
//             proposal; the planner revises it once
//   register  theory + experiment records are created through researchctl,
//             seed leases are granted by the allocator in repo.ts
//   implement `claude -p` runs the work package headlessly inside hard caps
//             (turns, dollars, wall clock) and returns a JSON report
//   review    opencode audits the retained records and artifacts
//   assess    the planner reads report + review and decides continue/stop
//
// Everything the loop writes lives in runs/orchestrator/<session>/ plus the
// research records the implementer creates.  Nothing is committed unless
// --commit is passed; nothing is pushed unless --push is passed as well.
// The loop can never open protected or final seeds: the allocator refuses
// those prefixes and the implementer prompt forbids any seed outside the
// granted leases.

import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import path from "node:path";
import { execFileSync, spawnSync } from "node:child_process";
import { randomBytes } from "node:crypto";

import { makePlanner, type Planner } from "./planner.ts";
import {
  REPO_ROOT, allocateLease, createExperiment, createTheory, createContribution, gatherContext, gitShort, isoNow,
  listFamilies, readJson, readText, validateRecords, writeJson, type HistoryEntry, type LeasePool,
} from "./repo.ts";
import { runClaudeImplementer, runOpencodeReviewer } from "./agents.ts";

// ---------------------------------------------------------------------------
// Options
// ---------------------------------------------------------------------------

interface Options {
  maxIterations: number;
  wallHoursPerIteration: number;
  cpuThreads: number;
  maxUsdPerIteration: number;
  plannerBudgetUsd: number;
  implementerMaxTurns: number;
  implementerModel?: string;
  reviewerModel: string;
  skipCritique: boolean;
  skipReview: boolean;
  dryRun: boolean;
  commit: boolean;
  push: boolean;
  focus?: string;
  resume?: string;
  leasePool: LeasePool;
}

function parseArgs(argv: string[]): Options {
  const o: Options = {
    maxIterations: 3,
    wallHoursPerIteration: 10,
    cpuThreads: 30,
    maxUsdPerIteration: 40,
    plannerBudgetUsd: 3,
    implementerMaxTurns: 400,
    reviewerModel: process.env.DO_RESEARCH_REVIEWER_MODEL ?? "baseten/moonshotai/Kimi-K3",
    skipCritique: false,
    skipReview: false,
    dryRun: false,
    commit: false,
    push: false,
    leasePool: { startHex: process.env.DO_RESEARCH_LEASE_POOL_START ?? "0xa52c0000", endExclusiveHex: process.env.DO_RESEARCH_LEASE_POOL_END ?? "0xa5300000" },
  };
  for (let i = 0; i < argv.length; i += 1) {
    const key = argv[i];
    const value = () => { i += 1; if (i >= argv.length) throw new Error(`missing value for ${key}`); return argv[i]; };
    switch (key) {
      case "--max-iterations": o.maxIterations = Number(value()); break;
      case "--wall-hours": o.wallHoursPerIteration = Number(value()); break;
      case "--cpu-threads": o.cpuThreads = Number(value()); break;
      case "--max-usd": o.maxUsdPerIteration = Number(value()); break;
      case "--planner-usd": o.plannerBudgetUsd = Number(value()); break;
      case "--max-turns": o.implementerMaxTurns = Number(value()); break;
      case "--implementer-model": o.implementerModel = value(); break;
      case "--reviewer-model": o.reviewerModel = value(); break;
      case "--focus": o.focus = value(); break;
      case "--resume": o.resume = value(); break;
      case "--skip-critique": o.skipCritique = true; break;
      case "--skip-review": o.skipReview = true; break;
      case "--dry-run": o.dryRun = true; break;
      case "--commit": o.commit = true; break;
      case "--push": o.push = true; break;
      case "-h": case "--help":
        console.log(readText("scripts/do-research/README.md"));
        process.exit(0);
      default:
        throw new Error(`unknown option ${key} (see scripts/do-research/README.md)`);
    }
  }
  if (o.push && !o.commit) throw new Error("--push requires --commit");
  return o;
}

// ---------------------------------------------------------------------------
// Planner contract
// ---------------------------------------------------------------------------

const PROPOSAL_SCHEMA = {
  type: "object",
  properties: {
    abstain: { type: "boolean" },
    abstainReason: { type: "string" },
    slug: { type: "string", pattern: "^[a-z0-9][a-z0-9-]{2,48}$" },
    title: { type: "string" },
    family: { type: "string" },
    claim: { type: "string" },
    mechanism: { type: "string" },
    falsificationCriteria: { type: "array", items: { type: "string" }, minItems: 2 },
    experiment: {
      type: "object",
      properties: {
        title: { type: "string" },
        hypothesis: { type: "string" },
        classification: { type: "string", enum: ["engineering", "diagnostic", "algorithmic", "validation", "infrastructure"] },
        candidateName: { type: "string" },
        benchmarkTier: { type: "string", enum: ["CHECK", "PILOT", "SCREEN", "STANDARD"] },
        primaryMetric: { type: "string" },
        secondaryMetrics: { type: "array", items: { type: "string" } },
        gatePassCriteria: { type: "array", items: { type: "string" }, minItems: 1 },
        failureAction: { type: "string" },
        passAction: { type: "string" },
        wallHours: { type: "number" },
        cpuThreads: { type: "integer" },
        usesGpu: { type: "boolean" },
        trainingSeeds: { type: "integer", minimum: 0 },
        heldOutSeeds: { type: "integer", minimum: 0 },
        stopConditions: { type: "array", items: { type: "string" } },
      },
      required: ["title", "hypothesis", "classification", "candidateName", "benchmarkTier", "primaryMetric", "secondaryMetrics", "gatePassCriteria", "failureAction", "passAction", "wallHours", "cpuThreads", "usesGpu", "trainingSeeds", "heldOutSeeds", "stopConditions"],
    },
    workPackage: { type: "string" },
    expectedEffectPoints: { type: "number" },
    expectedInformationGain: { type: "string" },
    rationale: { type: "string" },
  },
  required: ["abstain", "abstainReason", "slug", "title", "family", "claim", "mechanism", "falsificationCriteria", "experiment", "workPackage", "expectedEffectPoints", "expectedInformationGain", "rationale"],
};

const ASSESSMENT_SCHEMA = {
  type: "object",
  properties: {
    decision: { type: "string", enum: ["continue", "followup", "stop"] },
    outcomeLabel: { type: "string" },
    notes: { type: "string" },
    followupDirection: { type: "string" },
  },
  required: ["decision", "outcomeLabel", "notes", "followupDirection"],
};

function plannerSystem(o: Options): string {
  return [
    "You are the research coordinator for a Drop7 puzzle-game AI program. Read docs/agents/project-nature.md in the brief: this is a single-player puzzle simulator; nothing touches networks or other systems.",
    "Your job: propose exactly ONE falsifiable experiment that maximises expected information gain per CPU-hour given the repository's current evidence, or abstain when nothing affordable is worth running.",
    "Non-negotiable rules you must design within: the deployable policy sees only the visible board, visible next disc, moves until the next rise and terminal state; corrected 17,000-point Hardcore scoring; whole games are the statistical unit; a 64-game paired cohort cannot resolve effects below roughly 50,000 points, so either predict a larger effect or design a lower-variance estimator; never propose opening protected or final seeds; never tune on a cohort whose gate was already read; fresh seeds come only from the orchestrator's allocator; the comparator is unchanged fair D4 unless you state why a different comparator is the right ablation.",
    `Budget ceiling for one iteration: ${o.wallHoursPerIteration} wall-hours on ${o.cpuThreads} CPU threads (a depth-4 five-stratum game costs about 0.3 CPU-seconds per decision on this machine; seven strata about 1.0), one integrated GPU if usesGpu. Propose something that finishes inside that ceiling including CHECK gates and a pilot.`,
    "The work package you write is executed by a headless coding agent with no access to you. It must be explicit: what to implement and where (approaches/<family>/<slug>/), which existing code to reuse (name files), which gates to run before any seed is read, the exact cohort sizes and seed roles, the gate, what to record, and what to do on failure. Reuse existing engines and harnesses; do not ask for a rewrite of the simulator.",
    `Existing approach families: ${listFamilies().join(", ")}. Use one of them or justify a new one in the rationale.`,
    "Reply with JSON matching the schema and nothing else.",
  ].join("\n");
}

async function propose(planner: Planner, brief: string, o: Options, critique?: string, previous?: unknown): Promise<any> {
  const user = [
    brief,
    critique ? `\n\n## Adversarial critique of your previous proposal (revise it, or abstain if it is fatally flawed)\n${critique}\n\n## Your previous proposal\n${JSON.stringify(previous, null, 2)}` : "",
    "\n\nPropose now.",
  ].join("");
  const reply = await planner.complete({ system: plannerSystem(o), user, schema: PROPOSAL_SCHEMA });
  if (!reply.json || typeof reply.json !== "object") throw new Error("planner returned no JSON proposal");
  return reply.json;
}

function critiquePrompt(proposal: unknown, sessionRel: string, iteration: number): string {
  return [
    "You are an adversarial reviewer for a Drop7 puzzle-game AI research proposal in this repository (read docs/agents/project-nature.md first; it is a glossary).",
    `READ-ONLY: do not create, modify or delete any file except runs/${sessionRel}/iteration-${String(iteration).padStart(2, "0")}/critique.md, which you must write. Do not run builds, gameplay or long commands.`,
    "Read docs/research/status.md, docs/methodology.md, docs/benchmarks.md and the files the proposal names. Then try to break the proposal: information-boundary leaks, seed-role violations, effects below the detection floor, confounds with prior negative results, runtime that cannot fit the budget, vague gates. Finish with the three most damaging objections and one concrete improvement each. Under 1,200 words, specific, no flattery.",
    "\n## Proposal\n" + JSON.stringify(proposal, null, 2),
  ].join("\n");
}

function implementerPrompt(args: { proposal: any; theoryId: string; experimentId: string; theoryPath: string; experimentPath: string; leases: Array<{ id: string; range: string; role: string }>; sessionRel: string; iteration: number; o: Options }): string {
  const { proposal, o } = args;
  const approachDir = `approaches/${proposal.family}/${proposal.slug}`;
  return [
    "You are executing ONE preregistered research work package in this repository. Read AGENTS.md and .agents/skills/million-point-research/SKILL.md completely before acting, then docs/research/status.md and docs/benchmarks.md.",
    `Theory: ${args.theoryId} (${args.theoryPath}). Experiment: ${args.experimentId} (${args.experimentPath}). Both are drafts you must complete: fill every placeholder field of the experiment record (data, metrics, gate, resources, stopConditions, expectedArtifacts) from the work package, set gate.fixedBeforeControlledData to true, and freeze it with 'python3 .agents/skills/million-point-research/scripts/researchctl.py freeze <path>' BEFORE reading any leased seed.`,
    `Seed leases granted to this experiment (the ONLY seeds you may read, besides CHECK probes on already-opened development ranges listed in docs/exploratory/lease-map.md): ${args.leases.map((l) => `${l.id} ${l.range} role ${l.role}`).join("; ") || "none (no gameplay data)"}. Update each lease record's state/openedAt/runIds when you open it. Never read any seed with prefix 0x7d or 0xd7. Never read 0xa51d1000-0xa51d103f or any other already-evaluated cohort for tuning.`,
    `Implement under ${approachDir}/ (create it; add a README.mdx following docs/agents/approach-page-template.md). Reuse existing engines and harnesses named in the work package. Builds go to build/${proposal.slug}/, run output to runs/<run-id>/ where run IDs are minted as RUN-<UTC timestamp>Z-<8 random hex>.`,
    `Budget (hard): ${o.wallHoursPerIteration} wall-hours total, ${o.cpuThreads} CPU threads, GPU ${proposal.experiment.usesGpu ? "allowed" : "not used"}. Run the cheapest checks first: build, seed-free CHECK gates (parity with the frozen code you build on, determinism, reflection, metadata blindness, legality, completed depth), then a PILOT on already-opened development seeds for runtime projection, then the preregistered run. Stop at the budget; a partial run is recorded as partial, never extrapolated.`,
    "Records you must write (validate with 'make research-validate' until it passes): one run record per execution (research/runs/), one result record (research/results/) with the gate evaluated exactly once and narrow claim language, and one contribution record for yourself (research/contributions/) with the exact platform 'Claude Code' and the model identifier the runtime exposes (else 'unknown'). Append to the research log web/content/log/<today>.mdx (one file per local calendar day; quote titles/summaries; only numbers that exist in a record or artifact; negative results as prominently as positive). Do not commit. Do not edit frozen protocols, promoted results, baselines-v1.json, or any source under approaches/fair-expectimax/reference/.",
    "If the work is blocked (missing dependency, gate failure, budget), stop cleanly, record what was done as partial/invalid with the reason, and report status 'blocked' or 'partial'.",
    "\n## Work package\n" + proposal.workPackage,
    "\n## Experiment design as preregistered by the planner\n" + JSON.stringify(proposal.experiment, null, 2),
    "\n## Claim, mechanism, falsification\n" + JSON.stringify({ claim: proposal.claim, mechanism: proposal.mechanism, falsificationCriteria: proposal.falsificationCriteria }, null, 2),
    "\nWhen finished, your final message must be the JSON report requested by the output schema, listing every record ID you created and every seed range you opened.",
  ].join("\n\n");
}

function reviewPrompt(report: any, experimentId: string, sessionRel: string, iteration: number): string {
  return [
    "You are an independent auditor for one completed Drop7 research experiment in this repository. Read docs/agents/project-nature.md, then .agents/skills/audit-drop7-experiment/SKILL.md completely and follow it. Keep the audit separate from any repair: do not modify any file except your review.",
    `Write your review to runs/${sessionRel}/iteration-${String(iteration).padStart(2, "0")}/review.md (create only that file). Do not run gameplay or training.`,
    `Experiment: ${experimentId}. Implementer report: ${JSON.stringify(report, null, 2)}`,
    "Check: source/protocol/result hashes, seed roles and that no protected/final seed was opened, per-game counts and canonical order, arithmetic of the metrics and bounds, censoring vs partial vs invalid, claim language vs evidence tier, and whether the gate was evaluated once. State verdict: sound / sound-with-corrections / unsound, with file:line evidence. Under 1,500 words.",
  ].join("\n\n");
}

// ---------------------------------------------------------------------------
// Session state
// ---------------------------------------------------------------------------

interface SessionState {
  sessionId: string;
  createdAt: string;
  options: Options;
  iterations: Array<{ iteration: number; status: string; theoryId?: string; experimentId?: string; title?: string; outcome?: string; notes?: string; costUsd?: number }>;
}

function loadOrCreateSession(o: Options): { dir: string; rel: string; state: SessionState } {
  const base = path.join(REPO_ROOT, "runs/orchestrator");
  mkdirSync(base, { recursive: true });
  if (o.resume) {
    const dir = path.join(base, o.resume);
    const state = readJson<SessionState>(path.join(dir, "state.json"));
    if (!state) throw new Error(`no session state at ${dir}`);
    return { dir, rel: `orchestrator/${o.resume}`, state };
  }
  const sessionId = `ORCH-${isoNow().replace(/[-:]/g, "")}-${randomBytes(4).toString("hex")}`;
  const dir = path.join(base, sessionId);
  mkdirSync(dir, { recursive: true });
  const state: SessionState = { sessionId, createdAt: isoNow(), options: o, iterations: [] };
  writeJson(path.join(dir, "state.json"), state);
  return { dir, rel: `orchestrator/${sessionId}`, state };
}

function save(dir: string, state: SessionState): void {
  writeJson(path.join(dir, "state.json"), state);
}

function log(line: string): void {
  console.log(`[do-research ${new Date().toISOString().slice(11, 19)}] ${line}`);
}

// ---------------------------------------------------------------------------
// Commit
// ---------------------------------------------------------------------------

function commitIteration(ids: { theoryId?: string; experimentId?: string; contributionId: string; runIds: string[]; resultIds: string[] }, title: string, o: Options): void {
  const paths = ["research", "approaches", "web/content/log", "docs/exploratory/lease-map.md"];
  execFileSync("git", ["add", "-A", "--", ...paths], { cwd: REPO_ROOT });
  const staged = execFileSync("git", ["diff", "--cached", "--name-only"], { cwd: REPO_ROOT, encoding: "utf8" }).trim();
  if (!staged) { log("nothing to commit"); return; }
  const message = [
    `result(orchestrator): ${title.slice(0, 60)}`,
    "",
    "Why:",
    "- Autonomous iteration of ./do-research; records carry validity, outcome and tier.",
    "",
    "Validation:",
    "- make research-validate (passed before commit)",
    "",
    `Theory-ID: ${ids.theoryId ?? "none"}`,
    `Experiment-ID: ${ids.experimentId ?? "none"}`,
    `Run-ID: ${ids.runIds[0] ?? "none"}`,
    `Result-ID: ${ids.resultIds[0] ?? "none"}`,
    `Contribution-ID: ${ids.contributionId}`,
    "Evidence-Change: see result record",
    "Result-SHA256: none",
    "",
    "Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>",
  ].join("\n");
  const messagePath = path.join(REPO_ROOT, ".git", "DO_RESEARCH_COMMIT_MSG");
  writeFileSync(messagePath, message + "\n");
  const lint = spawnSync("python3", [path.join(REPO_ROOT, ".agents/skills/million-point-research/scripts/researchctl.py"), "commit-lint", messagePath], { cwd: REPO_ROOT, encoding: "utf8" });
  if (lint.status !== 0) { log(`commit-lint failed; leaving changes staged but uncommitted:\n${lint.stdout}${lint.stderr}`); return; }
  execFileSync("git", ["commit", "-F", messagePath], { cwd: REPO_ROOT });
  log(`committed ${gitShort()}`);
  if (o.push) {
    const branch = execFileSync("git", ["rev-parse", "--abbrev-ref", "HEAD"], { cwd: REPO_ROOT, encoding: "utf8" }).trim();
    execFileSync("git", ["push", "origin", branch], { cwd: REPO_ROOT });
    log(`pushed ${branch}`);
  }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

async function main(): Promise<void> {
  const o = parseArgs(process.argv.slice(2));
  const planner = makePlanner({ timeoutMs: 20 * 60_000, maxBudgetUsd: o.plannerBudgetUsd });
  const { dir, rel, state } = loadOrCreateSession(o);
  log(`session ${state.sessionId} planner=${planner.name}/${planner.model} reviewer=${o.reviewerModel} pool=${o.leasePool.startHex}-${o.leasePool.endExclusiveHex}`);
  const platform = "do-research orchestrator";
  const history: HistoryEntry[] = state.iterations.filter((it) => it.title).map((it) => ({ iteration: it.iteration, theoryId: it.theoryId, experimentId: it.experimentId, title: it.title!, outcome: it.outcome ?? it.status, notes: it.notes ?? "" }));
  let consecutiveFailures = 0;

  for (let iteration = state.iterations.length + 1; iteration <= o.maxIterations; iteration += 1) {
    if (existsSync(path.join(dir, "STOP"))) { log("STOP file present; ending"); break; }
    const itDir = path.join(dir, `iteration-${String(iteration).padStart(2, "0")}`);
    mkdirSync(itDir, { recursive: true });
    const entry: SessionState["iterations"][number] = { iteration, status: "planning" };
    state.iterations.push(entry);
    save(dir, state);

    // 1. plan
    const pre = validateRecords();
    if (!pre.ok) { log(`research-validate fails before planning; fix the tree first:\n${pre.output.slice(-2000)}`); entry.status = "aborted-invalid-tree"; save(dir, state); break; }
    const brief = gatherContext(history, o.focus);
    writeFileSync(path.join(itDir, "brief.md"), brief);
    log(`iteration ${iteration}: planning`);
    let proposal = await propose(planner, brief, o);
    writeJson(path.join(itDir, "proposal-v1.json"), proposal);

    // 2. critique + one revision
    if (!proposal.abstain && !o.skipCritique) {
      log("critique by " + o.reviewerModel);
      await runOpencodeReviewer({ prompt: critiquePrompt(proposal, rel, iteration), model: o.reviewerModel, cwd: REPO_ROOT, logPath: path.join(itDir, "critique.log"), timeoutMs: 30 * 60_000 });
      const critique = existsSync(path.join(itDir, "critique.md")) ? readFileSync(path.join(itDir, "critique.md"), "utf8") : "(the reviewer wrote no critique file)";
      proposal = await propose(planner, brief, o, critique, proposal);
      writeJson(path.join(itDir, "proposal-v2.json"), proposal);
    }
    if (proposal.abstain) { log(`planner abstained: ${proposal.abstainReason}`); entry.status = "abstained"; entry.notes = proposal.abstainReason; save(dir, state); break; }
    if (proposal.experiment.wallHours > o.wallHoursPerIteration || proposal.experiment.cpuThreads > o.cpuThreads) {
      log("proposal exceeds the iteration budget; recording and skipping"); entry.status = "over-budget"; entry.title = proposal.title; entry.notes = "planner exceeded budget"; save(dir, state); consecutiveFailures += 1; if (consecutiveFailures >= 2) break; continue;
    }
    entry.title = proposal.title;
    if (o.dryRun) { log(`dry run: proposal saved to ${itDir}`); entry.status = "dry-run"; save(dir, state); continue; }

    // 3. register
    const theory = createTheory({ slug: proposal.slug, title: proposal.title, claim: proposal.claim, mechanism: proposal.mechanism, falsification: proposal.falsificationCriteria, platform, model: `${planner.name}:${planner.model}` });
    const entryPoint = `approaches/${proposal.family}/${proposal.slug}/README.mdx`;
    mkdirSync(path.join(REPO_ROOT, `approaches/${proposal.family}/${proposal.slug}`), { recursive: true });
    if (!existsSync(path.join(REPO_ROOT, entryPoint))) {
      writeFileSync(path.join(REPO_ROOT, entryPoint), `---\ntitle: ${proposal.title}\nfamily: ${proposal.family}\nsummary: "${proposal.claim.replace(/"/g, "'").slice(0, 200)}"\nstatus: preregistered\nevidence: none\nreads: public\ndraft: true\n---\n\nPlaceholder written by ./do-research; the implementer replaces it.\n`);
    }
    const experiment = createExperiment({
      slug: proposal.slug, title: proposal.experiment.title, theoryId: theory.id, hypothesis: proposal.experiment.hypothesis, candidate: proposal.experiment.candidateName, entryPoint, classification: proposal.experiment.classification, platform, model: `${planner.name}:${planner.model}`,
      patch: {
        benchmarkTier: proposal.experiment.benchmarkTier,
        metrics: { primary: proposal.experiment.primaryMetric, secondary: proposal.experiment.secondaryMetrics, statisticalUnit: "whole-game", uncertaintyMethod: "Specify before preregistration: one-sided 95% bootstrap over whole games unless the work package names another" },
        gate: { fixedBeforeControlledData: false, passCriteria: proposal.experiment.gatePassCriteria, failureAction: proposal.experiment.failureAction, passAction: proposal.experiment.passAction },
        resources: { wallSeconds: Math.round(proposal.experiment.wallHours * 3600), cpuThreads: proposal.experiment.cpuThreads, maxHostBytes: null, maxGpuBytes: null, gpuDevices: proposal.experiment.usesGpu ? ["Radeon 8060S (gfx1151)"] : [] },
        stopConditions: proposal.experiment.stopConditions,
      },
    });
    const leases: Array<{ id: string; range: string; role: string }> = [];
    const wanted: Array<[number, string]> = [[proposal.experiment.trainingSeeds, "training"], [proposal.experiment.heldOutSeeds, "public-development"]];
    for (const [count, role] of wanted) {
      if (count <= 0) continue;
      const grant = allocateLease(o.leasePool, count, role, experiment.id, `${role} data for ${experiment.id}`);
      if (!grant) { log(`lease pool exhausted for ${count} ${role} seeds`); break; }
      leases.push({ id: grant.seedLeaseId, range: `${grant.rangeStartHex}-${grant.rangeEndExclusiveHex}`, role });
    }
    entry.theoryId = theory.id; entry.experimentId = experiment.id; entry.status = "implementing"; save(dir, state);
    writeJson(path.join(itDir, "records.json"), { theory, experiment, leases });
    log(`registered ${theory.id} / ${experiment.id}; leases ${leases.map((l) => l.range).join(", ") || "none"}`);

    // 4. implement
    const implementer = await runClaudeImplementer({
      prompt: implementerPrompt({ proposal, theoryId: theory.id, experimentId: experiment.id, theoryPath: theory.path, experimentPath: experiment.path, leases, sessionRel: rel, iteration, o }),
      cwd: REPO_ROOT, logPath: path.join(itDir, "implementer.log"), timeoutMs: o.wallHoursPerIteration * 3600_000, maxTurns: o.implementerMaxTurns, maxBudgetUsd: o.maxUsdPerIteration, model: o.implementerModel,
    });
    const report = implementer.report ?? { status: implementer.timedOut ? "partial" : "failed", summary: implementer.timedOut ? "implementer hit the wall-clock cap" : `implementer exited ${implementer.exitCode} without a report`, runIds: [], resultIds: [], contributionIds: [], scientificOutcome: "not-run", artifactPaths: [], seedsOpened: [], limitations: [] };
    writeJson(path.join(itDir, "implementer-report.json"), report);
    entry.costUsd = implementer.costUsd;
    log(`implementer: ${report.status} — ${String(report.summary).slice(0, 200)}`);

    // 5. review
    let review = "(review skipped)";
    if (!o.skipReview) {
      await runOpencodeReviewer({ prompt: reviewPrompt(report, experiment.id, rel, iteration), model: o.reviewerModel, cwd: REPO_ROOT, logPath: path.join(itDir, "review.log"), timeoutMs: 45 * 60_000 });
      review = existsSync(path.join(itDir, "review.md")) ? readFileSync(path.join(itDir, "review.md"), "utf8") : "(the reviewer wrote no review file)";
    }

    // 6. orchestration record + validation + assessment
    const post = validateRecords();
    const contributionId = createContribution({
      summary: `Orchestrated iteration ${iteration} of ${state.sessionId}: planner ${planner.name}/${planner.model} proposed "${proposal.title}"; implementer reported ${report.status}; reviewer ${o.reviewerModel}. Records validate: ${post.ok}.`,
      level: "L4", role: "orchestration", degree: "lead", platform, model: `${planner.name}:${planner.model}`, theoryId: theory.id, experimentId: experiment.id,
      artifacts: [`runs/${rel}/iteration-${String(iteration).padStart(2, "0")}/`], validation: [post.ok ? "make research-validate passed" : `make research-validate FAILED: ${post.output.slice(-500)}`],
    });
    const assessment = await planner.complete({
      system: "You assess one completed research iteration for a Drop7 AI program. Be skeptical and narrow: a fail is a completed contribution; an invalid run supports nothing. Reply with JSON only.",
      user: `Implementer report:\n${JSON.stringify(report, null, 2)}\n\nIndependent review:\n${review.slice(0, 12000)}\n\nRecord validation: ${post.ok ? "passes" : "FAILS:\n" + post.output.slice(-2000)}\n\nDecide: continue (plan a new theory next), followup (name the direction), or stop (budget better spent by a human decision).`,
      schema: ASSESSMENT_SCHEMA,
    });
    const verdict: any = assessment.json ?? { decision: "stop", outcomeLabel: "unassessed", notes: "assessment returned no JSON", followupDirection: "" };
    writeJson(path.join(itDir, "assessment.json"), verdict);
    entry.status = post.ok ? "completed" : "completed-invalid-records";
    entry.outcome = `${report.status}/${report.scientificOutcome}/${verdict.outcomeLabel}`;
    entry.notes = verdict.notes;
    save(dir, state);
    history.push({ iteration, theoryId: theory.id, experimentId: experiment.id, title: proposal.title, outcome: entry.outcome, notes: verdict.notes });
    consecutiveFailures = report.status === "failed" ? consecutiveFailures + 1 : 0;

    if (o.commit && post.ok) commitIteration({ theoryId: theory.id, experimentId: experiment.id, contributionId, runIds: report.runIds ?? [], resultIds: report.resultIds ?? [] }, proposal.title, o);
    if (verdict.decision === "stop") { log(`assessor says stop: ${verdict.notes}`); break; }
    if (verdict.decision === "followup" && verdict.followupDirection) o.focus = verdict.followupDirection;
    if (consecutiveFailures >= 2) { log("two consecutive failed iterations; stopping"); break; }
  }
  log(`session ${state.sessionId} finished: ${state.iterations.map((it) => `${it.iteration}:${it.status}`).join(" ")}`);
}

main().catch((error) => {
  console.error(`do-research failed: ${error instanceof Error ? error.stack ?? error.message : String(error)}`);
  process.exit(1);
});
