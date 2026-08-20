import { readFile } from "node:fs/promises";
import { mkdir, rename, writeFile } from "node:fs/promises";
import { dirname, resolve } from "node:path";
import { pathToFileURL } from "node:url";

import {
  BOARD_SIZE,
  EMPTY,
  seededRandom,
  type GameState,
  type MoveResult,
} from "../../../src/core/typescript/engine.ts";
import { extractHeuristicFeatures } from "../../../src/core/typescript/heuristic.ts";
import {
  DQN_FEATURE_SIZE,
  DenseQNetwork,
  actionInput,
  chooseAction,
  compactDqnState,
  compileDqnCheckpoint,
  expandDqnState,
  initialDqnState,
  playActualDqnMove,
  shapedReward,
  type CompactState,
  type NetworkSnapshot,
  type TrainingSample,
} from "./train.ts";
import { planOracleMove } from "../../oracle-curriculum/perfect-information-oracle/main.ts";

/**
 * DQN-v2: five-step credit assignment and trajectory-shaped replay.
 *
 * The optional oracle demonstrations are explicitly privileged training data.
 * Oracle seeds and future RNG are used only to generate transitions; action
 * inputs and every deployed decision remain observable-state-only.
 */

const V2_TRAINING_SEED_START = 0x8d70_0000;
const V2_DEMO_SEED_START = 0x8e70_0000;
const V2_PROBE_SEED_START = 0x9d70_0000;
const RESERVED_FINAL_SEED_START = 0xd700_0000;
const DEFAULT_CHECKPOINT = "/tmp/drop7-dqn-360k.json";
const DEFAULT_OUTPUT = "drop7-dqn-v2.json";
const DEFAULT_STEPS = 360_000;
const DEFAULT_TRAINING_GAMES = 4_096;
const DEFAULT_PROBE_GAMES = 64;
const DEFAULT_MAX_MOVES = 500;
const DEFAULT_REPLAY_CAPACITY = 120_000;
const DEFAULT_BATCH_SIZE = 24;
const DEFAULT_WARMUP = 1_500;
const DEFAULT_TRAIN_EVERY = 4;
const DEFAULT_TARGET_EVERY = 500;
const DEFAULT_EVALUATE_EVERY = 45_000;
const DEFAULT_LEARNING_RATE = 0.00045;
const DEFAULT_GAMMA = 0.99;
const DEFAULT_N_STEP = 5;
const DEFAULT_EPSILON_START = 0.25;
const DEFAULT_EPSILON_END = 0.03;
const DEFAULT_EPSILON_FRACTION = 0.75;
const DEFAULT_DEMO_GAMES = 16;
const DEFAULT_DEMO_MAX_MOVES = 100;
const DEFAULT_DEMO_FRACTION = 0.2;
const DEFAULT_ORACLE_DEPTH = 4;
const DEFAULT_ORACLE_BEAM = 128;
const DEFAULT_POLICY_SAMPLES = 2;
const DEFAULT_TRAINER_SEED = 0xd2b2_e202;
const DEFAULT_POLICY_SEED = 0xd0b1_d707;
const TERMINAL_PENALTY = 12;
const PRIORITY_ALPHA = 0.6;

const BOARD_CELLS = BOARD_SIZE * BOARD_SIZE;

interface Arguments {
  checkpoint: string;
  outputPath: string;
  steps: number;
  trainingGames: number;
  probeGames: number;
  maxMoves: number;
  replayCapacity: number;
  batchSize: number;
  warmup: number;
  trainEvery: number;
  targetEvery: number;
  evaluateEvery: number;
  learningRate: number;
  gamma: number;
  nStep: number;
  epsilonStart: number;
  epsilonEnd: number;
  epsilonFraction: number;
  demoGames: number;
  demoMaxMoves: number;
  demoFraction: number;
  oracleDepth: number;
  oracleBeam: number;
  policySamples: number;
  trainerSeed: number;
  policySeed: number;
  selfTest: boolean;
}

interface NstepExperience {
  state: CompactState;
  action: number;
  reward: number;
  nextState: CompactState;
  bootstrapDiscount: number;
  done: boolean;
}

interface SampledExperience {
  index: number;
  experience: NstepExperience;
}

interface RawStep {
  state: GameState;
  action: number;
  baseReward: number;
  move: MoveResult;
  nextState: GameState;
  done: boolean;
}

interface BoardMetrics {
  occupied: number;
  solids: number;
  lowCapLoad: number;
  adjacentLowCapLoad: number;
  directPotential: number;
}

interface GameResult {
  seed: number;
  score: number;
  moves: number;
  censored: boolean;
  maxChain: number;
}

