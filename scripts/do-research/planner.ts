// Planner backends for ./do-research.
//
// One interface, three providers.  The default is the `claude` CLI in print
// mode, which uses the operator's existing Claude Code login and needs no API
// key.  The Anthropic SDK backend is used when DO_RESEARCH_PLANNER=anthropic
// (requires ANTHROPIC_API_KEY or an `ant auth login` profile); an OpenAI-
// compatible HTTP backend is used when DO_RESEARCH_PLANNER=openai (requires
// OPENAI_API_KEY, optional OPENAI_BASE_URL and OPENAI_MODEL).
//
// Every backend returns the model's text; callers that need JSON pass a
// schema and get a parsed object or a thrown error — never a guessed value.

import { spawn } from "node:child_process";

export type PlannerName = "claude-cli" | "anthropic" | "openai";

export interface PlannerRequest {
  system: string;
  user: string;
  /** JSON Schema the reply must satisfy; when set, the result is parsed JSON. */
  schema?: Record<string, unknown>;
  maxTokens?: number;
}

export interface Planner {
  readonly name: PlannerName;
  readonly model: string;
  complete(request: PlannerRequest): Promise<{ text: string; json?: unknown; costUsd?: number }>;
}

function runProcess(
  command: string,
  args: string[],
  input: string | undefined,
  env: NodeJS.ProcessEnv,
  timeoutMs: number,
): Promise<{ code: number | null; stdout: string; stderr: string }> {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { env, stdio: ["pipe", "pipe", "pipe"] });
    let stdout = "";
    let stderr = "";
    const timer = setTimeout(() => {
      child.kill("SIGTERM");
      setTimeout(() => child.kill("SIGKILL"), 10_000).unref();
    }, timeoutMs);
    child.stdout.on("data", (d) => (stdout += d.toString()));
    child.stderr.on("data", (d) => (stderr += d.toString()));
    child.on("error", (error) => {
      clearTimeout(timer);
      reject(error);
    });
    child.on("close", (code) => {
      clearTimeout(timer);
      resolve({ code, stdout, stderr });
    });
    if (input !== undefined) child.stdin.write(input);
    child.stdin.end();
  });
}

function extractJson(text: string): unknown {
  const trimmed = text.trim();
  try {
    return JSON.parse(trimmed);
  } catch {
    // tolerate a fenced block or prose around one JSON object
    const fenced = trimmed.match(/```(?:json)?\s*([\s\S]*?)```/);
    if (fenced) return JSON.parse(fenced[1]);
    const start = trimmed.indexOf("{");
    const end = trimmed.lastIndexOf("}");
    if (start >= 0 && end > start) return JSON.parse(trimmed.slice(start, end + 1));
    throw new Error("planner reply is not JSON");
  }
}

/** `claude -p` with the operator's login. */
export class ClaudeCliPlanner implements Planner {
  readonly name = "claude-cli" as const;
  readonly model: string;
  private readonly timeoutMs: number;
  private readonly maxBudgetUsd: number;
  constructor(model: string, timeoutMs: number, maxBudgetUsd: number) {
    this.model = model;
    this.timeoutMs = timeoutMs;
    this.maxBudgetUsd = maxBudgetUsd;
  }

  async complete(request: PlannerRequest) {
    // The brief goes through stdin (it can be tens of kilobytes); the planner
    // gets no tools, so the only thing it can do is answer.
    const args = [
      "-p",
      "--output-format", "json",
      "--model", this.model,
      "--max-turns", "3",
      "--max-budget-usd", String(this.maxBudgetUsd),
      "--system-prompt", request.system,
      "--tools", "",
    ];
    if (request.schema) args.push("--json-schema", JSON.stringify(request.schema));
    // A nested Claude Code session refuses to start while CLAUDECODE is set.
    const env = { ...process.env };
    delete env.CLAUDECODE;
    const { code, stdout, stderr } = await runProcess("claude", args, request.user, env, this.timeoutMs);
    let payload: Record<string, unknown>;
    try {
      payload = JSON.parse(stdout) as Record<string, unknown>;
    } catch {
      throw new Error(`claude -p did not return JSON (exit ${code}): ${stderr.slice(0, 500)} ${stdout.slice(0, 500)}`);
    }
    if (payload.is_error) {
      throw new Error(`claude -p error: ${String(payload.result)} | ${JSON.stringify(payload).slice(0, 1500)} | stderr: ${stderr.slice(0, 500)}`);
    }
    const text = typeof payload.result === "string" ? payload.result : JSON.stringify(payload.result);
    const json = request.schema ? (payload.structured_output ?? extractJson(text)) : undefined;
    return { text, json, costUsd: typeof payload.total_cost_usd === "number" ? payload.total_cost_usd : undefined };
  }
}

