// Headless agent wrappers for ./do-research.
//
// The implementer is `claude -p` running unattended (the operator who starts
// ./do-research has accepted that by running it), with a turn cap, a dollar
// cap and a wall-clock cap; its final message is forced into a JSON report by
// --json-schema.  The reviewer is `opencode run` with a different model
// family, so the audit does not share the implementer's blind spots.  Both
// stream their output to a log file in the session directory.

import { spawn } from "node:child_process";
import { createWriteStream } from "node:fs";

export interface AgentOutcome {
  exitCode: number | null;
  timedOut: boolean;
  stdout: string;
  costUsd?: number;
}

function spawnLogged(command: string, args: string[], logPath: string, timeoutMs: number, cwd: string, env: NodeJS.ProcessEnv): Promise<AgentOutcome> {
  return new Promise((resolve, reject) => {
    const log = createWriteStream(logPath, { flags: "a" });
    log.write(`\n==== ${new Date().toISOString()} ${command} ${args.map((a) => (a.length > 120 ? a.slice(0, 117) + "..." : a)).join(" ")}\n`);
    const child = spawn(command, args, { cwd, env, stdio: ["ignore", "pipe", "pipe"] });
    let stdout = "";
    let timedOut = false;
    const timer = setTimeout(() => {
      timedOut = true;
      child.kill("SIGTERM");
      setTimeout(() => child.kill("SIGKILL"), 30_000).unref();
    }, timeoutMs);
    child.stdout.on("data", (d) => { stdout += d.toString(); log.write(d); });
    child.stderr.on("data", (d) => log.write(d));
    child.on("error", (error) => { clearTimeout(timer); log.end(); reject(error); });
    child.on("close", (code) => { clearTimeout(timer); log.end(); resolve({ exitCode: code, timedOut, stdout }); });
  });
}

export const IMPLEMENTER_REPORT_SCHEMA = {
  type: "object",
  properties: {
    status: { type: "string", enum: ["completed", "partial", "failed", "blocked"] },
    summary: { type: "string" },
    theoryId: { type: "string" },
    experimentId: { type: "string" },
    runIds: { type: "array", items: { type: "string" } },
    resultIds: { type: "array", items: { type: "string" } },
    contributionIds: { type: "array", items: { type: "string" } },
    scientificOutcome: { type: "string", enum: ["pass", "fail", "inconclusive", "not-applicable", "not-run"] },
    evidenceTier: { type: "string" },
    artifactPaths: { type: "array", items: { type: "string" } },
    seedsOpened: { type: "array", items: { type: "string" } },
    limitations: { type: "array", items: { type: "string" } },
    blockedOn: { type: "string" },
  },
  required: ["status", "summary", "runIds", "resultIds", "contributionIds", "scientificOutcome", "artifactPaths", "seedsOpened", "limitations"],
};

/** The permission mode an unattended implementer runs under. */
const UNATTENDED_PERMISSION_FLAG = "--permission-mode";
const UNATTENDED_PERMISSION_MODE = "bypassPermissions";

export async function runClaudeImplementer(options: { prompt: string; cwd: string; logPath: string; timeoutMs: number; maxTurns: number; maxBudgetUsd: number; model?: string }): Promise<AgentOutcome & { report?: Record<string, unknown> }> {
  const args = [
    "-p", options.prompt,
    "--output-format", "json",
    "--json-schema", JSON.stringify(IMPLEMENTER_REPORT_SCHEMA),
    UNATTENDED_PERMISSION_FLAG, UNATTENDED_PERMISSION_MODE,
    "--max-turns", String(options.maxTurns),
    "--max-budget-usd", String(options.maxBudgetUsd),
  ];
  if (options.model) args.push("--model", options.model);
  const env = { ...process.env };
  delete env.CLAUDECODE;
  const outcome = await spawnLogged("claude", args, options.logPath, options.timeoutMs, options.cwd, env);
  let report: Record<string, unknown> | undefined;
  try {
    const payload = JSON.parse(outcome.stdout) as Record<string, unknown>;
    outcome.costUsd = typeof payload.total_cost_usd === "number" ? payload.total_cost_usd : undefined;
    if (payload.structured_output && typeof payload.structured_output === "object") report = payload.structured_output as Record<string, unknown>;
    else if (typeof payload.result === "string") {
      try { report = JSON.parse(payload.result); } catch { report = { status: payload.is_error ? "failed" : "partial", summary: String(payload.result).slice(0, 2000) }; }
    }
  } catch {
    // non-JSON stdout: the log has the transcript; leave report undefined
  }
  return { ...outcome, report };
}

export async function runOpencodeReviewer(options: { prompt: string; model: string; cwd: string; logPath: string; timeoutMs: number }): Promise<AgentOutcome> {
  const args = ["run", "-m", options.model, "--auto", options.prompt];
  return spawnLogged("opencode", args, options.logPath, options.timeoutMs, options.cwd, process.env);
}