interface Summary {
  games: number;
  meanScore: number;
  medianScore: number;
  minimumScore: number;
  maximumScore: number;
  meanMoves: number;
  censoredGames: number;
  meanMaxChain: number;
  results: readonly GameResult[];
}

interface CurvePoint {
  step: number;
  episodes: number;
  epsilon: number;
  meanTdLoss: number;
  meanAbsoluteTdError: number;
  onlineReplaySize: number;
  probe: Omit<Summary, "results">;
}

interface SumTree {
  leafCount: number;
  values: Float64Array;
}

class PrioritizedReplay {
  readonly capacity: number;
  size = 0;
  private cursor = 0;
  private maximumPriority = 1;
  private readonly boards: Uint8Array;
  private readonly nextBoards: Uint8Array;
  private readonly metadata: Uint16Array;
  private readonly nextMetadata: Uint16Array;
  private readonly actions: Uint8Array;
  private readonly rewards: Float32Array;
  private readonly discounts: Float32Array;
  private readonly dones: Uint8Array;
  private readonly priorities: SumTree;

  constructor(capacity: number) {
    this.capacity = capacity;
    this.boards = new Uint8Array(capacity * BOARD_CELLS);
    this.nextBoards = new Uint8Array(capacity * BOARD_CELLS);
    this.metadata = new Uint16Array(capacity * 4);
    this.nextMetadata = new Uint16Array(capacity * 4);
    this.actions = new Uint8Array(capacity);
    this.rewards = new Float32Array(capacity);
    this.discounts = new Float32Array(capacity);
    this.dones = new Uint8Array(capacity);
    this.priorities = createSumTree(capacity);
  }

  add(experience: NstepExperience, priority = this.maximumPriority) {
    const index = this.cursor;
    writeCompactState(this.boards, this.metadata, index, experience.state);
    writeCompactState(
      this.nextBoards,
      this.nextMetadata,
      index,
      experience.nextState,
    );
    this.actions[index] = experience.action;
    this.rewards[index] = experience.reward;
    this.discounts[index] = experience.bootstrapDiscount;
    this.dones[index] = experience.done ? 1 : 0;
    this.setPriority(index, priority);
    this.cursor = (this.cursor + 1) % this.capacity;
    this.size = Math.min(this.capacity, this.size + 1);
  }

  sample(random: () => number): SampledExperience {
    if (this.size === 0) throw new Error("Cannot sample an empty replay");
    const total = this.priorities.values[1];
    const index =
      total > 0
        ? sampleSumTree(this.priorities, random() * total, this.size)
        : Math.floor(random() * this.size);
    return { index, experience: this.read(index) };
  }

  updatePriority(index: number, absoluteTdError: number) {
    this.setPriority(index, Math.max(0.01, absoluteTdError + 0.01));
  }

  byteLength() {
    return (
      this.boards.byteLength +
      this.nextBoards.byteLength +
      this.metadata.byteLength +
      this.nextMetadata.byteLength +
      this.actions.byteLength +
      this.rewards.byteLength +
      this.discounts.byteLength +
      this.dones.byteLength +
      this.priorities.values.byteLength
    );
  }

  private setPriority(index: number, rawPriority: number) {
    if (!Number.isFinite(rawPriority) || rawPriority <= 0) {
      throw new Error("Replay priority must be positive and finite");
    }
    this.maximumPriority = Math.max(this.maximumPriority, rawPriority);
    updateSumTree(this.priorities, index, rawPriority ** PRIORITY_ALPHA);
  }

  private read(index: number): NstepExperience {
    return {
      state: readCompactState(this.boards, this.metadata, index),
      action: this.actions[index],
      reward: this.rewards[index],
      nextState: readCompactState(this.nextBoards, this.nextMetadata, index),
      bootstrapDiscount: this.discounts[index],
      done: this.dones[index] === 1,
    };
  }
}