/** Anthropic SDK backend (optional dependency, loaded lazily). */
export class AnthropicPlanner implements Planner {
  readonly name = "anthropic" as const;
  readonly model: string;
  private readonly timeoutMs: number;
  constructor(model: string, timeoutMs: number) {
    this.model = model;
    this.timeoutMs = timeoutMs;
  }

  async complete(request: PlannerRequest) {
    let sdk: any;
    try {
      sdk = await import("@anthropic-ai/sdk");
    } catch {
      throw new Error("DO_RESEARCH_PLANNER=anthropic needs `npm install @anthropic-ai/sdk`");
    }
    const Anthropic = sdk.default ?? sdk.Anthropic;
    const client = new Anthropic({ timeout: this.timeoutMs });
    const body: Record<string, unknown> = {
      model: this.model,
      max_tokens: request.maxTokens ?? 16000,
      system: request.system,
      messages: [{ role: "user", content: request.user }],
      thinking: { type: "adaptive" },
    };
    if (request.schema) {
      body.output_config = { format: { type: "json_schema", schema: request.schema } };
    }
    const response = await client.messages.create(body);
    if (response.stop_reason === "refusal") {
      throw new Error(`planner refused: ${response.stop_details?.explanation ?? "no explanation"}`);
    }
    const text = response.content
      .filter((block: { type: string }) => block.type === "text")
      .map((block: { text: string }) => block.text)
      .join("\n");
    const json = request.schema ? extractJson(text) : undefined;
    return { text, json };
  }
}

/** OpenAI-compatible chat completions over HTTP (OPENAI_BASE_URL, OPENAI_API_KEY, OPENAI_MODEL). */
export class OpenAiCompatiblePlanner implements Planner {
  readonly name = "openai" as const;
  readonly model: string;
  private readonly baseUrl: string;
  private readonly apiKey: string;
  private readonly timeoutMs: number;
  constructor(model: string, baseUrl: string, apiKey: string, timeoutMs: number) {
    this.model = model;
    this.baseUrl = baseUrl;
    this.apiKey = apiKey;
    this.timeoutMs = timeoutMs;
  }

  async complete(request: PlannerRequest) {
    const body: Record<string, unknown> = {
      model: this.model,
      messages: [
        { role: "system", content: request.system },
        { role: "user", content: request.user },
      ],
      max_tokens: request.maxTokens ?? 16000,
    };
    if (request.schema) {
      body.response_format = { type: "json_schema", json_schema: { name: "reply", schema: request.schema, strict: false } };
    }
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), this.timeoutMs);
    try {
      const response = await fetch(`${this.baseUrl.replace(/\/$/, "")}/chat/completions`, {
        method: "POST",
        headers: { "content-type": "application/json", authorization: `Bearer ${this.apiKey}` },
        body: JSON.stringify(body),
        signal: controller.signal,
      });
      if (!response.ok) throw new Error(`openai-compatible planner HTTP ${response.status}: ${(await response.text()).slice(0, 500)}`);
      const payload = (await response.json()) as { choices?: Array<{ message?: { content?: string } }> };
      const text = payload.choices?.[0]?.message?.content ?? "";
      const json = request.schema ? extractJson(text) : undefined;
      return { text, json };
    } finally {
      clearTimeout(timer);
    }
  }
}

export function makePlanner(options: { timeoutMs: number; maxBudgetUsd: number }): Planner {
  const which = (process.env.DO_RESEARCH_PLANNER ?? "claude-cli") as PlannerName;
  switch (which) {
    case "claude-cli":
      return new ClaudeCliPlanner(process.env.DO_RESEARCH_MODEL ?? "claude-opus-5", options.timeoutMs, options.maxBudgetUsd);
    case "anthropic":
      return new AnthropicPlanner(process.env.DO_RESEARCH_MODEL ?? "claude-opus-5", options.timeoutMs);
    case "openai": {
      const apiKey = process.env.OPENAI_API_KEY;
      if (!apiKey) throw new Error("DO_RESEARCH_PLANNER=openai needs OPENAI_API_KEY");
      return new OpenAiCompatiblePlanner(
        process.env.OPENAI_MODEL ?? process.env.DO_RESEARCH_MODEL ?? "gpt-5",
        process.env.OPENAI_BASE_URL ?? "https://api.openai.com/v1",
        apiKey,
        options.timeoutMs,
      );
    }
    default:
      throw new Error(`unknown DO_RESEARCH_PLANNER ${which}`);
  }
}
