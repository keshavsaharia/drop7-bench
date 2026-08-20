import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";

import {
  initialFairPolicyWeights,
  runFairPolicyGame,
  type FairPolicyWeights,
} from "./tune.ts";

interface SweepArguments {
  seed: number;
  games: number;
  samples: number;
  maxMoves: number;
  policySeed: number;
  rollouts: number;
  rolloutHorizon: number;
  continuationSamples: number;
  riskAversion: number;
  base?: string;
  fixedOverrides: Partial<FairPolicyWeights>;
  sweepName: keyof FairPolicyWeights;
  sweepValues: number[];
  only: boolean;
}

/**
 * Fast, paired coordinate ablations for heuristic theories. This is not a
 * tuner: callers choose the tested values up front, and the script reports
 * every candidate on exactly the same game seeds.
 */
export function runSweep(options: SweepArguments) {
  const baseline = {
    ...loadWeights(options.base),
    ...options.fixedOverrides,
  };
  const settings = {
    samples: options.samples,
    policySeed: options.policySeed,
    ...(options.rollouts === 0
      ? {}
      : {
          rollout: {
            count: options.rollouts,
            horizon: options.rolloutHorizon,
            continuationSamples: options.continuationSamples,
            riskAversion: options.riskAversion,
          },
        }),
  };
  const baselineResults = Array.from({ length: options.games }, (_, offset) =>
    runFairPolicyGame(
      (options.seed + offset) >>> 0,
      baseline,
      settings,
      options.maxMoves,
    ),
  );
  const baselineScores = new Map(
    baselineResults.map((result) => [result.seed, result.score]),
  );

  process.stdout.write(`baseline · ${format(baselineResults)}\n`);
  if (options.only) return;
  for (const value of options.sweepValues) {
    const weights = { ...baseline, [options.sweepName]: value };
    const results = Array.from({ length: options.games }, (_, offset) =>
      runFairPolicyGame(
        (options.seed + offset) >>> 0,
        weights,
        settings,
        options.maxMoves,
      ),
    );
    const deltas = results.map(
      (result) => result.score - baselineScores.get(result.seed)!,
    );
    const wins = deltas.filter((delta) => delta > 0).length;
    const ties = deltas.filter((delta) => delta === 0).length;
    process.stdout.write(
      `${String(options.sweepName)}=${value} · ${format(results)} · ` +
        `paired ${signed(Math.round(mean(deltas)))} · W/T/L ` +
        `${wins}/${ties}/${deltas.length - wins - ties}\n`,
    );
  }
}

function loadWeights(path: string | undefined): FairPolicyWeights {
  if (!path) return initialFairPolicyWeights();
  const parsed = JSON.parse(readFileSync(path, "utf8")) as {
    champion?: { weights?: FairPolicyWeights };
  };
  if (!parsed.champion?.weights) {
    throw new Error("--base must be a fair-tuner artifact");
  }
  return parsed.champion.weights;
}

function format(
  results: readonly ReturnType<typeof runFairPolicyGame>[],
) {
  const scores = results.map((result) => result.score).sort((a, b) => a - b);
  return [
    `mean ${Math.round(mean(scores)).toLocaleString()}`,
    `median ${scores[Math.floor(scores.length / 2)].toLocaleString()}`,
    `moves ${mean(results.map((result) => result.moves)).toFixed(1)}`,
    `max ${scores.at(-1)!.toLocaleString()}`,
    `censored ${results.filter((result) => result.censored).length}/${results.length}`,
  ].join(" · ");
}

function mean(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function signed(value: number) {
  return `${value >= 0 ? "+" : "−"}${Math.abs(value).toLocaleString()}`;
}

function parseArguments(arguments_: readonly string[]): SweepArguments {
  const valueAfter = (flag: string) => {
    const index = arguments_.indexOf(flag);
    return index < 0 ? undefined : arguments_[index + 1];
  };
  const integer = (flag: string, fallback: number, minimum = 1) => {
    const raw = valueAfter(flag);
    const value = raw === undefined ? fallback : Number(raw);
    if (!Number.isSafeInteger(value) || value < minimum) {
      throw new Error(`${flag} must be an integer of at least ${minimum}`);
    }
    return value;
  };
  const seed = integer("--seed", 0x1d70_0000, 0);
  const policySeed = integer("--policy-seed", 0xfa17_d707, 0);
  if (seed > 0xffff_ffff || policySeed > 0xffff_ffff) {
    throw new Error("seeds must be uint32 integers");
  }
  const riskRaw = valueAfter("--risk");
  const riskAversion = riskRaw === undefined ? 0 : Number(riskRaw);
  if (!Number.isFinite(riskAversion) || riskAversion < 0) {
    throw new Error("--risk must be a non-negative finite number");
  }
  const sweepName = valueAfter("--name");
  const sweepValues = valueAfter("--values");
  const known = Object.keys(initialFairPolicyWeights());
  const only = arguments_.includes("--only");
  if (!only && (!sweepName || !known.includes(sweepName))) {
    throw new Error(`--name must be one of ${known.join(", ")}`);
  }
  if (!only && !sweepValues) throw new Error("--values is required");
  const parsedValues = sweepValues ? sweepValues.split(",").map(Number) : [];
  if (
    (!only && parsedValues.length === 0) ||
    parsedValues.some((value) => !Number.isFinite(value))
  ) {
    throw new Error("--values must be a comma-separated list of finite numbers");
  }
  const fixedOverrides: Record<string, number> = {};
  const fixed = valueAfter("--set");
  if (fixed) {
    for (const assignment of fixed.split(",")) {
      const [name, rawValue] = assignment.split("=");
      const value = Number(rawValue);
      if (!known.includes(name) || !Number.isFinite(value)) {
        throw new Error(`Invalid --set assignment ${assignment}`);
      }
      fixedOverrides[name] = value;
    }
  }
  return {
    seed,
    policySeed,
    games: integer("--games", 64),
    samples: integer("--samples", 3),
    rollouts: integer("--rollouts", 0, 0),
    rolloutHorizon: integer("--horizon", 6),
    continuationSamples: integer("--continuation-samples", 2),
    riskAversion,
    maxMoves: integer("--max-moves", 1_000),
    base: valueAfter("--base"),
    fixedOverrides: fixedOverrides as Partial<FairPolicyWeights>,
    sweepName: (sweepName ?? known[0]) as keyof FairPolicyWeights,
    sweepValues: parsedValues,
    only,
  };
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  runSweep(parseArguments(process.argv.slice(2)));
}