export async function trainDqnV2(options: Arguments) {
  const warmStart = await readWarmStart(options.checkpoint);
  const random = seededRandom(options.trainerSeed);
  const online = new DenseQNetwork(
    DQN_FEATURE_SIZE,
    warmStart.hiddenOne,
    warmStart.hiddenTwo,
    random,
  );
  online.restore(warmStart);
  const target = new DenseQNetwork(
    DQN_FEATURE_SIZE,
    warmStart.hiddenOne,
    warmStart.hiddenTwo,
    () => 0.5,
  );
  target.copyFrom(online);
  const replay = new PrioritizedReplay(options.replayCapacity);
  const demoReplay = new PrioritizedReplay(
    Math.max(1, options.demoGames * options.demoMaxMoves),
  );
  const trainingSeeds = consecutiveSeeds(
    V2_TRAINING_SEED_START,
    options.trainingGames,
  );
  const probeSeeds = consecutiveSeeds(V2_PROBE_SEED_START, options.probeGames);

  process.stdout.write(
    `DQN-v2 · ${options.nStep}-step Double-DQN + prioritized replay · train ${seedRange(trainingSeeds)} · probe ${seedRange(probeSeeds)} · final ${formatSeed(RESERVED_FINAL_SEED_START)}+ untouched\n`,
  );
  process.stdout.write(
    `warm start ${resolve(options.checkpoint)} · online replay ${formatBytes(replay.byteLength())} · demo fraction ${(options.demoFraction * 100).toFixed(0)}%\n`,
  );

  if (options.demoGames > 0 && options.demoFraction > 0) {
    await collectOracleDemonstrations(demoReplay, options);
    process.stdout.write(
      `privileged demonstrations ${demoReplay.size.toLocaleString("en-US")} transitions · seeds ${formatSeed(V2_DEMO_SEED_START)}+ · depth ${options.oracleDepth}/beam ${options.oracleBeam}\n`,
    );
  }

  const baseline = evaluateNetwork(
    online,
    probeSeeds,
    options.policySamples,
    options.policySeed,
    options.maxMoves,
  );
  process.stdout.write(`warm-start probe · ${formatSummary(baseline)}\n`);

  let bestSnapshot = online.snapshot();
  let bestProbe = baseline;
  const curves: CurvePoint[] = [];
  let environmentSteps = 0;
  let episodes = 0;
  let updates = 0;
  let lossSum = 0;
  let tdErrorSum = 0;
  let lossCount = 0;
  let nextEvaluation = options.evaluateEvery;
  const shuffledSeeds = [...trainingSeeds];
  shuffle(shuffledSeeds, random);

  while (environmentSteps < options.steps) {
    if (episodes > 0 && episodes % shuffledSeeds.length === 0) {
      shuffle(shuffledSeeds, random);
    }
    const seed = shuffledSeeds[episodes % shuffledSeeds.length];
    let state = initialDqnState(seed);
    const pending: RawStep[] = [];

    while (!state.gameOver && state.movesPlayed < options.maxMoves) {
      const epsilon = annealedEpsilon(environmentSteps, options);
      const action = chooseAction(
        state,
        online,
        options.policySamples,
        options.policySeed,
        random,
        epsilon,
      );
      if (action === null) throw new Error("DQN-v2 found no legal move");
      const moved = playActualDqnMove(state, action, seed);
      if (!moved) throw new Error(`DQN-v2 chose illegal column ${action}`);
      const capped = moved.state.movesPlayed >= options.maxMoves;
      pending.push({
        state,
        action,
        baseReward: shapedReward(moved, moved.state.gameOver),
        move: moved,
        nextState: moved.state,
        done: moved.state.gameOver || capped,
      });
      if (pending.length >= options.nStep) {
        replay.add(buildNstepExperience(pending.slice(0, options.nStep), options.gamma));
        pending.shift();
      }
      state = moved.state;
      environmentSteps += 1;

      if (
        replay.size >= Math.max(options.warmup, options.batchSize) &&
        environmentSteps % options.trainEvery === 0
      ) {
        const batch: TrainingSample[] = [];
        const sampled: Array<{
          replay: PrioritizedReplay;
          index: number;
          input: Float64Array;
          target: number;
        }> = [];
        for (let index = 0; index < options.batchSize; index += 1) {
          const source =
            demoReplay.size > 0 && random() < options.demoFraction
              ? demoReplay
              : replay;
          const item = source.sample(random);
          const experience = item.experience;
          const currentState = expandDqnState(experience.state);
          const input = actionInput(
            currentState,
            experience.action,
            options.policySamples,
            options.policySeed,
          );
          let targetValue = experience.reward;
          if (!experience.done && experience.bootstrapDiscount > 0) {
            const nextState = expandDqnState(experience.nextState);
            const nextAction = chooseAction(
              nextState,
              online,
              options.policySamples,
              options.policySeed,
            );
            if (nextAction !== null) {
              targetValue +=
                experience.bootstrapDiscount *
                target.value(
                  actionInput(
                    nextState,
                    nextAction,
                    options.policySamples,
                    options.policySeed,
                  ),
                );
            }
          }
          batch.push({ input, target: targetValue });
          sampled.push({ replay: source, index: item.index, input, target: targetValue });
        }
        lossSum += online.trainBatch(batch, options.learningRate);
        for (const item of sampled) {
          const error = Math.abs(online.value(item.input) - item.target);
          tdErrorSum += error;
          item.replay.updatePriority(item.index, error);
        }
        lossCount += 1;
        updates += 1;
        if (updates % options.targetEvery === 0) target.copyFrom(online);
      }

      if (state.gameOver || capped || environmentSteps >= options.steps) {
        while (pending.length > 0) {
          replay.add(buildNstepExperience([...pending], options.gamma));
          pending.shift();
        }
      }

      if (environmentSteps >= nextEvaluation || environmentSteps >= options.steps) {
        const probe = evaluateNetwork(
          online,
          probeSeeds,
          options.policySamples,
          options.policySeed,
          options.maxMoves,
        );
        const curve: CurvePoint = {
          step: environmentSteps,
          episodes,
          epsilon,
          meanTdLoss: lossCount === 0 ? 0 : lossSum / lossCount,
          meanAbsoluteTdError:
            lossCount === 0 ? 0 : tdErrorSum / (lossCount * options.batchSize),
          onlineReplaySize: replay.size,
          probe: omitResults(probe),
        };
        curves.push(curve);
        process.stdout.write(
          `step ${formatInteger(environmentSteps)} · ε ${epsilon.toFixed(3)} · loss ${curve.meanTdLoss.toFixed(4)} · |TD| ${curve.meanAbsoluteTdError.toFixed(3)} · probe ${formatSummary(probe)}\n`,
        );
        if (compareSummary(probe, bestProbe) > 0) {
          bestProbe = probe;
          bestSnapshot = online.snapshot();
        }
        lossSum = 0;
        tdErrorSum = 0;
        lossCount = 0;
        while (nextEvaluation <= environmentSteps) {
          nextEvaluation += options.evaluateEvery;
        }
      }
      if (state.gameOver || capped || environmentSteps >= options.steps) break;
    }
    episodes += 1;
  }

  const materiallyBetter =
    bestProbe.meanMoves >= baseline.meanMoves * 1.1 &&
    bestProbe.meanScore >= baseline.meanScore * 1.1;
  const reachesValidationGate = bestProbe.meanScore >= 400_000;
  if (materiallyBetter) {
    await writeCheckpoint(options.outputPath, {
      format: "drop7-observable-double-dqn",
      version: 1,
      algorithm: "double-dqn",
      observableOnly: true,
      trainingOnly: true,
      validationUsed: false,
      v2: {
        nStep: options.nStep,
        prioritizedReplayAlpha: PRIORITY_ALPHA,
        demoFraction: options.demoFraction,
        demoGames: options.demoGames,
        reward: windowRewardDescription(),
      },
      options: {
        policySamples: options.policySamples,
        policySeed: options.policySeed,
      },
      trainingSeedStart: V2_TRAINING_SEED_START,
      probeSeedStart: V2_PROBE_SEED_START,
      reservedFinalSeedStart: RESERVED_FINAL_SEED_START,
      network: bestSnapshot,
      warmStartProbe: omitResults(baseline),
      bestProbe: omitResults(bestProbe),
      curves,
    });
    process.stdout.write(`training-only checkpoint ${resolve(options.outputPath)}\n`);
  } else {
    process.stdout.write("checkpoint withheld: v2 did not improve probe score and moves by 10%\n");
  }
  process.stdout.write(
    `validation gate ${reachesValidationGate ? "MET" : "NOT MET"} · best probe ${formatSummary(bestProbe)} · target ≥400,000\n`,
  );
  return { baseline, bestProbe, curves, materiallyBetter, reachesValidationGate };
}

async function collectOracleDemonstrations(
  replay: PrioritizedReplay,
  options: Arguments,
) {
  for (let game = 0; game < options.demoGames; game += 1) {
    const seed = (V2_DEMO_SEED_START + game) >>> 0;
    let state = initialDqnState(seed);
    const pending: RawStep[] = [];
    while (!state.gameOver && state.movesPlayed < options.demoMaxMoves) {
      const plan = planOracleMove(
        state,
        seed,
        options.oracleDepth,
        options.oracleBeam,
      );
      if (plan.column === null) throw new Error("Oracle demonstration has no move");
      const moved = playActualDqnMove(state, plan.column, seed);
      if (!moved) throw new Error("Oracle demonstration chose an illegal move");
      const capped = moved.state.movesPlayed >= options.demoMaxMoves;
      pending.push({
        state,
        action: plan.column,
        baseReward: shapedReward(moved, moved.state.gameOver),
        move: moved,
        nextState: moved.state,
        done: moved.state.gameOver || capped,
      });
      if (pending.length >= options.nStep) {
        replay.add(buildNstepExperience(pending.slice(0, options.nStep), options.gamma), 2);
        pending.shift();
      }
      state = moved.state;
      if (state.gameOver || capped) {
        while (pending.length > 0) {
          replay.add(buildNstepExperience([...pending], options.gamma), 2);
          pending.shift();
        }
      }
    }
    process.stdout.write(
      `demo ${game + 1}/${options.demoGames} · ${formatSeed(seed)} · ${state.movesPlayed} moves · ${formatInteger(state.score)}\n`,
    );
  }
}

function buildNstepExperience(steps: readonly RawStep[], gamma: number) {
  if (steps.length === 0) throw new Error("Cannot build an empty n-step return");
  let reward = 0;
  for (let index = 0; index < steps.length; index += 1) {
    reward += gamma ** index * steps[index].baseReward;
  }
  reward += gamma ** (steps.length - 1) * windowReward(steps);
  const final = steps.at(-1)!;
  return {
    state: compactDqnState(steps[0].state),
    action: steps[0].action,
    reward,
    nextState: compactDqnState(final.nextState),
    bootstrapDiscount: final.done ? 0 : gamma ** steps.length,
    done: final.done,
  };
}

/** Every dense window term is clipped below one move of survival reward. */
function windowReward(steps: readonly RawStep[]) {
  const before = boardMetrics(steps[0].state);
  const after = boardMetrics(steps.at(-1)!.nextState);
  const solidDrift = clip((before.solids - after.solids) * 0.08, -0.35, 0.35);
  const occupancyDrift = clip(
    (before.occupied - after.occupied) * 0.035,
    -0.3,
    0.3,
  );
  const lowCapDrift = clip(
    (before.lowCapLoad - after.lowCapLoad) * 0.008 +
      (before.adjacentLowCapLoad - after.adjacentLowCapLoad) * 0.006,
    -0.3,
    0.3,
  );
  let quietGain = 0;
  let triggerSpend = 0;
  let triggered = false;
  for (const step of steps) {
    const start = boardMetrics(step.state);
    const end = boardMetrics(step.nextState);
    const delta = end.directPotential - start.directPotential;
    if (step.move.waves.length === 0) {
      quietGain += Math.max(0, delta);
    } else {
      triggered = true;
      triggerSpend += Math.max(0, -delta);
      triggerSpend += step.move.waves.reduce(
        (sum, wave) => sum + wave.cleared * 0.15 + wave.revealed * 0.2,
        0,
      );
    }
  }
  const buildThenRelease = triggered
    ? clip(Math.min(quietGain, triggerSpend) * 0.1, 0, 0.3)
    : 0;
  return clip(
    solidDrift + occupancyDrift + lowCapDrift + buildThenRelease,
    -0.75,
    0.75,
  );
}

function boardMetrics(state: GameState): BoardMetrics {
  const heuristic = extractHeuristicFeatures(state);
  const heights = columnHeights(state);
  let lowCapLoad = 0;
  let adjacentLowCapLoad = 0;
  const lowCaps = Array<boolean>(BOARD_SIZE).fill(false);
  for (let column = 0; column < BOARD_SIZE; column += 1) {
    const height = heights[column];
    if (height === 0) continue;
    const cap = state.board[(BOARD_SIZE - height) * BOARD_SIZE + column];
    if (cap !== 1 && cap !== 2) continue;
    lowCaps[column] = true;
    lowCapLoad += height ** 2 * (cap === 1 ? 1.5 : 1);
    if (column > 0 && lowCaps[column - 1]) {
      adjacentLowCapLoad += Math.min(heights[column - 1], height) ** 2;
    }
  }
  return {
    occupied:
      heuristic.solidCells + heuristic.crackedCells + heuristic.numberedCells,
    solids: heuristic.solidCells,
    lowCapLoad,
    adjacentLowCapLoad,
    directPotential: heuristic.directPotential,
  };
}

function evaluateNetwork(
  network: DenseQNetwork,
  seeds: readonly number[],
  samples: number,
  policySeed: number,
  maxMoves: number,
) {
  return summarize(
    seeds.map((seed): GameResult => {
      let state = initialDqnState(seed);
      let maxChain = 0;
      while (!state.gameOver && state.movesPlayed < maxMoves) {
        const action = chooseAction(state, network, samples, policySeed);
        if (action === null) throw new Error("DQN-v2 evaluation found no move");
        const moved = playActualDqnMove(state, action, seed);
        if (!moved) throw new Error("DQN-v2 evaluation chose an illegal move");
        maxChain = Math.max(maxChain, moved.waves.length);
        state = moved.state;
      }
      return {
        seed,
        score: state.score,
        moves: state.movesPlayed,
        censored: !state.gameOver,
        maxChain,
      };
    }),
  );
}

function summarize(results: readonly GameResult[]): Summary {
  const scores = results.map((result) => result.score).sort(numberOrder);
  return {
    games: results.length,
    meanScore: mean(scores),
    medianScore: median(scores),
    minimumScore: scores[0],
    maximumScore: scores.at(-1)!,
    meanMoves: mean(results.map((result) => result.moves)),
    censoredGames: results.filter((result) => result.censored).length,
    meanMaxChain: mean(results.map((result) => result.maxChain)),
    results,
  };
}

async function readWarmStart(path: string): Promise<NetworkSnapshot> {
  const parsed = JSON.parse(await readFile(path, "utf8")) as unknown;
  compileDqnCheckpoint(parsed, { cacheEntries: 0 });
  const artifact = parsed as { network: NetworkSnapshot };
  if (artifact.network.inputSize !== DQN_FEATURE_SIZE) {
    throw new Error("Warm-start feature size does not match DQN-v2");
  }
  return artifact.network;
}

function createSumTree(capacity: number): SumTree {
  let leafCount = 1;
  while (leafCount < capacity) leafCount *= 2;
  return { leafCount, values: new Float64Array(leafCount * 2) };
}

function updateSumTree(tree: SumTree, index: number, value: number) {
  let cursor = tree.leafCount + index;
  const difference = value - tree.values[cursor];
  while (cursor >= 1) {
    tree.values[cursor] += difference;
    cursor = Math.floor(cursor / 2);
  }
}

function sampleSumTree(
  tree: SumTree,
  target: number,
  populatedSize: number,
) {
  let cursor = 1;
  while (cursor < tree.leafCount) {
    const left = cursor * 2;
    if (target < tree.values[left]) {
      cursor = left;
    } else {
      target -= tree.values[left];
      cursor = left + 1;
    }
  }
  return Math.min(populatedSize - 1, cursor - tree.leafCount);
}

function writeCompactState(
  boards: Uint8Array,
  metadata: Uint16Array,
  index: number,
  state: CompactState,
) {
  boards.set(state.board, index * BOARD_CELLS);
  const offset = index * 4;
  metadata[offset] = state.nextDisc;
  metadata[offset + 1] = state.level;
  metadata[offset + 2] = state.movesRemaining;
  metadata[offset + 3] = state.movesPlayed;
}

function readCompactState(
  boards: Uint8Array,
  metadata: Uint16Array,
  index: number,
): CompactState {
  const boardOffset = index * BOARD_CELLS;
  const metadataOffset = index * 4;
  return {
    board: Array.from(
      boards.subarray(boardOffset, boardOffset + BOARD_CELLS),
      (cell) => cell as GameState["board"][number],
    ),
    nextDisc: metadata[metadataOffset] as CompactState["nextDisc"],
    level: metadata[metadataOffset + 1],
    movesRemaining: metadata[metadataOffset + 2],
    movesPlayed: metadata[metadataOffset + 3],
    gameOver: false,
  };
}

function columnHeights(state: GameState) {
  const heights = Array<number>(BOARD_SIZE).fill(0);
  for (let index = 0; index < state.board.length; index += 1) {
    if (state.board[index] !== EMPTY) heights[index % BOARD_SIZE] += 1;
  }
  return heights;
}

function compareSummary(first: Summary, second: Summary) {
  const moveDelta = first.meanMoves - second.meanMoves;
  return Math.abs(moveDelta) > 0.25
    ? moveDelta
    : first.meanScore - second.meanScore;
}

function omitResults(summary: Summary): Omit<Summary, "results"> {
  const { results, ...rest } = summary;
  void results;
  return rest;
}

function annealedEpsilon(step: number, options: Arguments) {
  const annealSteps = Math.max(1, options.steps * options.epsilonFraction);
  const fraction = Math.min(1, step / annealSteps);
  return (
    options.epsilonStart +
    (options.epsilonEnd - options.epsilonStart) * fraction
  );
}

function windowRewardDescription() {
  return {
    survivalPerMove: 1,
    terminalPenalty: TERMINAL_PENALTY,
    fiveMoveSolidDrift: 0.08,
    fiveMoveOccupancyDrift: 0.035,
    fiveMoveLowCapLoadDrift: 0.008,
    fiveMoveAdjacentLowCapDrift: 0.006,
    quietBuildThenRelease: 0.1,
    windowClip: [-0.75, 0.75],
  };
}

async function writeCheckpoint(path: string, artifact: unknown) {
  const absolute = resolve(path);
  await mkdir(dirname(absolute), { recursive: true });
  const temporary = `${absolute}.tmp-${process.pid}`;
  await writeFile(temporary, `${JSON.stringify(artifact, null, 2)}\n`, "utf8");
  await rename(temporary, absolute);
}

function consecutiveSeeds(start: number, count: number) {
  if (!Number.isSafeInteger(count) || count < 1 || count > 10_000) {
    throw new Error("Seed count must be from 1 to 10,000");
  }
  if (start + count >= RESERVED_FINAL_SEED_START) {
    throw new Error("Seed range overlaps reserved final seeds");
  }
  return Array.from({ length: count }, (_, index) => (start + index) >>> 0);
}

function shuffle<T>(items: T[], random: () => number) {
  for (let index = items.length - 1; index > 0; index -= 1) {
    const other = Math.floor(random() * (index + 1));
    [items[index], items[other]] = [items[other], items[index]];
  }
}

function clip(value: number, minimum: number, maximum: number) {
  return Math.max(minimum, Math.min(maximum, value));
}

function mean(values: readonly number[]) {
  return values.reduce((sum, value) => sum + value, 0) / values.length;
}

function median(values: readonly number[]) {
  const sorted = [...values].sort(numberOrder);
  const middle = Math.floor(sorted.length / 2);
  return sorted.length % 2 === 0
    ? (sorted[middle - 1] + sorted[middle]) / 2
    : sorted[middle];
}

function numberOrder(first: number, second: number) {
  return first - second;
}

function formatInteger(value: number) {
  return Math.round(value).toLocaleString("en-US");
}

function formatSummary(summary: Summary) {
  return `mean ${formatInteger(summary.meanScore)} · median ${formatInteger(summary.medianScore)} · moves ${summary.meanMoves.toFixed(1)} · capped ${summary.censoredGames}/${summary.games} · chain ${summary.meanMaxChain.toFixed(2)}`;
}

function formatSeed(seed: number) {
  return `0x${seed.toString(16).padStart(8, "0")}`;
}

function seedRange(seeds: readonly number[]) {
  return `${formatSeed(seeds[0])}..${formatSeed(seeds.at(-1)!)}`;
}

function formatBytes(bytes: number) {
  return `${(bytes / 1024 / 1024).toFixed(2)} MiB`;
}

export function parseArguments(arguments_: readonly string[]): Arguments {
  const options: Arguments = {
    checkpoint: DEFAULT_CHECKPOINT,
    outputPath: DEFAULT_OUTPUT,
    steps: DEFAULT_STEPS,
    trainingGames: DEFAULT_TRAINING_GAMES,
    probeGames: DEFAULT_PROBE_GAMES,
    maxMoves: DEFAULT_MAX_MOVES,
    replayCapacity: DEFAULT_REPLAY_CAPACITY,
    batchSize: DEFAULT_BATCH_SIZE,
    warmup: DEFAULT_WARMUP,
    trainEvery: DEFAULT_TRAIN_EVERY,
    targetEvery: DEFAULT_TARGET_EVERY,
    evaluateEvery: DEFAULT_EVALUATE_EVERY,
    learningRate: DEFAULT_LEARNING_RATE,
    gamma: DEFAULT_GAMMA,
    nStep: DEFAULT_N_STEP,
    epsilonStart: DEFAULT_EPSILON_START,
    epsilonEnd: DEFAULT_EPSILON_END,
    epsilonFraction: DEFAULT_EPSILON_FRACTION,
    demoGames: DEFAULT_DEMO_GAMES,
    demoMaxMoves: DEFAULT_DEMO_MAX_MOVES,
    demoFraction: DEFAULT_DEMO_FRACTION,
    oracleDepth: DEFAULT_ORACLE_DEPTH,
    oracleBeam: DEFAULT_ORACLE_BEAM,
    policySamples: DEFAULT_POLICY_SAMPLES,
    trainerSeed: DEFAULT_TRAINER_SEED,
    policySeed: DEFAULT_POLICY_SEED,
    selfTest: false,
  };
  const numeric = new Map<string, keyof Arguments>([
    ["--steps", "steps"],
    ["--training-games", "trainingGames"],
    ["--probe-games", "probeGames"],
    ["--max-moves", "maxMoves"],
    ["--replay-capacity", "replayCapacity"],
    ["--batch-size", "batchSize"],
    ["--warmup", "warmup"],
    ["--train-every", "trainEvery"],
    ["--target-every", "targetEvery"],
    ["--evaluate-every", "evaluateEvery"],
    ["--learning-rate", "learningRate"],
    ["--gamma", "gamma"],
    ["--n-step", "nStep"],
    ["--epsilon-start", "epsilonStart"],
    ["--epsilon-end", "epsilonEnd"],
    ["--epsilon-fraction", "epsilonFraction"],
    ["--demo-games", "demoGames"],
    ["--demo-max-moves", "demoMaxMoves"],
    ["--demo-fraction", "demoFraction"],
    ["--oracle-depth", "oracleDepth"],
    ["--oracle-beam", "oracleBeam"],
    ["--samples", "policySamples"],
    ["--trainer-seed", "trainerSeed"],
    ["--policy-seed", "policySeed"],
  ]);
  for (let index = 0; index < arguments_.length; index += 1) {
    const flag = arguments_[index];
    if (flag === "--self-test") {
      options.selfTest = true;
      continue;
    }
    if (flag === "--checkpoint" || flag === "--output") {
      const value = arguments_[++index];
      if (!value) throw new Error(`${flag} needs a value`);
      if (flag === "--checkpoint") options.checkpoint = value;
      else options.outputPath = value;
      continue;
    }
    const key = numeric.get(flag);
    if (!key) throw new Error(`Unknown argument ${flag}`);
    const value = Number(arguments_[++index]);
    (options as unknown as Record<string, number>)[key] = value;
  }
  validateArguments(options);
  return options;
}

function validateArguments(options: Arguments) {
  for (const key of [
    "steps",
    "trainingGames",
    "probeGames",
    "maxMoves",
    "replayCapacity",
    "batchSize",
    "warmup",
    "trainEvery",
    "targetEvery",
    "evaluateEvery",
    "nStep",
    "demoMaxMoves",
    "oracleDepth",
    "oracleBeam",
    "policySamples",
  ] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 1) {
      throw new Error(`${key} must be a positive integer`);
    }
  }
  if (!Number.isSafeInteger(options.demoGames) || options.demoGames < 0) {
    throw new Error("demoGames must be a non-negative integer");
  }
  for (const key of ["trainerSeed", "policySeed"] as const) {
    if (!Number.isSafeInteger(options[key]) || options[key] < 0 || options[key] > 0xffff_ffff) {
      throw new Error(`${key} must be a uint32 integer`);
    }
    options[key] >>>= 0;
  }
  for (const key of ["learningRate", "gamma", "epsilonStart", "epsilonEnd", "epsilonFraction", "demoFraction"] as const) {
    if (!Number.isFinite(options[key])) throw new Error(`${key} must be finite`);
  }
  if (options.learningRate <= 0) throw new Error("learningRate must be positive");
  if (options.gamma < 0 || options.gamma > 1) throw new Error("gamma must be in [0,1]");
  if (options.epsilonStart < 0 || options.epsilonStart > 1 || options.epsilonEnd < 0 || options.epsilonEnd > 1) {
    throw new Error("epsilon values must be in [0,1]");
  }
  if (options.epsilonFraction <= 0 || options.epsilonFraction > 1) {
    throw new Error("epsilonFraction must be in (0,1]");
  }
  if (options.demoFraction < 0 || options.demoFraction > 1) {
    throw new Error("demoFraction must be in [0,1]");
  }
  if (options.batchSize > options.replayCapacity) {
    throw new Error("batchSize cannot exceed replayCapacity");
  }
}

export function runSelfTest() {
  const state = initialDqnState(42);
  const moved = playActualDqnMove(state, 3, 42);
  if (!moved) throw new Error("Could not create v2 test transition");
  const raw: RawStep = {
    state,
    action: 3,
    baseReward: shapedReward(moved, moved.state.gameOver),
    move: moved,
    nextState: moved.state,
    done: false,
  };
  const experience = buildNstepExperience([raw], 0.99);
  if (!Number.isFinite(experience.reward) || experience.bootstrapDiscount !== 0.99) {
    throw new Error("n-step construction failed");
  }
  const replay = new PrioritizedReplay(4);
  replay.add(experience);
  const sampled = replay.sample(() => 0.5);
  if (sampled.experience.action !== 3) throw new Error("prioritized replay failed");
  replay.updatePriority(sampled.index, 2);
  const reward = windowReward([raw]);
  if (!Number.isFinite(reward) || Math.abs(reward) > 0.75) {
    throw new Error("window reward is not bounded");
  }
  process.stdout.write("drop7 DQN-v2 self-test passed\n");
}

export async function runCli(arguments_: readonly string[]) {
  const options = parseArguments(arguments_);
  if (options.selfTest) {
    runSelfTest();
    return;
  }
  await trainDqnV2(options);
}

if (
  process.argv[1] &&
  import.meta.url === pathToFileURL(process.argv[1]).href
) {
  await runCli(process.argv.slice(2));
}
