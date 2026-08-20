#!/usr/bin/env python3
"""Deterministic public-state behavior cloning and PPO for Hardcore Drop7.

This offline runner builds the native environment directly with Clang, trains
a compact reflection-ensembled convolutional policy, and exports both
TorchScript and a runtime-neutral Float32 manifest for deployment.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import resource
import shlex
import subprocess
import sys
import sysconfig
import time
from dataclasses import dataclass
from typing import Any, Callable, Iterable, Literal, NamedTuple

import numpy as np
import pybind11
import torch
from torch import Tensor, nn
import torch.nn.functional as F


ROOT = Path(__file__).resolve().parents[3]
ENV_SOURCE = ROOT / "approaches/ntuple-rl/torch-ppo/torch-env.cpp"
ENGINE_HEADER = ROOT / "src/core/native/engine.hpp"
CFPI_HEADER = ROOT / "src/core/native/public-behavior.hpp"

# Fixed seed partitions.  The C++ boundary independently rejects anything
# outside 0x3d30_0000..0x3d3f_ffff.  No CLI option can widen these ranges.
SELF_TEST_SEED_START = 0x3D30_0040
SELF_TEST_GAMES = 4
CLONE_TRAIN_SEED_START = 0x3D31_0000
CLONE_TRAIN_GAMES = 768
CLONE_VALIDATION_SEED_START = 0x3D32_0000
CLONE_VALIDATION_GAMES = 256
DAGGER_SEED_START = 0x3D33_0000
DAGGER_GAMES = 512
PPO_PILOT_SEED_START = 0x3D34_0000
PPO_PILOT_ITERATIONS = 8
PPO_EPISODES_PER_ITERATION = 512
PPO_CONTINUATION_SEED_START = 0x3D35_0000
PPO_CONTINUATION_STAGES = (8, 16)
DIRECT_PPO_DEVELOPMENT_SEED_START = 0x3D36_0000
DIRECT_PPO_DEVELOPMENT_GAMES = 64
DEVELOPMENT_SEED_START = 0x3D37_0000
DEVELOPMENT_GAMES = 32
CONFIRMATION_SEED_START = 0x3D38_0000
CONFIRMATION_GAMES = 64
FRESH_DIRECT_PPO_TRAINING_SEED_START = 0x3D39_0000
FRESH_DIRECT_PPO_TRAINING_GAMES = 16_384
FRESH_DIRECT_PPO_DEVELOPMENT_SEED_START = 0x3D3A_0000
FRESH_DIRECT_PPO_DEVELOPMENT_GAMES = 64
MAXIMUM_MOVES = 1_000

# Fixed public teacher and optimization protocol.
TEACHER_DEPTH = 2
TEACHER_THREADS = 4
CLONE_EPOCHS = 12
DAGGER_EPOCHS = 6
CORRECTED_HARD_EPOCHS = 16
CLONE_BATCH_SIZE = 512
CLONE_LEARNING_RATE = 3.0e-4
DAGGER_LEARNING_RATE = 1.0e-4
PPO_EPOCHS = 4
PPO_MINIBATCH = 1_024
PPO_LEARNING_RATE = 1.0e-4
GAMMA = 0.997
GAE_LAMBDA = 0.95
CLIP_RATIO = 0.15
ENTROPY_COEFFICIENT = 0.005
VALUE_COEFFICIENT = 0.25
IMITATION_ANCHOR_COEFFICIENT = 0.03
MAX_GRADIENT_NORM = 0.5
SURVIVAL_REWARD = 0.02
CLEAR_REWARD = 0.005
REVEAL_REWARD = 0.005
TERMINAL_PENALTY = -1.0
NETWORK_SEED = 0x5EED_C0DE
POLICY_SAMPLE_SEED = 0x51A7_2026
RANDOM_POLICY_SEED = 0x4A17_2026
TORCH_THREADS = 4
EXPECTED_LEVEL_BONUS = 17_000
MAXIMUM_PARAMETERS = 300_000
MAXIMUM_RSS_BYTES = 512 * 1024 * 1024

# Fixed pre-PPO and continuation gates.  Ratios must pass for both score and
# survival; teacher agreement is measured on whole-game-disjoint D2 states.
MINIMUM_D2_VALIDATION_AGREEMENT = 0.55
WARM_RANDOM_RATIO = 1.05
WARM_D1_RATIO = 0.65
PILOT_RANDOM_RATIO = 1.10
PILOT_D1_RATIO = 0.75
PILOT_D2_RATIO = 0.55
PILOT_CLONE_RETENTION = 0.98
CONTINUATION_RETENTION = 0.95
TARGET_SCORE = 1_000_000.0
DIRECT_PPO_ITERATIONS = PPO_PILOT_ITERATIONS + sum(PPO_CONTINUATION_STAGES)
DIRECT_PPO_CLONE_RATIO = 1.15
DIRECT_PPO_D2_RATIO = 1.00
DIRECT_PPO_PAIRED_T_CRITICAL = 1.6694022215079607
DIRECT_PPO_WALL_LIMIT_SECONDS = 90 * 60
FRESH_PROCESS_PREFLIGHT_MAXIMUM_RSS_BYTES = 480 * 1024 * 1024
GRADACCUM_PHYSICAL_MINIBATCH = 256
GRADACCUM_PREFLIGHT_MAXIMUM_RSS_BYTES = 400 * 1024 * 1024
ORIGINAL_CLONE_SHA256 = (
    "20c2dd906b6e7d58bac2da64b1267a8032222d937cdd02fe98aaeaedd339ecf8"
)

CLONE_CHECKPOINT = Path("/tmp/drop7-torch-d2-clone.pt")
CORRECTED_CLONE_CHECKPOINT = Path("/tmp/drop7-torch-d2-clone-corrected.pt")
PILOT_CHECKPOINT = Path("/tmp/drop7-torch-ppo-pilot.pt")
CONTINUATION_CHECKPOINT = Path("/tmp/drop7-torch-ppo-continuation.pt")
ARTIFACT_PATH = Path("/tmp/drop7-torch-rl-pilot.json")
CORRECTED_ARTIFACT_PATH = Path("/tmp/drop7-torch-rl-corrected.json")
DIRECT_PPO_CHECKPOINT = Path("/tmp/drop7-torch-direct-ppo.pt")
DIRECT_PPO_PARTIAL_CHECKPOINT = Path("/tmp/drop7-torch-direct-ppo-partial.pt")
DIRECT_PPO_ARTIFACT_PATH = Path("/tmp/drop7-torch-direct-ppo.json")
DIRECT_PPO_EXPORT_MANIFEST = Path("/tmp/drop7-torch-direct-ppo-policy.json")
DIRECT_PPO_EXPORT_WEIGHTS = Path("/tmp/drop7-torch-direct-ppo-policy.f32")
DIRECT_PPO_EXPORT_GOLDEN = Path("/tmp/drop7-torch-direct-ppo-golden.json")
DIRECT_PPO_EXPORT_TORCHSCRIPT = Path("/tmp/drop7-torch-direct-ppo-policy.ts")
SELF_TEST_ARTIFACT_PATH = Path("/tmp/drop7-torch-selftest-passed.json")
FRESH_PROCESS_PREFLIGHT_ARTIFACT_PATH = Path(
    "/tmp/drop7-torch-fresh-process-preflight.json"
)
FRESH_DIRECT_PPO_CHECKPOINT = Path("/tmp/drop7-torch-fresh-process-ppo.pt")
FRESH_DIRECT_PPO_PARTIAL_CHECKPOINT = Path(
    "/tmp/drop7-torch-fresh-process-ppo-partial.pt"
)
FRESH_DIRECT_PPO_ARTIFACT_PATH = Path("/tmp/drop7-torch-fresh-process-ppo.json")
FRESH_DIRECT_PPO_EXPORT_MANIFEST = Path(
    "/tmp/drop7-torch-fresh-process-policy.json"
)
FRESH_DIRECT_PPO_EXPORT_WEIGHTS = Path(
    "/tmp/drop7-torch-fresh-process-policy.f32"
)
FRESH_DIRECT_PPO_EXPORT_GOLDEN = Path(
    "/tmp/drop7-torch-fresh-process-golden.json"
)
FRESH_DIRECT_PPO_EXPORT_TORCHSCRIPT = Path(
    "/tmp/drop7-torch-fresh-process-policy.ts"
)
GRADACCUM_PREFLIGHT_ARTIFACT_PATH = Path(
    "/tmp/drop7-torch-gradaccum256-preflight.json"
)
GRADACCUM_PPO_CHECKPOINT = Path("/tmp/drop7-torch-gradaccum256-ppo.pt")
GRADACCUM_PPO_PARTIAL_CHECKPOINT = Path(
    "/tmp/drop7-torch-gradaccum256-ppo-partial.pt"
)
GRADACCUM_PPO_ARTIFACT_PATH = Path("/tmp/drop7-torch-gradaccum256-ppo.json")
GRADACCUM_EXPORT_MANIFEST = Path("/tmp/drop7-torch-gradaccum256-policy.json")
GRADACCUM_EXPORT_WEIGHTS = Path("/tmp/drop7-torch-gradaccum256-policy.f32")
GRADACCUM_EXPORT_GOLDEN = Path("/tmp/drop7-torch-gradaccum256-golden.json")
GRADACCUM_EXPORT_TORCHSCRIPT = Path("/tmp/drop7-torch-gradaccum256-policy.ts")
EXPORT_MANIFEST = Path("/tmp/drop7-torch-public-policy.json")
EXPORT_WEIGHTS = Path("/tmp/drop7-torch-public-policy.f32")
EXPORT_GOLDEN = Path("/tmp/drop7-torch-public-policy-golden.json")
EXPORT_TORCHSCRIPT = Path("/tmp/drop7-torch-public-policy.ts")

assert CLONE_TRAIN_SEED_START + CLONE_TRAIN_GAMES <= CLONE_VALIDATION_SEED_START
assert (
    CLONE_VALIDATION_SEED_START + CLONE_VALIDATION_GAMES <= DAGGER_SEED_START
)
assert DAGGER_SEED_START + DAGGER_GAMES <= PPO_PILOT_SEED_START
assert (
    PPO_PILOT_SEED_START
    + PPO_PILOT_ITERATIONS * PPO_EPISODES_PER_ITERATION
    <= PPO_CONTINUATION_SEED_START
)
assert (
    PPO_CONTINUATION_SEED_START
    + sum(PPO_CONTINUATION_STAGES) * PPO_EPISODES_PER_ITERATION
    <= DIRECT_PPO_DEVELOPMENT_SEED_START
)
assert (
    DIRECT_PPO_DEVELOPMENT_SEED_START + DIRECT_PPO_DEVELOPMENT_GAMES
    <= DEVELOPMENT_SEED_START
)
assert DEVELOPMENT_SEED_START + DEVELOPMENT_GAMES <= CONFIRMATION_SEED_START
assert (
    CONFIRMATION_SEED_START + CONFIRMATION_GAMES
    <= FRESH_DIRECT_PPO_TRAINING_SEED_START
)
assert (
    FRESH_DIRECT_PPO_TRAINING_SEED_START + FRESH_DIRECT_PPO_TRAINING_GAMES
    <= FRESH_DIRECT_PPO_DEVELOPMENT_SEED_START
)
assert (
    FRESH_DIRECT_PPO_DEVELOPMENT_SEED_START
    + FRESH_DIRECT_PPO_DEVELOPMENT_GAMES
    <= 0x3D40_0000
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def state_dict_sha256(state: dict[str, Tensor]) -> str:
    digest = hashlib.sha256()
    for name in sorted(state):
        tensor = state[name].detach().cpu().contiguous()
        digest.update(name.encode("utf-8"))
        digest.update(str(tensor.dtype).encode("ascii"))
        digest.update(json.dumps(list(tensor.shape)).encode("ascii"))
        digest.update(tensor.numpy().tobytes(order="C"))
    return digest.hexdigest()


def generator_state_sha256(generator: torch.Generator) -> str:
    state = generator.get_state().cpu().contiguous().numpy()
    return hashlib.sha256(state.tobytes(order="C")).hexdigest()


def configure_torch() -> None:
    torch.manual_seed(NETWORK_SEED)
    np.random.seed(NETWORK_SEED & 0xFFFF_FFFF)
    torch.set_num_threads(TORCH_THREADS)
    torch.set_num_interop_threads(1)
    torch.use_deterministic_algorithms(True)


def peak_rss_bytes() -> int:
    value = int(resource.getrusage(resource.RUSAGE_SELF).ru_maxrss)
    return value if sys.platform == "darwin" else value * 1024


class DirectPpoResourceLimit(RuntimeError):
    """Raised before direct PPO exceeds a fixed hard limit."""


def enforce_direct_ppo_resource_limits(deadline: float) -> None:
    if time.perf_counter() >= deadline:
        raise DirectPpoResourceLimit("90-minute direct-PPO wall limit reached")
    resident = peak_rss_bytes()
    if resident >= MAXIMUM_RSS_BYTES:
        raise DirectPpoResourceLimit(
            f"direct-PPO RSS {resident} reached the 512 MiB hard limit"
        )


def build_extension() -> Any:
    """Build a content-addressed strict pybind module without Ninja."""
    digest = hashlib.sha256()
    for path in (ENV_SOURCE, ENGINE_HEADER, CFPI_HEADER):
        digest.update(path.read_bytes())
    digest.update(sys.version.encode())
    name = f"drop7_torch_env_{digest.hexdigest()[:12]}"
    output_dir = Path("/tmp/drop7_torch_extensions")
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"{name}{sysconfig.get_config_var('EXT_SUFFIX')}"
    if not output.exists():
        command = [
            "clang++",
            "-O3",
            "-DNDEBUG",
            "-std=c++20",
            "-shared",
            "-fPIC",
            "-undefined",
            "dynamic_lookup",
            "-pthread",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Werror",
            f"-DTORCH_EXTENSION_NAME={name}",
            "-isystem",
            pybind11.get_include(),
            "-isystem",
            sysconfig.get_paths()["include"],
            str(ENV_SOURCE),
            "-o",
            str(output),
        ]
        print("TORCH_RL_BUILD", shlex.join(command), flush=True)
        subprocess.run(command, check=True)
    specification = importlib.util.spec_from_file_location(name, output)
    if specification is None or specification.loader is None:
        raise RuntimeError("could not construct extension import specification")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    if int(module.level_bonus) != EXPECTED_LEVEL_BONUS:
        raise RuntimeError(
            f"stale/wrong engine level bonus: {module.level_bonus}, "
            f"expected {EXPECTED_LEVEL_BONUS}"
        )
    report = dict(module.self_test())
    if not report.get("passed"):
        raise RuntimeError(f"native extension self-test failed: {report}")
    return module


class ResidualBlock(nn.Module):
    def __init__(self, channels: int) -> None:
        super().__init__()
        self.conv1 = nn.Conv2d(channels, channels, 3, padding=1)
        self.conv2 = nn.Conv2d(channels, channels, 3, padding=1)

    def forward(self, source: Tensor) -> Tensor:
        residual = F.relu(self.conv1(source))
        residual = self.conv2(residual)
        return F.relu(source + residual)


class PublicActorCritic(nn.Module):
    """Compact public-state network with exact horizontal reflection ensemble."""

    __constants__ = [
        "board_categories",
        "disc_categories",
        "phase_categories",
        "channels",
        "hidden",
        "residual_blocks",
    ]
    board_categories: int = 10
    disc_categories: int = 7
    phase_categories: int = 5
    channels: int = 32
    hidden: int = 128
    residual_blocks: int = 3

    def __init__(self) -> None:
        super().__init__()
        input_channels = (
            self.board_categories
            + self.disc_categories
            + self.phase_categories
        )
        self.input_conv = nn.Conv2d(input_channels, self.channels, 3, padding=1)
        self.blocks = nn.ModuleList(
            [ResidualBlock(self.channels) for _ in range(self.residual_blocks)]
        )
        self.dense = nn.Linear(self.channels * 7 * 7, self.hidden)
        self.policy = nn.Linear(self.hidden, 7)
        self.value = nn.Linear(self.hidden, 1)
        self.reset_parameters()

    def reset_parameters(self) -> None:
        nn.init.orthogonal_(self.input_conv.weight, math.sqrt(2.0))
        nn.init.zeros_(self.input_conv.bias)
        for block in self.blocks:
            nn.init.orthogonal_(block.conv1.weight, math.sqrt(2.0))
            nn.init.zeros_(block.conv1.bias)
            nn.init.orthogonal_(block.conv2.weight, math.sqrt(2.0))
            nn.init.zeros_(block.conv2.bias)
        nn.init.orthogonal_(self.dense.weight, math.sqrt(2.0))
        nn.init.zeros_(self.dense.bias)
        nn.init.orthogonal_(self.policy.weight, 0.01)
        nn.init.zeros_(self.policy.bias)
        nn.init.orthogonal_(self.value.weight, 1.0)
        nn.init.zeros_(self.value.bias)

    def encode(self, boards: Tensor, discs: Tensor, phases: Tensor) -> Tensor:
        board = F.one_hot(boards.to(torch.long), self.board_categories)
        board = board.view(-1, 7, 7, self.board_categories).permute(0, 3, 1, 2)
        disc = F.one_hot(discs.to(torch.long) - 1, self.disc_categories)
        disc = disc.to(torch.float32).view(-1, self.disc_categories, 1, 1)
        disc = disc.expand(-1, -1, 7, 7)
        phase = F.one_hot(phases.to(torch.long) - 1, self.phase_categories)
        phase = phase.to(torch.float32).view(-1, self.phase_categories, 1, 1)
        phase = phase.expand(-1, -1, 7, 7)
        return torch.cat((board.to(torch.float32), disc, phase), dim=1)

    def raw(self, encoded: Tensor) -> tuple[Tensor, Tensor]:
        hidden = F.relu(self.input_conv(encoded))
        for block in self.blocks:
            hidden = block(hidden)
        hidden = F.relu(self.dense(hidden.flatten(1)))
        return self.policy(hidden), self.value(hidden).squeeze(-1)

    def forward(
        self, boards: Tensor, discs: Tensor, phases: Tensor, legal_masks: Tensor
    ) -> tuple[Tensor, Tensor]:
        encoded = self.encode(boards, discs, phases)
        direct_logits, direct_value = self.raw(encoded)
        mirror_logits, mirror_value = self.raw(torch.flip(encoded, dims=(-1,)))
        logits = 0.5 * (direct_logits + torch.flip(mirror_logits, dims=(-1,)))
        values = 0.5 * (direct_value + mirror_value)
        actions = torch.arange(7, device=legal_masks.device, dtype=torch.int64)
        legal = ((legal_masks.to(torch.int64).unsqueeze(1) >> actions) & 1) != 0
        logits = logits.masked_fill(~legal, -1.0e9)
        return logits, values


class PublicPolicy:
    """H25-compatible public batch adapter; it accepts no game metadata."""

    def __init__(self, model: PublicActorCritic) -> None:
        self.model = model.eval()

    @torch.inference_mode()
    def logits_values(
        self,
        boards: np.ndarray,
        discs: np.ndarray,
        phases: np.ndarray,
        legal_masks: np.ndarray,
    ) -> tuple[np.ndarray, np.ndarray]:
        logits, values = self.model(
            torch.from_numpy(np.ascontiguousarray(boards)),
            torch.from_numpy(np.ascontiguousarray(discs)),
            torch.from_numpy(np.ascontiguousarray(phases)),
            torch.from_numpy(np.ascontiguousarray(legal_masks)),
        )
        return logits.numpy(), values.numpy()

    @torch.inference_mode()
    def actions(
        self,
        boards: np.ndarray,
        discs: np.ndarray,
        phases: np.ndarray,
        legal_masks: np.ndarray,
    ) -> np.ndarray:
        logits, _ = self.logits_values(boards, discs, phases, legal_masks)
        return np.argmax(logits, axis=1).astype(np.int64)


def parameter_count(model: nn.Module) -> int:
    return sum(parameter.numel() for parameter in model.parameters())


def mirror_masks(masks: np.ndarray) -> np.ndarray:
    result = np.zeros_like(masks)
    for action in range(7):
        result |= ((masks >> action) & 1) << (6 - action)
    return result


def top_row_masks(boards: np.ndarray) -> np.ndarray:
    masks = np.zeros(boards.shape[0], dtype=np.uint8)
    for action in range(7):
        masks |= (boards[:, action] == 0).astype(np.uint8) << action
    return masks


@dataclass(frozen=True)
class TeacherCorpus:
    boards: np.ndarray
    discs: np.ndarray
    phases: np.ndarray
    legal_masks: np.ndarray
    actions: np.ndarray
    q_values: np.ndarray
    games: int
    states: int
    teacher_work: int
    elapsed_seconds: float
    mean_score: float
    mean_moves: float
    censored: int


def concatenate_corpora(*corpora: TeacherCorpus) -> TeacherCorpus:
    return TeacherCorpus(
        boards=np.concatenate([corpus.boards for corpus in corpora]),
        discs=np.concatenate([corpus.discs for corpus in corpora]),
        phases=np.concatenate([corpus.phases for corpus in corpora]),
        legal_masks=np.concatenate([corpus.legal_masks for corpus in corpora]),
        actions=np.concatenate([corpus.actions for corpus in corpora]),
        q_values=np.concatenate([corpus.q_values for corpus in corpora]),
        games=sum(corpus.games for corpus in corpora),
        states=sum(corpus.states for corpus in corpora),
        teacher_work=sum(corpus.teacher_work for corpus in corpora),
        elapsed_seconds=sum(corpus.elapsed_seconds for corpus in corpora),
        mean_score=float(
            np.average(
                [corpus.mean_score for corpus in corpora],
                weights=[corpus.games for corpus in corpora],
            )
        ),
        mean_moves=float(
            np.average(
                [corpus.mean_moves for corpus in corpora],
                weights=[corpus.games for corpus in corpora],
            )
        ),
        censored=sum(corpus.censored for corpus in corpora),
    )


def collect_teacher_corpus(
    extension: Any,
    seed_start: int,
    games: int,
    drive: Literal["teacher", "student"],
    model: PublicActorCritic | None = None,
) -> TeacherCorpus:
    if drive == "student" and model is None:
        raise ValueError("student-driven DAgger requires a model")
    environment = extension.VectorEnvironment(games, seed_start, games, MAXIMUM_MOVES)
    observation = environment.observations()
    board_chunks: list[np.ndarray] = []
    disc_chunks: list[np.ndarray] = []
    phase_chunks: list[np.ndarray] = []
    mask_chunks: list[np.ndarray] = []
    action_chunks: list[np.ndarray] = []
    q_chunks: list[np.ndarray] = []
    scores = np.full(games, -1, dtype=np.int64)
    moves = np.full(games, -1, dtype=np.int32)
    censored = np.zeros(games, dtype=np.uint8)
    teacher_work = 0
    started = time.perf_counter()
    step_index = 0
    student = PublicPolicy(model) if model is not None else None
    while environment.games_completed < games:
        boards, discs, phases, masks, active = observation
        active_indices = np.flatnonzero(active)
        teacher_actions, teacher_q, work = environment.teacher_actions(
            TEACHER_DEPTH, TEACHER_THREADS
        )
        board_chunks.append(np.asarray(boards[active_indices], dtype=np.uint8).copy())
        disc_chunks.append(np.asarray(discs[active_indices], dtype=np.uint8).copy())
        phase_chunks.append(np.asarray(phases[active_indices], dtype=np.uint8).copy())
        mask_chunks.append(np.asarray(masks[active_indices], dtype=np.uint8).copy())
        action_chunks.append(
            np.asarray(teacher_actions[active_indices], dtype=np.int64).copy()
        )
        q_chunks.append(np.asarray(teacher_q[active_indices], dtype=np.float32).copy())
        teacher_work += int(np.asarray(work, dtype=np.uint64).sum())
        actions = np.full(games, -1, dtype=np.int64)
        if drive == "teacher":
            actions[active_indices] = teacher_actions[active_indices]
        else:
            assert student is not None
            actions[active_indices] = student.actions(
                boards[active_indices],
                discs[active_indices],
                phases[active_indices],
                masks[active_indices],
            )
        outcome = environment.step(actions)
        done = np.asarray(outcome[8], dtype=bool) | np.asarray(outcome[9], dtype=bool)
        done_indices = np.flatnonzero(done)
        scores[done_indices] = np.asarray(outcome[10])[done_indices]
        moves[done_indices] = np.asarray(outcome[11])[done_indices]
        censored[done_indices] = np.asarray(outcome[9])[done_indices]
        observation = outcome[:5]
        step_index += 1
        if step_index % 25 == 0:
            elapsed = time.perf_counter() - started
            states = sum(chunk.shape[0] for chunk in board_chunks)
            print(
                "TORCH_RL_TEACHER",
                json.dumps(
                    {
                        "drive": drive,
                        "steps": step_index,
                        "gamesCompleted": int(environment.games_completed),
                        "states": states,
                        "teacherWorkPerSecond": teacher_work / max(elapsed, 1e-9),
                    },
                    sort_keys=True,
                ),
                flush=True,
            )
    elapsed = time.perf_counter() - started
    if np.any(scores < 0) or np.any(moves < 0):
        raise RuntimeError("teacher corpus lost a completed episode")
    return TeacherCorpus(
        boards=np.concatenate(board_chunks),
        discs=np.concatenate(disc_chunks),
        phases=np.concatenate(phase_chunks),
        legal_masks=np.concatenate(mask_chunks),
        actions=np.concatenate(action_chunks),
        q_values=np.concatenate(q_chunks),
        games=games,
        states=sum(chunk.shape[0] for chunk in board_chunks),
        teacher_work=teacher_work,
        elapsed_seconds=elapsed,
        mean_score=float(scores.mean()),
        mean_moves=float(moves.mean()),
        censored=int(censored.sum()),
    )


def clone_loss(
    model: PublicActorCritic,
    boards: Tensor,
    discs: Tensor,
    phases: Tensor,
    masks: Tensor,
    actions: Tensor,
    q_values: Tensor,
) -> tuple[Tensor, Tensor]:
    logits, _ = model(boards, discs, phases, masks)
    hard = F.cross_entropy(logits, actions)
    legal = torch.isfinite(q_values) & (q_values > -1.0e30)
    safe_q = torch.where(legal, q_values, torch.full_like(q_values, -1.0e9))
    maximum = safe_q.max(dim=1, keepdim=True).values
    minimum = torch.where(legal, safe_q, maximum).min(dim=1, keepdim=True).values
    scale = (maximum - minimum).clamp_min(1.0)
    teacher_logits = (safe_q - maximum) / (0.18 * scale)
    teacher_logits = teacher_logits.masked_fill(~legal, -1.0e9)
    teacher_probabilities = torch.softmax(teacher_logits, dim=1)
    soft = -(teacher_probabilities * torch.log_softmax(logits, dim=1)).sum(1).mean()
    return 0.70 * hard + 0.30 * soft, logits


@torch.inference_mode()
def clone_metrics(model: PublicActorCritic, corpus: TeacherCorpus) -> dict[str, float]:
    batch_size = 1_024
    correct = 0
    top2 = 0
    cross_entropy = 0.0
    for start in range(0, corpus.states, batch_size):
        end = min(corpus.states, start + batch_size)
        boards = torch.from_numpy(corpus.boards[start:end])
        discs = torch.from_numpy(corpus.discs[start:end])
        phases = torch.from_numpy(corpus.phases[start:end])
        masks = torch.from_numpy(corpus.legal_masks[start:end])
        actions = torch.from_numpy(corpus.actions[start:end])
        logits, _ = model(boards, discs, phases, masks)
        predictions = logits.argmax(1)
        correct += int((predictions == actions).sum())
        top2 += int((logits.topk(2, dim=1).indices == actions[:, None]).any(1).sum())
        cross_entropy += float(F.cross_entropy(logits, actions, reduction="sum"))
    return {
        "agreement": correct / corpus.states,
        "top2": top2 / corpus.states,
        "crossEntropy": cross_entropy / corpus.states,
    }


def train_clone(
    model: PublicActorCritic,
    training: TeacherCorpus,
    validation: TeacherCorpus,
    epochs: int,
    learning_rate: float,
    label: str,
) -> list[dict[str, float]]:
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=learning_rate, eps=1.0e-5, weight_decay=1.0e-4
    )
    generator = torch.Generator().manual_seed(NETWORK_SEED ^ len(label))
    history: list[dict[str, float]] = []
    model.train()
    for epoch in range(epochs):
        permutation = torch.randperm(training.states, generator=generator)
        loss_sum = 0.0
        seen = 0
        for start in range(0, training.states, CLONE_BATCH_SIZE):
            indices = permutation[start : start + CLONE_BATCH_SIZE]
            loss, _ = clone_loss(
                model,
                torch.from_numpy(training.boards)[indices],
                torch.from_numpy(training.discs)[indices],
                torch.from_numpy(training.phases)[indices],
                torch.from_numpy(training.legal_masks)[indices],
                torch.from_numpy(training.actions)[indices],
                torch.from_numpy(training.q_values)[indices],
            )
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            gradient_norm = torch.nn.utils.clip_grad_norm_(
                model.parameters(), MAX_GRADIENT_NORM
            )
            if not torch.isfinite(gradient_norm):
                raise RuntimeError("non-finite cloning gradient")
            optimizer.step()
            count = indices.numel()
            loss_sum += float(loss.detach()) * count
            seen += count
        model.eval()
        validation_metrics = clone_metrics(model, validation)
        record = {
            "epoch": float(epoch + 1),
            "loss": loss_sum / seen,
            **validation_metrics,
        }
        history.append(record)
        print("TORCH_RL_CLONE", json.dumps({"stage": label, **record}), flush=True)
        model.train()
    model.eval()
    return history


def train_hard_clone(
    model: PublicActorCritic,
    training: TeacherCorpus,
    validation: TeacherCorpus,
) -> list[dict[str, float]]:
    """Apply the fixed repair: pure hard-D2 action cross-entropy."""
    optimizer = torch.optim.AdamW(
        model.parameters(),
        lr=CLONE_LEARNING_RATE,
        eps=1.0e-5,
        weight_decay=1.0e-4,
    )
    generator = torch.Generator().manual_seed(NETWORK_SEED ^ 0x4841_5244)
    history: list[dict[str, float]] = []
    model.train()
    boards = torch.from_numpy(training.boards)
    discs = torch.from_numpy(training.discs)
    phases = torch.from_numpy(training.phases)
    masks = torch.from_numpy(training.legal_masks)
    actions = torch.from_numpy(training.actions)
    for epoch in range(CORRECTED_HARD_EPOCHS):
        permutation = torch.randperm(training.states, generator=generator)
        loss_sum = 0.0
        seen = 0
        for start in range(0, training.states, CLONE_BATCH_SIZE):
            indices = permutation[start : start + CLONE_BATCH_SIZE]
            logits, _ = model(
                boards[indices],
                discs[indices],
                phases[indices],
                masks[indices],
            )
            loss = F.cross_entropy(logits, actions[indices])
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            gradient_norm = torch.nn.utils.clip_grad_norm_(
                model.parameters(), MAX_GRADIENT_NORM
            )
            if not torch.isfinite(gradient_norm):
                raise RuntimeError("non-finite corrected cloning gradient")
            optimizer.step()
            count = indices.numel()
            loss_sum += float(loss.detach()) * count
            seen += count
        model.eval()
        validation_metrics = clone_metrics(model, validation)
        record = {
            "epoch": float(epoch + 1),
            "hardCrossEntropy": loss_sum / seen,
            **validation_metrics,
        }
        history.append(record)
        print(
            "TORCH_RL_CORRECTED_CLONE",
            json.dumps(record, sort_keys=True),
            flush=True,
        )
        model.train()
    model.eval()
    return history


def soft_target_diagnostic(corpus: TeacherCorpus) -> dict[str, float]:
    chosen_probabilities: list[float] = []
    entropies: list[float] = []
    normalized_margins: list[float] = []
    terminal_outliers = 0
    for q_values, chosen in zip(corpus.q_values, corpus.actions):
        legal = np.isfinite(q_values) & (q_values > -1.0e30)
        legal_values = q_values[legal].astype(np.float64)
        maximum = float(legal_values.max())
        minimum = float(legal_values.min())
        scale = max(1.0, maximum - minimum)
        logits = (legal_values - maximum) / (0.18 * scale)
        probabilities = np.exp(logits - logits.max())
        probabilities /= probabilities.sum()
        legal_actions = np.flatnonzero(legal)
        chosen_offset = int(np.flatnonzero(legal_actions == chosen)[0])
        chosen_probabilities.append(float(probabilities[chosen_offset]))
        entropies.append(
            float(-(probabilities * np.log(probabilities + 1.0e-30)).sum())
        )
        ordered = np.sort(legal_values)
        margin = float(ordered[-1] - ordered[-2]) if ordered.size > 1 else scale
        normalized_margins.append(margin / scale)
        terminal_outliers += minimum <= -500_000.0
    return {
        "meanSoftTargetChosenProbability": float(np.mean(chosen_probabilities)),
        "meanSoftTargetEntropy": float(np.mean(entropies)),
        "medianTopTwoMarginOverFullRange": float(np.median(normalized_margins)),
        "terminalOutlierFraction": terminal_outliers / corpus.states,
    }


class Evaluation(NamedTuple):
    scores: np.ndarray
    moves: np.ndarray
    cleared: np.ndarray
    revealed: np.ndarray
    maximum_chain: np.ndarray
    censored: np.ndarray
    elapsed_seconds: float
    decisions: int

    def summary(self) -> dict[str, float | int]:
        return {
            "games": int(self.scores.size),
            "meanScore": float(self.scores.mean()),
            "meanMoves": float(self.moves.mean()),
            "minimumScore": int(self.scores.min()),
            "maximumScore": int(self.scores.max()),
            "minimumMoves": int(self.moves.min()),
            "maximumMoves": int(self.moves.max()),
            "meanClearsPerMove": float(self.cleared.sum() / self.moves.sum()),
            "meanRevealsPerMove": float(self.revealed.sum() / self.moves.sum()),
            "meanMaximumChain": float(self.maximum_chain.mean()),
            "censored": int(self.censored.sum()),
            "elapsedSeconds": self.elapsed_seconds,
            "decisionsPerSecond": self.decisions / max(self.elapsed_seconds, 1e-9),
        }


def evaluate_policy(
    extension: Any,
    policy: Literal["random", "d1", "d2", "network"],
    seed_start: int,
    games: int,
    model: PublicActorCritic | None = None,
) -> Evaluation:
    if policy == "network" and model is None:
        raise ValueError("network evaluation requires a model")
    environment = extension.VectorEnvironment(games, seed_start, games, MAXIMUM_MOVES)
    observation = environment.observations()
    scores = np.full(games, -1, dtype=np.int64)
    moves = np.full(games, -1, dtype=np.int32)
    cleared = np.full(games, -1, dtype=np.int64)
    revealed = np.full(games, -1, dtype=np.int64)
    maximum_chain = np.full(games, -1, dtype=np.int32)
    censored = np.zeros(games, dtype=np.uint8)
    random = np.random.Generator(np.random.PCG64(RANDOM_POLICY_SEED))
    network = PublicPolicy(model) if model is not None else None
    started = time.perf_counter()
    decisions = 0
    while environment.games_completed < games:
        boards, discs, phases, masks, active = observation
        active_indices = np.flatnonzero(active)
        actions = np.full(games, -1, dtype=np.int64)
        if policy == "random":
            for index in active_indices:
                legal = [action for action in range(7) if masks[index] & (1 << action)]
                actions[index] = legal[int(random.integers(len(legal)))]
        elif policy in ("d1", "d2"):
            depth = 1 if policy == "d1" else 2
            teacher_actions, _, _ = environment.teacher_actions(depth, TEACHER_THREADS)
            actions[active_indices] = teacher_actions[active_indices]
        else:
            assert network is not None
            actions[active_indices] = network.actions(
                boards[active_indices],
                discs[active_indices],
                phases[active_indices],
                masks[active_indices],
            )
        decisions += active_indices.size
        outcome = environment.step(actions)
        done = np.asarray(outcome[8], dtype=bool) | np.asarray(outcome[9], dtype=bool)
        done_indices = np.flatnonzero(done)
        scores[done_indices] = np.asarray(outcome[10])[done_indices]
        moves[done_indices] = np.asarray(outcome[11])[done_indices]
        cleared[done_indices] = np.asarray(outcome[12])[done_indices]
        revealed[done_indices] = np.asarray(outcome[13])[done_indices]
        maximum_chain[done_indices] = np.asarray(outcome[14])[done_indices]
        censored[done_indices] = np.asarray(outcome[9])[done_indices]
        observation = outcome[:5]
    elapsed = time.perf_counter() - started
    if any(np.any(values < 0) for values in (scores, moves, cleared, revealed)):
        raise RuntimeError("evaluation lost completed episode metrics")
    result = Evaluation(
        scores,
        moves,
        cleared,
        revealed,
        maximum_chain,
        censored,
        elapsed,
        decisions,
    )
    print(
        "TORCH_RL_EVAL",
        json.dumps({"policy": policy, **result.summary()}, sort_keys=True),
        flush=True,
    )
    return result


@dataclass(frozen=True)
class RolloutBatch:
    boards: np.ndarray
    discs: np.ndarray
    phases: np.ndarray
    legal_masks: np.ndarray
    actions: np.ndarray
    old_log_probabilities: np.ndarray
    old_values: np.ndarray
    advantages: np.ndarray
    returns: np.ndarray
    rewards: np.ndarray
    episode_scores: np.ndarray
    episode_moves: np.ndarray
    censored: int
    transitions: int
    elapsed_seconds: float


def compute_gae(
    rewards: np.ndarray,
    old_values: np.ndarray,
    terminals: np.ndarray,
    truncations: np.ndarray,
    bootstraps: np.ndarray,
    trajectories: list[list[int]],
) -> tuple[np.ndarray, np.ndarray]:
    """Compute GAE without assuming that a vector lane is contiguous in storage."""
    if not (
        rewards.shape
        == old_values.shape
        == terminals.shape
        == truncations.shape
        == bootstraps.shape
    ):
        raise ValueError("GAE arrays must have identical one-dimensional shapes")
    if rewards.ndim != 1:
        raise ValueError("GAE inputs must be one-dimensional")
    advantages = np.zeros(rewards.size, dtype=np.float32)
    returns = np.zeros(rewards.size, dtype=np.float32)
    seen = np.zeros(rewards.size, dtype=np.uint8)
    for trajectory in trajectories:
        gae = 0.0
        next_value = 0.0
        if trajectory:
            last = trajectory[-1]
            if truncations[last]:
                next_value = float(bootstraps[last])
        for sample_index in reversed(trajectory):
            if sample_index < 0 or sample_index >= rewards.size:
                raise ValueError("GAE trajectory index is out of range")
            if seen[sample_index]:
                raise ValueError("GAE trajectory index appears more than once")
            seen[sample_index] = 1
            delta = (
                float(rewards[sample_index])
                + GAMMA * next_value
                - float(old_values[sample_index])
            )
            gae = delta + GAMMA * GAE_LAMBDA * gae
            advantages[sample_index] = gae
            returns[sample_index] = gae + old_values[sample_index]
            next_value = float(old_values[sample_index])
            if terminals[sample_index] or truncations[sample_index]:
                # The vector environment never autoresets.  An episode boundary
                # can therefore occur only at the final item of its lane.
                if sample_index != trajectory[-1]:
                    raise ValueError("episode boundary appeared mid-trajectory")
        if trajectory:
            first_terminal = terminals[trajectory[:-1]] | truncations[trajectory[:-1]]
            if np.any(first_terminal):
                raise ValueError("episode boundary appeared before trajectory end")
    if rewards.size and not np.all(seen):
        raise ValueError("GAE trajectories do not cover every transition exactly once")
    return advantages, returns


def collect_ppo_games(
    extension: Any,
    model: PublicActorCritic,
    seed_start: int,
    games: int,
    generator: torch.Generator,
    deadline: float | None = None,
) -> RolloutBatch:
    environment = extension.VectorEnvironment(games, seed_start, games, MAXIMUM_MOVES)
    observation = environment.observations()
    board_chunks: list[np.ndarray] = []
    disc_chunks: list[np.ndarray] = []
    phase_chunks: list[np.ndarray] = []
    mask_chunks: list[np.ndarray] = []
    action_chunks: list[np.ndarray] = []
    log_probability_chunks: list[np.ndarray] = []
    value_chunks: list[np.ndarray] = []
    reward_chunks: list[np.ndarray] = []
    terminal_chunks: list[np.ndarray] = []
    truncated_chunks: list[np.ndarray] = []
    bootstrap_chunks: list[np.ndarray] = []
    trajectories: list[list[int]] = [[] for _ in range(games)]
    episode_scores = np.full(games, -1, dtype=np.int64)
    episode_moves = np.full(games, -1, dtype=np.int32)
    censored = np.zeros(games, dtype=np.uint8)
    cursor = 0
    started = time.perf_counter()
    model.eval()
    while environment.games_completed < games:
        if deadline is not None:
            enforce_direct_ppo_resource_limits(deadline)
        boards, discs, phases, masks, active = observation
        active_indices = np.flatnonzero(active)
        active_boards = np.asarray(boards[active_indices], dtype=np.uint8).copy()
        active_discs = np.asarray(discs[active_indices], dtype=np.uint8).copy()
        active_phases = np.asarray(phases[active_indices], dtype=np.uint8).copy()
        active_masks = np.asarray(masks[active_indices], dtype=np.uint8).copy()
        with torch.inference_mode():
            logits, values = model(
                torch.from_numpy(active_boards),
                torch.from_numpy(active_discs),
                torch.from_numpy(active_phases),
                torch.from_numpy(active_masks),
            )
            probabilities = torch.softmax(logits, dim=1)
            sampled = torch.multinomial(
                probabilities, 1, replacement=True, generator=generator
            ).squeeze(1)
            log_probabilities = torch.log_softmax(logits, dim=1).gather(
                1, sampled[:, None]
            ).squeeze(1)
        actions = np.full(games, -1, dtype=np.int64)
        sampled_numpy = sampled.numpy().astype(np.int64, copy=False)
        actions[active_indices] = sampled_numpy
        outcome = environment.step(actions)
        score_delta = np.asarray(outcome[5])[active_indices].astype(np.float32)
        clears = np.asarray(outcome[6])[active_indices].astype(np.float32)
        reveals = np.asarray(outcome[7])[active_indices].astype(np.float32)
        terminated = np.asarray(outcome[8])[active_indices].astype(bool)
        truncated = np.asarray(outcome[9])[active_indices].astype(bool)
        rewards = (
            score_delta / float(EXPECTED_LEVEL_BONUS)
            + SURVIVAL_REWARD
            + CLEAR_REWARD * clears
            + REVEAL_REWARD * reveals
            + TERMINAL_PENALTY * terminated.astype(np.float32)
        ).astype(np.float32)
        bootstrap = np.zeros(active_indices.size, dtype=np.float32)
        if np.any(truncated):
            final_boards = np.asarray(outcome[0])[active_indices[truncated]]
            final_discs = np.asarray(outcome[1])[active_indices[truncated]]
            final_phases = np.asarray(outcome[2])[active_indices[truncated]]
            final_masks = top_row_masks(final_boards)
            with torch.inference_mode():
                _, final_values = model(
                    torch.from_numpy(np.ascontiguousarray(final_boards)),
                    torch.from_numpy(np.ascontiguousarray(final_discs)),
                    torch.from_numpy(np.ascontiguousarray(final_phases)),
                    torch.from_numpy(np.ascontiguousarray(final_masks)),
                )
            bootstrap[truncated] = final_values.numpy()
        count = active_indices.size
        batch_indices = np.arange(cursor, cursor + count)
        for environment_index, sample_index in zip(active_indices, batch_indices):
            trajectories[int(environment_index)].append(int(sample_index))
        cursor += count
        board_chunks.append(active_boards)
        disc_chunks.append(active_discs)
        phase_chunks.append(active_phases)
        mask_chunks.append(active_masks)
        action_chunks.append(sampled_numpy.copy())
        log_probability_chunks.append(log_probabilities.numpy().copy())
        value_chunks.append(values.numpy().copy())
        reward_chunks.append(rewards)
        terminal_chunks.append(terminated)
        truncated_chunks.append(truncated)
        bootstrap_chunks.append(bootstrap)
        done = np.asarray(outcome[8], dtype=bool) | np.asarray(outcome[9], dtype=bool)
        done_indices = np.flatnonzero(done)
        episode_scores[done_indices] = np.asarray(outcome[10])[done_indices]
        episode_moves[done_indices] = np.asarray(outcome[11])[done_indices]
        censored[done_indices] = np.asarray(outcome[9])[done_indices]
        observation = outcome[:5]

    old_values = np.concatenate(value_chunks).astype(np.float32)
    rewards = np.concatenate(reward_chunks).astype(np.float32)
    terminals = np.concatenate(terminal_chunks)
    truncations = np.concatenate(truncated_chunks)
    bootstraps = np.concatenate(bootstrap_chunks).astype(np.float32)
    advantages, returns = compute_gae(
        rewards,
        old_values,
        terminals,
        truncations,
        bootstraps,
        trajectories,
    )
    elapsed = time.perf_counter() - started
    if np.any(episode_scores < 0) or np.any(episode_moves < 0):
        raise RuntimeError("PPO collection lost episode summaries")
    return RolloutBatch(
        boards=np.concatenate(board_chunks),
        discs=np.concatenate(disc_chunks),
        phases=np.concatenate(phase_chunks),
        legal_masks=np.concatenate(mask_chunks),
        actions=np.concatenate(action_chunks),
        old_log_probabilities=np.concatenate(log_probability_chunks).astype(
            np.float32
        ),
        old_values=old_values,
        advantages=advantages,
        returns=returns,
        rewards=rewards,
        episode_scores=episode_scores,
        episode_moves=episode_moves,
        censored=int(censored.sum()),
        transitions=cursor,
        elapsed_seconds=elapsed,
    )


def ppo_update(
    model: PublicActorCritic,
    optimizer: torch.optim.Optimizer,
    rollout: RolloutBatch,
    imitation_anchor: TeacherCorpus,
    generator: torch.Generator,
    hard_anchor: bool = False,
) -> dict[str, float]:
    advantages = torch.from_numpy(rollout.advantages)
    advantages = (advantages - advantages.mean()) / advantages.std().clamp_min(1e-6)
    old_log_probabilities = torch.from_numpy(rollout.old_log_probabilities)
    old_values = torch.from_numpy(rollout.old_values)
    returns = torch.from_numpy(rollout.returns)
    actions = torch.from_numpy(rollout.actions)
    samples = rollout.transitions
    totals = {
        "policyLoss": 0.0,
        "valueLoss": 0.0,
        "entropy": 0.0,
        "anchorLoss": 0.0,
        "approximateKl": 0.0,
        "clipFraction": 0.0,
    }
    updates = 0
    model.train()
    for _ in range(PPO_EPOCHS):
        permutation = torch.randperm(samples, generator=generator)
        for start in range(0, samples, PPO_MINIBATCH):
            indices = permutation[start : start + PPO_MINIBATCH]
            logits, values = model(
                torch.from_numpy(rollout.boards)[indices],
                torch.from_numpy(rollout.discs)[indices],
                torch.from_numpy(rollout.phases)[indices],
                torch.from_numpy(rollout.legal_masks)[indices],
            )
            log_probabilities = torch.log_softmax(logits, dim=1)
            selected = log_probabilities.gather(1, actions[indices, None]).squeeze(1)
            ratio = torch.exp(selected - old_log_probabilities[indices])
            objective = ratio * advantages[indices]
            clipped = torch.clamp(
                ratio, 1.0 - CLIP_RATIO, 1.0 + CLIP_RATIO
            ) * advantages[indices]
            policy_loss = -torch.minimum(objective, clipped).mean()
            clipped_values = old_values[indices] + torch.clamp(
                values - old_values[indices], -CLIP_RATIO, CLIP_RATIO
            )
            raw_value_loss = F.smooth_l1_loss(values, returns[indices])
            clipped_value_loss = F.smooth_l1_loss(
                clipped_values, returns[indices]
            )
            value_loss = torch.maximum(raw_value_loss, clipped_value_loss)
            probabilities = torch.softmax(logits, dim=1)
            entropy = -(probabilities * log_probabilities).sum(1).mean()

            anchor_indices = torch.randint(
                imitation_anchor.states,
                (indices.numel(),),
                generator=generator,
            )
            anchor_logits, _ = model(
                torch.from_numpy(imitation_anchor.boards)[anchor_indices],
                torch.from_numpy(imitation_anchor.discs)[anchor_indices],
                torch.from_numpy(imitation_anchor.phases)[anchor_indices],
                torch.from_numpy(imitation_anchor.legal_masks)[anchor_indices],
            )
            if hard_anchor:
                anchor_loss = F.cross_entropy(
                    anchor_logits,
                    torch.from_numpy(imitation_anchor.actions)[anchor_indices],
                )
            else:
                anchor_loss, _ = clone_loss(
                    model,
                    torch.from_numpy(imitation_anchor.boards)[anchor_indices],
                    torch.from_numpy(imitation_anchor.discs)[anchor_indices],
                    torch.from_numpy(imitation_anchor.phases)[anchor_indices],
                    torch.from_numpy(imitation_anchor.legal_masks)[anchor_indices],
                    torch.from_numpy(imitation_anchor.actions)[anchor_indices],
                    torch.from_numpy(imitation_anchor.q_values)[anchor_indices],
                )
            loss = (
                policy_loss
                + VALUE_COEFFICIENT * value_loss
                - ENTROPY_COEFFICIENT * entropy
                + IMITATION_ANCHOR_COEFFICIENT * anchor_loss
            )
            optimizer.zero_grad(set_to_none=True)
            loss.backward()
            gradient_norm = torch.nn.utils.clip_grad_norm_(
                model.parameters(), MAX_GRADIENT_NORM
            )
            if not torch.isfinite(gradient_norm):
                raise RuntimeError("non-finite PPO gradient")
            optimizer.step()
            with torch.no_grad():
                approximate_kl = (
                    old_log_probabilities[indices] - selected
                ).mean()
                clip_fraction = ((ratio - 1.0).abs() > CLIP_RATIO).float().mean()
            values_to_add = (
                policy_loss,
                value_loss,
                entropy,
                anchor_loss,
                approximate_kl,
                clip_fraction,
            )
            for key, value in zip(totals, values_to_add):
                totals[key] += float(value.detach())
            updates += 1
    model.eval()
    return {key: value / updates for key, value in totals.items()}


class DirectPpoLosses(NamedTuple):
    total: Tensor
    policy: Tensor
    value: Tensor
    entropy: Tensor
    approximate_kl: Tensor
    clip_fraction: Tensor


def direct_ppo_losses(
    logits: Tensor,
    values: Tensor,
    actions: Tensor,
    old_log_probabilities: Tensor,
    old_values: Tensor,
    advantages: Tensor,
    returns: Tensor,
) -> DirectPpoLosses:
    """Standard clipped PPO objective with a per-sample clipped value loss."""
    log_probabilities = torch.log_softmax(logits, dim=1)
    selected = log_probabilities.gather(1, actions[:, None]).squeeze(1)
    ratio = torch.exp(selected - old_log_probabilities)
    unclipped_objective = ratio * advantages
    clipped_objective = torch.clamp(
        ratio, 1.0 - CLIP_RATIO, 1.0 + CLIP_RATIO
    ) * advantages
    policy_loss = -torch.minimum(
        unclipped_objective, clipped_objective
    ).mean()

    clipped_values = old_values + torch.clamp(
        values - old_values, -CLIP_RATIO, CLIP_RATIO
    )
    raw_value_loss = F.smooth_l1_loss(values, returns, reduction="none")
    clipped_value_loss = F.smooth_l1_loss(
        clipped_values, returns, reduction="none"
    )
    value_loss = torch.maximum(raw_value_loss, clipped_value_loss).mean()
    probabilities = torch.softmax(logits, dim=1)
    entropy = -(probabilities * log_probabilities).sum(1).mean()
    approximate_kl = (old_log_probabilities - selected).mean()
    clip_fraction = ((ratio - 1.0).abs() > CLIP_RATIO).to(torch.float32).mean()
    total = (
        policy_loss
        + VALUE_COEFFICIENT * value_loss
        - ENTROPY_COEFFICIENT * entropy
    )
    return DirectPpoLosses(
        total,
        policy_loss,
        value_loss,
        entropy,
        approximate_kl,
        clip_fraction,
    )


def direct_ppo_update(
    model: PublicActorCritic,
    optimizer: torch.optim.Optimizer,
    rollout: RolloutBatch,
    generator: torch.Generator,
    deadline: float,
) -> dict[str, float | int]:
    """Run the fixed anchor-free PPO update over one complete cohort."""
    if rollout.transitions < 2:
        raise RuntimeError("direct PPO requires at least two transitions")
    advantages = torch.from_numpy(rollout.advantages)
    advantages = (advantages - advantages.mean()) / advantages.std(
        unbiased=False
    ).clamp_min(1.0e-6)
    boards = torch.from_numpy(rollout.boards)
    discs = torch.from_numpy(rollout.discs)
    phases = torch.from_numpy(rollout.phases)
    legal_masks = torch.from_numpy(rollout.legal_masks)
    actions = torch.from_numpy(rollout.actions)
    old_log_probabilities = torch.from_numpy(rollout.old_log_probabilities)
    old_values = torch.from_numpy(rollout.old_values)
    returns = torch.from_numpy(rollout.returns)
    totals = {
        "policyLoss": 0.0,
        "valueLoss": 0.0,
        "entropy": 0.0,
        "approximateKl": 0.0,
        "clipFraction": 0.0,
    }
    updates = 0
    started = time.perf_counter()
    model.train()
    for _ in range(PPO_EPOCHS):
        permutation = torch.randperm(rollout.transitions, generator=generator)
        for start in range(0, rollout.transitions, PPO_MINIBATCH):
            enforce_direct_ppo_resource_limits(deadline)
            indices = permutation[start : start + PPO_MINIBATCH]
            logits, values = model(
                boards[indices],
                discs[indices],
                phases[indices],
                legal_masks[indices],
            )
            losses = direct_ppo_losses(
                logits,
                values,
                actions[indices],
                old_log_probabilities[indices],
                old_values[indices],
                advantages[indices],
                returns[indices],
            )
            optimizer.zero_grad(set_to_none=True)
            losses.total.backward()
            gradient_norm = torch.nn.utils.clip_grad_norm_(
                model.parameters(), MAX_GRADIENT_NORM
            )
            if not torch.isfinite(gradient_norm):
                raise RuntimeError("non-finite direct-PPO gradient")
            optimizer.step()
            totals["policyLoss"] += float(losses.policy.detach())
            totals["valueLoss"] += float(losses.value.detach())
            totals["entropy"] += float(losses.entropy.detach())
            totals["approximateKl"] += float(losses.approximate_kl.detach())
            totals["clipFraction"] += float(losses.clip_fraction.detach())
            updates += 1
            enforce_direct_ppo_resource_limits(deadline)
    model.eval()
    if updates == 0:
        raise RuntimeError("direct PPO performed no optimizer update")
    return {
        **{key: value / updates for key, value in totals.items()},
        "optimizerUpdates": updates,
        "updateSeconds": time.perf_counter() - started,
    }


def gradaccum256_ppo_update(
    model: PublicActorCritic,
    optimizer: torch.optim.Optimizer,
    rollout: RolloutBatch,
    generator: torch.Generator,
    deadline: float,
) -> dict[str, float | int]:
    """Exact logical-1024 PPO using ordered <=256 activation chunks."""
    if rollout.transitions < 2:
        raise RuntimeError("gradient-accumulation PPO requires two transitions")
    advantages = torch.from_numpy(rollout.advantages)
    advantages = (advantages - advantages.mean()) / advantages.std(
        unbiased=False
    ).clamp_min(1.0e-6)
    boards = torch.from_numpy(rollout.boards)
    discs = torch.from_numpy(rollout.discs)
    phases = torch.from_numpy(rollout.phases)
    legal_masks = torch.from_numpy(rollout.legal_masks)
    actions = torch.from_numpy(rollout.actions)
    old_log_probabilities = torch.from_numpy(rollout.old_log_probabilities)
    old_values = torch.from_numpy(rollout.old_values)
    returns = torch.from_numpy(rollout.returns)
    totals = {
        "policyLoss": 0.0,
        "valueLoss": 0.0,
        "entropy": 0.0,
        "approximateKl": 0.0,
        "clipFraction": 0.0,
    }
    logical_updates = 0
    physical_chunks = 0
    started = time.perf_counter()
    model.train()
    for _ in range(PPO_EPOCHS):
        permutation = torch.randperm(rollout.transitions, generator=generator)
        for logical_start in range(0, rollout.transitions, PPO_MINIBATCH):
            logical_indices = permutation[
                logical_start : logical_start + PPO_MINIBATCH
            ]
            logical_size = logical_indices.numel()
            optimizer.zero_grad(set_to_none=True)
            logical_metrics = {key: 0.0 for key in totals}
            for physical_start in range(
                0, logical_size, GRADACCUM_PHYSICAL_MINIBATCH
            ):
                enforce_direct_ppo_resource_limits(deadline)
                indices = logical_indices[
                    physical_start : physical_start + GRADACCUM_PHYSICAL_MINIBATCH
                ]
                weight = indices.numel() / logical_size
                logits, values = model(
                    boards[indices],
                    discs[indices],
                    phases[indices],
                    legal_masks[indices],
                )
                losses = direct_ppo_losses(
                    logits,
                    values,
                    actions[indices],
                    old_log_probabilities[indices],
                    old_values[indices],
                    advantages[indices],
                    returns[indices],
                )
                (losses.total * weight).backward()
                logical_metrics["policyLoss"] += float(losses.policy.detach()) * weight
                logical_metrics["valueLoss"] += float(losses.value.detach()) * weight
                logical_metrics["entropy"] += float(losses.entropy.detach()) * weight
                logical_metrics["approximateKl"] += (
                    float(losses.approximate_kl.detach()) * weight
                )
                logical_metrics["clipFraction"] += (
                    float(losses.clip_fraction.detach()) * weight
                )
                physical_chunks += 1
                enforce_direct_ppo_resource_limits(deadline)
            gradient_norm = torch.nn.utils.clip_grad_norm_(
                model.parameters(), MAX_GRADIENT_NORM
            )
            if not torch.isfinite(gradient_norm):
                raise RuntimeError("non-finite gradient-accumulation PPO gradient")
            optimizer.step()
            logical_updates += 1
            for key in totals:
                totals[key] += logical_metrics[key]
            enforce_direct_ppo_resource_limits(deadline)
    model.eval()
    if logical_updates == 0:
        raise RuntimeError("gradient-accumulation PPO performed no update")
    return {
        **{key: value / logical_updates for key, value in totals.items()},
        "optimizerUpdates": logical_updates,
        "physicalChunks": physical_chunks,
        "logicalMinibatch": PPO_MINIBATCH,
        "physicalMinibatch": GRADACCUM_PHYSICAL_MINIBATCH,
        "updateSeconds": time.perf_counter() - started,
    }


def save_checkpoint(
    path: Path,
    model: PublicActorCritic,
    stage: str,
    iterations: int,
) -> None:
    torch.save(
        {
            "format": "drop7-public-conv-actor-critic-v1",
            "stage": stage,
            "iterations": iterations,
            "levelBonus": EXPECTED_LEVEL_BONUS,
            "model": model.state_dict(),
            "architecture": architecture_manifest(model),
        },
        path,
    )


def architecture_manifest(model: PublicActorCritic) -> dict[str, Any]:
    return {
        "format": "drop7-public-conv-actor-critic-v1",
        "boardShape": [7, 7],
        "boardCategories": model.board_categories,
        "discCategories": model.disc_categories,
        "phaseCategories": model.phase_categories,
        "inputChannels": 22,
        "channels": model.channels,
        "residualBlocks": model.residual_blocks,
        "hidden": model.hidden,
        "actions": 7,
        "reflection": "mean(raw(x), reverse(raw(horizontalFlip(x))))",
        "inputNormalization": {
            "board": "one-hot cell values 0..9",
            "nextDisc": "one-hot values 1..7 broadcast over 7x7",
            "movesRemaining": "one-hot values 1..5 broadcast over 7x7",
            "legalMask": "post-ensemble bit mask; illegal logit = -1e9",
        },
        "publicInputsOnly": True,
        "excluded": [
            "gameSeed",
            "futureTape",
            "score",
            "level",
            "movesPlayed",
            "history",
        ],
        "parameterCount": parameter_count(model),
    }


def export_public_policy(
    model: PublicActorCritic,
    golden: TeacherCorpus,
    manifest_path: Path = EXPORT_MANIFEST,
    weights_path: Path = EXPORT_WEIGHTS,
    golden_path: Path = EXPORT_GOLDEN,
    torchscript_path: Path = EXPORT_TORCHSCRIPT,
) -> dict[str, Any]:
    model = copy.deepcopy(model).eval()
    parameters: list[dict[str, Any]] = []
    offset = 0
    with weights_path.open("wb") as output:
        for name, tensor in model.state_dict().items():
            values = tensor.detach().cpu().contiguous().numpy().astype("<f4")
            payload = values.tobytes(order="C")
            output.write(payload)
            parameters.append(
                {
                    "name": name,
                    "shape": list(values.shape),
                    "offsetFloats": offset,
                    "countFloats": values.size,
                }
            )
            offset += values.size
    manifest = {
        **architecture_manifest(model),
        "weightFile": weights_path.name,
        "weightEncoding": "little-endian IEEE-754 Float32, row-major",
        "weightBytes": weights_path.stat().st_size,
        "weightSha256": sha256_file(weights_path),
        "parameters": parameters,
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    # Reload solely from the raw file and manifest, then prove a golden batch.
    raw = np.fromfile(weights_path, dtype="<f4")
    restored = PublicActorCritic().eval()
    restored_state: dict[str, Tensor] = {}
    for item in parameters:
        begin = int(item["offsetFloats"])
        end = begin + int(item["countFloats"])
        values = raw[begin:end].reshape(item["shape"]).copy()
        restored_state[str(item["name"])] = torch.from_numpy(values)
    restored.load_state_dict(restored_state, strict=True)
    count = min(16, golden.states)
    policy = PublicPolicy(model)
    restored_policy = PublicPolicy(restored)
    logits, values = policy.logits_values(
        golden.boards[:count],
        golden.discs[:count],
        golden.phases[:count],
        golden.legal_masks[:count],
    )
    restored_logits, restored_values = restored_policy.logits_values(
        golden.boards[:count],
        golden.discs[:count],
        golden.phases[:count],
        golden.legal_masks[:count],
    )
    maximum_logit_error = float(np.max(np.abs(logits - restored_logits)))
    maximum_value_error = float(np.max(np.abs(values - restored_values)))
    if maximum_logit_error != 0 or maximum_value_error != 0:
        raise RuntimeError("raw public-policy export is not bit-exact")
    golden_payload = {
        "format": "drop7-public-conv-golden-v1",
        "examples": [
            {
                "board": golden.boards[index].tolist(),
                "nextDisc": int(golden.discs[index]),
                "movesRemaining": int(golden.phases[index]),
                "legalMask": int(golden.legal_masks[index]),
                "logits": [float(value) for value in logits[index]],
                "value": float(values[index]),
                "action": int(np.argmax(logits[index])),
            }
            for index in range(count)
        ],
        "pythonVsRawMaximumLogitError": maximum_logit_error,
        "pythonVsRawMaximumValueError": maximum_value_error,
    }
    golden_path.write_text(
        json.dumps(golden_payload, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    scripted = torch.jit.script(model)
    scripted.save(str(torchscript_path))
    with torch.inference_mode():
        scripted_logits, scripted_values = scripted(
            torch.from_numpy(golden.boards[:count]),
            torch.from_numpy(golden.discs[:count]),
            torch.from_numpy(golden.phases[:count]),
            torch.from_numpy(golden.legal_masks[:count]),
        )
    script_logit_error = float(
        np.max(np.abs(logits - scripted_logits.numpy()))
    )
    script_value_error = float(
        np.max(np.abs(values - scripted_values.numpy()))
    )
    if script_logit_error > 1.0e-6 or script_value_error > 1.0e-6:
        raise RuntimeError("TorchScript public-policy export mismatch")
    return {
        "manifest": str(manifest_path),
        "manifestBytes": manifest_path.stat().st_size,
        "manifestSha256": sha256_file(manifest_path),
        "weights": str(weights_path),
        "weightBytes": weights_path.stat().st_size,
        "weightSha256": sha256_file(weights_path),
        "golden": str(golden_path),
        "goldenBytes": golden_path.stat().st_size,
        "goldenSha256": sha256_file(golden_path),
        "torchscript": str(torchscript_path),
        "torchscriptBytes": torchscript_path.stat().st_size,
        "torchscriptSha256": sha256_file(torchscript_path),
        "pythonVsRawMaximumLogitError": maximum_logit_error,
        "pythonVsRawMaximumValueError": maximum_value_error,
        "pythonVsTorchscriptMaximumLogitError": script_logit_error,
        "pythonVsTorchscriptMaximumValueError": script_value_error,
    }


def ratio_at_least(candidate: Evaluation, baseline: Evaluation, ratio: float) -> bool:
    return (
        candidate.scores.mean() >= ratio * baseline.scores.mean()
        and candidate.moves.mean() >= ratio * baseline.moves.mean()
    )


def warm_gate(
    validation: dict[str, float],
    clone: Evaluation,
    random: Evaluation,
    d1: Evaluation,
) -> bool:
    return (
        validation["agreement"] >= MINIMUM_D2_VALIDATION_AGREEMENT
        and ratio_at_least(clone, random, WARM_RANDOM_RATIO)
        and ratio_at_least(clone, d1, WARM_D1_RATIO)
        and clone.censored.sum() == 0
    )


def pilot_gate(
    candidate: Evaluation,
    clone: Evaluation,
    random: Evaluation,
    d1: Evaluation,
    d2: Evaluation,
) -> bool:
    return (
        ratio_at_least(candidate, random, PILOT_RANDOM_RATIO)
        and ratio_at_least(candidate, d1, PILOT_D1_RATIO)
        and ratio_at_least(candidate, d2, PILOT_D2_RATIO)
        and ratio_at_least(candidate, clone, PILOT_CLONE_RETENTION)
        and candidate.censored.sum() == 0
    )


def run_ppo_iterations(
    extension: Any,
    model: PublicActorCritic,
    anchor: TeacherCorpus,
    seed_start: int,
    iterations: int,
    iteration_offset: int,
    generator: torch.Generator,
    optimizer: torch.optim.Optimizer,
    hard_anchor: bool = False,
) -> list[dict[str, Any]]:
    history: list[dict[str, Any]] = []
    for local_iteration in range(iterations):
        iteration = iteration_offset + local_iteration
        start = seed_start + local_iteration * PPO_EPISODES_PER_ITERATION
        rollout = collect_ppo_games(
            extension,
            model,
            start,
            PPO_EPISODES_PER_ITERATION,
            generator,
        )
        update = ppo_update(
            model, optimizer, rollout, anchor, generator, hard_anchor
        )
        record = {
            "iteration": iteration + 1,
            "seedStart": f"0x{start:08x}",
            "episodes": PPO_EPISODES_PER_ITERATION,
            "transitions": rollout.transitions,
            "meanTrainingScore": float(rollout.episode_scores.mean()),
            "meanTrainingMoves": float(rollout.episode_moves.mean()),
            "meanReward": float(rollout.rewards.mean()),
            "censored": rollout.censored,
            "collectionSeconds": rollout.elapsed_seconds,
            "transitionsPerSecond": rollout.transitions
            / max(rollout.elapsed_seconds, 1e-9),
            **update,
        }
        history.append(record)
        print("TORCH_RL_PPO", json.dumps(record, sort_keys=True), flush=True)
    return history


def run_direct_ppo_schedule(
    extension: Any,
    model: PublicActorCritic,
    generator: torch.Generator,
    optimizer: torch.optim.Optimizer,
    deadline: float,
    artifact: dict[str, Any],
) -> list[dict[str, Any]]:
    """Run all 32 fixed iterations without evaluation or checkpoint selection."""
    ranges = (
        (PPO_PILOT_SEED_START, PPO_PILOT_ITERATIONS),
        (PPO_CONTINUATION_SEED_START, sum(PPO_CONTINUATION_STAGES)),
    )
    history: list[dict[str, Any]] = []
    iteration = 0
    for range_start, range_iterations in ranges:
        for local_iteration in range(range_iterations):
            enforce_direct_ppo_resource_limits(deadline)
            seed_start = (
                range_start + local_iteration * PPO_EPISODES_PER_ITERATION
            )
            rollout = collect_ppo_games(
                extension,
                model,
                seed_start,
                PPO_EPISODES_PER_ITERATION,
                generator,
                deadline,
            )
            update = direct_ppo_update(
                model,
                optimizer,
                rollout,
                generator,
                deadline,
            )
            iteration += 1
            record = {
                "iteration": iteration,
                "seedStart": f"0x{seed_start:08x}",
                "seedEnd": (
                    f"0x{seed_start + PPO_EPISODES_PER_ITERATION - 1:08x}"
                ),
                "episodes": PPO_EPISODES_PER_ITERATION,
                "transitions": rollout.transitions,
                "meanTrainingScore": float(rollout.episode_scores.mean()),
                "meanTrainingMoves": float(rollout.episode_moves.mean()),
                "meanReward": float(rollout.rewards.mean()),
                "censored": rollout.censored,
                "collectionSeconds": rollout.elapsed_seconds,
                "transitionsPerSecond": rollout.transitions
                / max(rollout.elapsed_seconds, 1.0e-9),
                "peakRssBytes": peak_rss_bytes(),
                **update,
            }
            history.append(record)
            artifact["trainingHistory"] = history
            artifact["trainingIterationsCompleted"] = iteration
            artifact["trainingGamesCompleted"] = (
                iteration * PPO_EPISODES_PER_ITERATION
            )
            artifact["elapsedSeconds"] = (
                DIRECT_PPO_WALL_LIMIT_SECONDS
                - max(0.0, deadline - time.perf_counter())
            )
            save_artifact(artifact, DIRECT_PPO_ARTIFACT_PATH)
            print(
                "TORCH_RL_DIRECT_PPO",
                json.dumps(record, sort_keys=True),
                flush=True,
            )
    if iteration != DIRECT_PPO_ITERATIONS:
        raise RuntimeError(
            f"direct PPO completed {iteration}, expected {DIRECT_PPO_ITERATIONS}"
        )
    return history


def corpus_summary(corpus: TeacherCorpus) -> dict[str, Any]:
    return {
        "games": corpus.games,
        "states": corpus.states,
        "teacherWork": corpus.teacher_work,
        "elapsedSeconds": corpus.elapsed_seconds,
        "teacherWorkPerSecond": corpus.teacher_work
        / max(corpus.elapsed_seconds, 1e-9),
        "meanScore": corpus.mean_score,
        "meanMoves": corpus.mean_moves,
        "censored": corpus.censored,
    }


def save_artifact(
    payload: dict[str, Any], path: Path = ARTIFACT_PATH
) -> None:
    payload["resources"] = {
        "peakRssBytes": peak_rss_bytes(),
        "under512MiB": peak_rss_bytes() < MAXIMUM_RSS_BYTES,
    }
    payload["sourceHashes"] = {
        "trainer": sha256_file(Path(__file__).resolve()),
        "environment": sha256_file(ENV_SOURCE),
        "engine": sha256_file(ENGINE_HEADER),
        "cfpi": sha256_file(CFPI_HEADER),
    }
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def direct_ppo_math_self_test() -> dict[str, float | bool]:
    rewards = np.asarray([1.0, 2.0, 3.0, 4.0], dtype=np.float32)
    old_values = np.asarray([0.5, 1.0, 1.5, 2.0], dtype=np.float32)
    terminals = np.asarray([False, False, True, False])
    truncations = np.asarray([False, False, False, True])
    bootstraps = np.asarray([0.0, 0.0, 0.0, 5.0], dtype=np.float32)
    advantages, returns = compute_gae(
        rewards,
        old_values,
        terminals,
        truncations,
        bootstraps,
        [[0, 2], [1, 3]],
    )
    expected_advantages = np.asarray(
        [3.416225, 9.60984275, 1.5, 6.985], dtype=np.float32
    )
    expected_returns = np.asarray(
        [3.916225, 10.60984275, 3.0, 8.985], dtype=np.float32
    )
    gae_error = max(
        float(np.max(np.abs(advantages - expected_advantages))),
        float(np.max(np.abs(returns - expected_returns))),
    )
    if gae_error > 1.0e-6:
        raise RuntimeError(f"direct-PPO GAE fixture mismatch: {gae_error}")

    logits = torch.tensor(
        [
            [math.log(0.6), math.log(0.4), -1.0e9, -1.0e9, -1.0e9, -1.0e9, -1.0e9],
            [math.log(0.6), math.log(0.4), -1.0e9, -1.0e9, -1.0e9, -1.0e9, -1.0e9],
        ],
        dtype=torch.float32,
        requires_grad=True,
    )
    values = torch.tensor([1.3, 0.7], dtype=torch.float32, requires_grad=True)
    losses = direct_ppo_losses(
        logits,
        values,
        torch.tensor([0, 1], dtype=torch.int64),
        torch.full((2,), math.log(0.5), dtype=torch.float32),
        torch.ones(2, dtype=torch.float32),
        torch.tensor([1.0, -1.0], dtype=torch.float32),
        torch.tensor([2.0, 0.0], dtype=torch.float32),
    )
    expected = {
        "policy": -0.15,
        "value": 0.36125,
        "entropy": 0.6730116670092565,
        "approximate_kl": 0.020410997260127586,
        "clip_fraction": 1.0,
        "total": -0.06305255833504629,
    }
    observed = {
        "policy": float(losses.policy.detach()),
        "value": float(losses.value.detach()),
        "entropy": float(losses.entropy.detach()),
        "approximate_kl": float(losses.approximate_kl.detach()),
        "clip_fraction": float(losses.clip_fraction.detach()),
        "total": float(losses.total.detach()),
    }
    maximum_loss_error = max(abs(observed[key] - value) for key, value in expected.items())
    if maximum_loss_error > 1.0e-6:
        raise RuntimeError(
            f"direct-PPO clipped-loss fixture mismatch: {maximum_loss_error}"
        )
    losses.total.backward()
    if logits.grad is None or values.grad is None:
        raise RuntimeError("direct-PPO math fixture produced a missing gradient")
    if not torch.isfinite(logits.grad).all() or not torch.isfinite(values.grad).all():
        raise RuntimeError("direct-PPO math fixture produced a non-finite gradient")
    return {
        "passed": True,
        "gaeMaximumError": gae_error,
        "lossMaximumError": maximum_loss_error,
        "policyLoss": observed["policy"],
        "valueLoss": observed["value"],
        "entropy": observed["entropy"],
        "approximateKl": observed["approximate_kl"],
        "clipFraction": observed["clip_fraction"],
    }


def gradaccum_equivalence_fixture(samples: int) -> dict[str, float | int | bool]:
    if samples < 2 or samples > PPO_MINIBATCH:
        raise ValueError("equivalence fixture must fit one logical PPO batch")
    base = nn.Linear(5, 8)
    with torch.no_grad():
        base.weight.copy_(
            torch.arange(40, dtype=torch.float32).reshape(8, 5) / 200.0 - 0.1
        )
        base.bias.copy_(torch.arange(8, dtype=torch.float32) / 100.0 - 0.035)
    features = (
        torch.arange(samples * 5, dtype=torch.float32).reshape(samples, 5) % 31
    ) / 15.0 - 1.0
    actions = torch.arange(samples, dtype=torch.int64) % 7
    with torch.no_grad():
        initial = base(features)
        initial_logits = initial[:, :7]
        initial_values = initial[:, 7]
        selected = torch.log_softmax(initial_logits, dim=1).gather(
            1, actions[:, None]
        ).squeeze(1)
    offsets = (torch.arange(samples, dtype=torch.float32) % 9 - 4.0) / 50.0
    old_log_probabilities = selected + offsets
    old_values = initial_values + offsets * 0.5
    advantages = (torch.arange(samples, dtype=torch.float32) % 17) - 8.0
    advantages = (advantages - advantages.mean()) / advantages.std(
        unbiased=False
    ).clamp_min(1.0e-6)
    returns = old_values + (
        (torch.arange(samples, dtype=torch.float32) % 13) - 6.0
    ) / 5.0

    full = copy.deepcopy(base)
    accumulated = copy.deepcopy(base)
    full_optimizer = torch.optim.AdamW(
        full.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    accumulated_optimizer = torch.optim.AdamW(
        accumulated.parameters(),
        lr=PPO_LEARNING_RATE,
        eps=1.0e-5,
        weight_decay=1.0e-5,
    )

    full_output = full(features)
    full_losses = direct_ppo_losses(
        full_output[:, :7],
        full_output[:, 7],
        actions,
        old_log_probabilities,
        old_values,
        advantages,
        returns,
    )
    full_optimizer.zero_grad(set_to_none=True)
    full_losses.total.backward()
    full_gradients = [parameter.grad.detach().clone() for parameter in full.parameters()]
    full_norm = torch.nn.utils.clip_grad_norm_(full.parameters(), MAX_GRADIENT_NORM)
    full_optimizer.step()

    accumulated_optimizer.zero_grad(set_to_none=True)
    accumulated_metrics = torch.zeros(6, dtype=torch.float64)
    physical_chunks = 0
    for start in range(0, samples, GRADACCUM_PHYSICAL_MINIBATCH):
        stop = min(samples, start + GRADACCUM_PHYSICAL_MINIBATCH)
        weight = (stop - start) / samples
        output = accumulated(features[start:stop])
        losses = direct_ppo_losses(
            output[:, :7],
            output[:, 7],
            actions[start:stop],
            old_log_probabilities[start:stop],
            old_values[start:stop],
            advantages[start:stop],
            returns[start:stop],
        )
        (losses.total * weight).backward()
        accumulated_metrics += torch.tensor(
            [
                float(losses.total.detach()),
                float(losses.policy.detach()),
                float(losses.value.detach()),
                float(losses.entropy.detach()),
                float(losses.approximate_kl.detach()),
                float(losses.clip_fraction.detach()),
            ],
            dtype=torch.float64,
        ) * weight
        physical_chunks += 1
    accumulated_gradients = [
        parameter.grad.detach().clone() for parameter in accumulated.parameters()
    ]
    accumulated_norm = torch.nn.utils.clip_grad_norm_(
        accumulated.parameters(), MAX_GRADIENT_NORM
    )
    accumulated_optimizer.step()

    full_metrics = torch.tensor(
        [
            float(full_losses.total.detach()),
            float(full_losses.policy.detach()),
            float(full_losses.value.detach()),
            float(full_losses.entropy.detach()),
            float(full_losses.approximate_kl.detach()),
            float(full_losses.clip_fraction.detach()),
        ],
        dtype=torch.float64,
    )
    loss_error = float(torch.max(torch.abs(full_metrics - accumulated_metrics)))
    gradient_error = max(
        float(torch.max(torch.abs(first - second)))
        for first, second in zip(full_gradients, accumulated_gradients)
    )
    parameter_error = max(
        float(torch.max(torch.abs(first - second)))
        for first, second in zip(full.parameters(), accumulated.parameters())
    )
    optimizer_state_error = 0.0
    full_steps: list[int] = []
    accumulated_steps: list[int] = []
    for full_parameter, accumulated_parameter in zip(
        full.parameters(), accumulated.parameters()
    ):
        full_state = full_optimizer.state[full_parameter]
        accumulated_state = accumulated_optimizer.state[accumulated_parameter]
        full_steps.append(int(full_state["step"]))
        accumulated_steps.append(int(accumulated_state["step"]))
        for key in ("exp_avg", "exp_avg_sq"):
            optimizer_state_error = max(
                optimizer_state_error,
                float(
                    torch.max(
                        torch.abs(full_state[key] - accumulated_state[key])
                    )
                ),
            )
    norm_error = abs(float(full_norm) - float(accumulated_norm))
    passed = (
        loss_error <= 1.0e-6
        and gradient_error <= 2.0e-6
        and parameter_error <= 2.0e-6
        and optimizer_state_error <= 2.0e-6
        and norm_error <= 2.0e-6
        and full_steps == accumulated_steps
        and all(step == 1 for step in full_steps)
    )
    if not passed:
        raise RuntimeError(
            "gradient-accumulation equivalence fixture failed: "
            f"loss={loss_error}, gradient={gradient_error}, "
            f"parameter={parameter_error}, state={optimizer_state_error}, "
            f"norm={norm_error}"
        )
    return {
        "passed": True,
        "samples": samples,
        "physicalChunks": physical_chunks,
        "lossMaximumError": loss_error,
        "gradientMaximumError": gradient_error,
        "gradientNormError": norm_error,
        "parameterMaximumError": parameter_error,
        "optimizerStateMaximumError": optimizer_state_error,
        "optimizerSteps": full_steps[0],
    }


def rollouts_equal(first: RolloutBatch, second: RolloutBatch) -> bool:
    array_fields = (
        "boards",
        "discs",
        "phases",
        "legal_masks",
        "actions",
        "old_log_probabilities",
        "old_values",
        "advantages",
        "returns",
        "rewards",
        "episode_scores",
        "episode_moves",
    )
    return (
        first.censored == second.censored
        and first.transitions == second.transitions
        and all(
            np.array_equal(getattr(first, field), getattr(second, field))
            for field in array_fields
        )
    )


def self_test(extension: Any) -> dict[str, Any]:
    native = dict(extension.self_test())
    environment = extension.VectorEnvironment(
        SELF_TEST_GAMES,
        SELF_TEST_SEED_START,
        SELF_TEST_GAMES,
        100,
    )
    if any("seed" in name.lower() for name in dir(environment)):
        raise RuntimeError("Python environment exposes seed-named state")
    observation = environment.observations()
    teacher_one = environment.teacher_actions(2, 1)
    teacher_four = environment.teacher_actions(2, 4)
    for first, second in zip(teacher_one, teacher_four):
        if not np.array_equal(first, second):
            raise RuntimeError("parallel teacher is not deterministic")

    model = PublicActorCritic().eval()
    if parameter_count(model) > MAXIMUM_PARAMETERS:
        raise RuntimeError("deployment model exceeds frozen parameter bound")
    boards, discs, phases, masks, _ = observation
    mirrored_boards = np.flip(np.asarray(boards).reshape(-1, 7, 7), axis=2)
    mirrored_boards = np.ascontiguousarray(mirrored_boards.reshape(-1, 49))
    mirrored_masks = mirror_masks(np.asarray(masks))
    with torch.inference_mode():
        logits, values = model(
            torch.from_numpy(np.asarray(boards)),
            torch.from_numpy(np.asarray(discs)),
            torch.from_numpy(np.asarray(phases)),
            torch.from_numpy(np.asarray(masks)),
        )
        mirror_logits, mirror_values = model(
            torch.from_numpy(mirrored_boards),
            torch.from_numpy(np.asarray(discs)),
            torch.from_numpy(np.asarray(phases)),
            torch.from_numpy(mirrored_masks),
        )
    reflection_logit_error = float(
        torch.max(torch.abs(logits - torch.flip(mirror_logits, dims=(-1,))))
    )
    reflection_value_error = float(torch.max(torch.abs(values - mirror_values)))
    if reflection_logit_error > 1.0e-6 or reflection_value_error > 1.0e-6:
        raise RuntimeError("network reflection ensemble failed")
    for index, mask in enumerate(masks):
        for action in range(7):
            if not (int(mask) & (1 << action)) and logits[index, action] > -1.0e8:
                raise RuntimeError("illegal action was not masked")

    # Finite actor/critic gradient on a small public batch.
    model.train()
    test_actions = torch.from_numpy(np.asarray(teacher_one[0], dtype=np.int64))
    test_q = torch.from_numpy(np.asarray(teacher_one[1], dtype=np.float32))
    loss, _ = clone_loss(
        model,
        torch.from_numpy(np.asarray(boards)),
        torch.from_numpy(np.asarray(discs)),
        torch.from_numpy(np.asarray(phases)),
        torch.from_numpy(np.asarray(masks)),
        test_actions,
        test_q,
    )
    _, critic_values = model(
        torch.from_numpy(np.asarray(boards)),
        torch.from_numpy(np.asarray(discs)),
        torch.from_numpy(np.asarray(phases)),
        torch.from_numpy(np.asarray(masks)),
    )
    loss = loss + critic_values.square().mean()
    loss.backward()
    finite_gradient = all(
        parameter.grad is not None and torch.isfinite(parameter.grad).all()
        for parameter in model.parameters()
    )
    if not finite_gradient:
        raise RuntimeError("model self-test found a non-finite/missing gradient")
    model.zero_grad(set_to_none=True)
    model.eval()

    ppo_math = direct_ppo_math_self_test()
    gradaccum_full = gradaccum_equivalence_fixture(PPO_MINIBATCH)
    gradaccum_final = gradaccum_equivalence_fixture(777)
    smoke_seed = NETWORK_SEED ^ 0x5050_4F53
    smoke_one = collect_ppo_games(
        extension,
        model,
        SELF_TEST_SEED_START,
        SELF_TEST_GAMES,
        torch.Generator().manual_seed(smoke_seed),
    )
    smoke_two = collect_ppo_games(
        extension,
        model,
        SELF_TEST_SEED_START,
        SELF_TEST_GAMES,
        torch.Generator().manual_seed(smoke_seed),
    )
    smoke_deterministic = rollouts_equal(smoke_one, smoke_two)
    if not smoke_deterministic or smoke_one.censored:
        raise RuntimeError("direct-PPO already-opened-seed rollout smoke failed")
    update_one = copy.deepcopy(model)
    update_two = copy.deepcopy(model)
    initial_state = copy.deepcopy(model.state_dict())
    optimizer_one = torch.optim.AdamW(
        update_one.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    optimizer_two = torch.optim.AdamW(
        update_two.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    deadline = time.perf_counter() + 300.0
    update_metrics_one = direct_ppo_update(
        update_one,
        optimizer_one,
        smoke_one,
        torch.Generator().manual_seed(smoke_seed ^ 0x5550_4441),
        deadline,
    )
    update_metrics_two = direct_ppo_update(
        update_two,
        optimizer_two,
        smoke_two,
        torch.Generator().manual_seed(smoke_seed ^ 0x5550_4441),
        deadline,
    )
    update_deterministic = all(
        torch.equal(update_one.state_dict()[name], update_two.state_dict()[name])
        for name in update_one.state_dict()
    )
    actor_changed = not torch.equal(
        initial_state["policy.weight"], update_one.state_dict()["policy.weight"]
    )
    critic_changed = not torch.equal(
        initial_state["value.weight"], update_one.state_dict()["value.weight"]
    )
    comparable_metric_keys = (
        "policyLoss",
        "valueLoss",
        "entropy",
        "approximateKl",
        "clipFraction",
        "optimizerUpdates",
    )
    metric_deterministic = all(
        update_metrics_one[key] == update_metrics_two[key]
        for key in comparable_metric_keys
    )
    if not (
        update_deterministic
        and metric_deterministic
        and actor_changed
        and critic_changed
    ):
        raise RuntimeError("direct-PPO deterministic optimizer smoke failed")

    corpus = TeacherCorpus(
        boards=np.asarray(boards, dtype=np.uint8).copy(),
        discs=np.asarray(discs, dtype=np.uint8).copy(),
        phases=np.asarray(phases, dtype=np.uint8).copy(),
        legal_masks=np.asarray(masks, dtype=np.uint8).copy(),
        actions=np.asarray(teacher_one[0], dtype=np.int64).copy(),
        q_values=np.asarray(teacher_one[1], dtype=np.float32).copy(),
        games=SELF_TEST_GAMES,
        states=SELF_TEST_GAMES,
        teacher_work=int(np.asarray(teacher_one[2]).sum()),
        elapsed_seconds=0,
        mean_score=0,
        mean_moves=0,
        censored=0,
    )
    export = export_public_policy(
        model,
        corpus,
        Path("/tmp/drop7-torch-selftest-policy.json"),
        Path("/tmp/drop7-torch-selftest-policy.f32"),
        Path("/tmp/drop7-torch-selftest-golden.json"),
        Path("/tmp/drop7-torch-selftest-policy.ts"),
    )
    # The rollout adapter's natural common-random batch is seven root actions
    # crossed with seven scenarios.  It must remain legal and equivariant.
    repeated = np.arange(49) % SELF_TEST_GAMES
    adapter = PublicPolicy(model)
    continuation_actions = adapter.actions(
        corpus.boards[repeated],
        corpus.discs[repeated],
        corpus.phases[repeated],
        corpus.legal_masks[repeated],
    )
    continuation_legal = all(
        int(corpus.legal_masks[source]) & (1 << int(action))
        for source, action in zip(repeated, continuation_actions)
    )
    if not continuation_legal:
        raise RuntimeError("49-state h25 continuation batch chose illegal action")
    direct_gate_test = direct_ppo_gate_self_test()
    assert_fresh_training_batch(
        FRESH_DIRECT_PPO_TRAINING_SEED_START, PPO_EPISODES_PER_ITERATION
    )
    assert_fresh_training_batch(
        FRESH_DIRECT_PPO_TRAINING_SEED_START
        + (DIRECT_PPO_ITERATIONS - 1) * PPO_EPISODES_PER_ITERATION,
        PPO_EPISODES_PER_ITERATION,
    )
    fresh_seed_rejection = False
    try:
        assert_fresh_training_batch(0x3D34_0000, PPO_EPISODES_PER_ITERATION)
    except RuntimeError:
        fresh_seed_rejection = True
    if not fresh_seed_rejection:
        raise RuntimeError("fresh-process seed guard accepted rejected 0x3d34 data")
    result = {
        "passed": True,
        "native": native,
        "levelBonus": int(extension.level_bonus),
        "parameterCount": parameter_count(model),
        "parameterBytes": parameter_count(model) * 4,
        "reflectionMaximumLogitError": reflection_logit_error,
        "reflectionMaximumValueError": reflection_value_error,
        "finiteGradient": finite_gradient,
        "directPpoMath": ppo_math,
        "gradaccum256FullLogicalBatch": gradaccum_full,
        "gradaccum256FinalLogicalBatch": gradaccum_final,
        "directPpoSmokeGames": SELF_TEST_GAMES,
        "directPpoSmokeTransitions": smoke_one.transitions,
        "directPpoSmokeCensored": smoke_one.censored,
        "directPpoSmokeDeterministic": smoke_deterministic,
        "directPpoUpdateDeterministic": update_deterministic,
        "directPpoMetricDeterministic": metric_deterministic,
        "directPpoActorChanged": actor_changed,
        "directPpoCriticChanged": critic_changed,
        "directPpoGate": direct_gate_test,
        "freshProcessSeedGuard": True,
        "freshProcessRejected3d34": fresh_seed_rejection,
        "parallelTeacherDeterministic": True,
        "seedAttributesExposed": False,
        "h25ContinuationBatch": 49,
        "h25ContinuationLegal": continuation_legal,
        "export": export,
        "peakRssBytes": peak_rss_bytes(),
    }
    print("TORCH_RL_SELF_TEST", json.dumps(result, sort_keys=True), flush=True)
    return result


def run_pilot(extension: Any) -> dict[str, Any]:
    started = time.perf_counter()
    artifact: dict[str, Any] = {
        "format": "drop7-pytorch-public-actor-critic-pilot-v1",
        "status": "running",
        "levelBonus": int(extension.level_bonus),
        "architecture": architecture_manifest(PublicActorCritic()),
        "protocolFrozenBeforeGameplay": True,
        "parameterSweep": False,
        "priorInterruptedReplay": {
            "reason": "seed ownership was narrowed before any conflicting range was opened",
            "cloneTrainingCohortCompletedOnce": True,
            "validationCohortGamesPartiallyAdvanced": 256,
            "validationDecisionsPerGameBeforeInterrupt": 25,
            "validationResultOrGateObserved": False,
            "action": "deterministically replay the same registered full cohorts",
            "conflictingSeedOpened": False,
        },
        "reward": {
            "formula": "scoreDelta/17000 + 0.02 + 0.005*clears + 0.005*reveals - 1.0*terminal",
            "scoreScale": EXPECTED_LEVEL_BONUS,
            "survival": SURVIVAL_REWARD,
            "numberedClear": CLEAR_REWARD,
            "coverReveal": REVEAL_REWARD,
            "terminal": TERMINAL_PENALTY,
            "gamma": GAMMA,
            "gaeLambda": GAE_LAMBDA,
        },
        "seedRanges": {
            "selfTest": ["0x3d300000", "0x3d300043"],
            "cloneTraining": [
                f"0x{CLONE_TRAIN_SEED_START:08x}",
                f"0x{CLONE_TRAIN_SEED_START + CLONE_TRAIN_GAMES - 1:08x}",
            ],
            "cloneValidation": [
                f"0x{CLONE_VALIDATION_SEED_START:08x}",
                f"0x{CLONE_VALIDATION_SEED_START + CLONE_VALIDATION_GAMES - 1:08x}",
            ],
            "dagger": [
                f"0x{DAGGER_SEED_START:08x}",
                f"0x{DAGGER_SEED_START + DAGGER_GAMES - 1:08x}",
            ],
            "ppoPilot": [
                f"0x{PPO_PILOT_SEED_START:08x}",
                f"0x{PPO_PILOT_SEED_START + PPO_PILOT_ITERATIONS * PPO_EPISODES_PER_ITERATION - 1:08x}",
            ],
            "ppoContinuation": [
                f"0x{PPO_CONTINUATION_SEED_START:08x}",
                f"0x{PPO_CONTINUATION_SEED_START + sum(PPO_CONTINUATION_STAGES) * PPO_EPISODES_PER_ITERATION - 1:08x}",
            ],
            "development": [
                f"0x{DEVELOPMENT_SEED_START:08x}",
                f"0x{DEVELOPMENT_SEED_START + DEVELOPMENT_GAMES - 1:08x}",
            ],
            "confirmation": [
                f"0x{CONFIRMATION_SEED_START:08x}",
                f"0x{CONFIRMATION_SEED_START + CONFIRMATION_GAMES - 1:08x}",
            ],
            "forbiddenOpened": False,
        },
        "gates": {
            "minimumD2ValidationAgreement": MINIMUM_D2_VALIDATION_AGREEMENT,
            "warmRandomScoreAndMoveRatio": WARM_RANDOM_RATIO,
            "warmD1ScoreAndMoveRatio": WARM_D1_RATIO,
            "pilotRandomScoreAndMoveRatio": PILOT_RANDOM_RATIO,
            "pilotD1ScoreAndMoveRatio": PILOT_D1_RATIO,
            "pilotD2ScoreAndMoveRatio": PILOT_D2_RATIO,
            "pilotCloneRetentionScoreAndMoveRatio": PILOT_CLONE_RETENTION,
            "continuationRetention": CONTINUATION_RETENTION,
            "targetMeanScore": TARGET_SCORE,
        },
    }
    save_artifact(artifact)
    model = PublicActorCritic().eval()
    if parameter_count(model) > MAXIMUM_PARAMETERS:
        raise RuntimeError("frozen network exceeded parameter bound")

    training = collect_teacher_corpus(
        extension,
        CLONE_TRAIN_SEED_START,
        CLONE_TRAIN_GAMES,
        "teacher",
    )
    validation = collect_teacher_corpus(
        extension,
        CLONE_VALIDATION_SEED_START,
        CLONE_VALIDATION_GAMES,
        "teacher",
    )
    if training.censored or validation.censored:
        raise RuntimeError("D2 behavior corpus contained capped games")
    clone_history = train_clone(
        model,
        training,
        validation,
        CLONE_EPOCHS,
        CLONE_LEARNING_RATE,
        "teacher",
    )
    dagger = collect_teacher_corpus(
        extension,
        DAGGER_SEED_START,
        DAGGER_GAMES,
        "student",
        model,
    )
    if dagger.censored:
        raise RuntimeError("D2 DAgger corpus contained capped games")
    combined = concatenate_corpora(training, dagger)
    dagger_history = train_clone(
        model,
        combined,
        validation,
        DAGGER_EPOCHS,
        DAGGER_LEARNING_RATE,
        "dagger",
    )
    validation_metrics = clone_metrics(model, validation)
    save_checkpoint(CLONE_CHECKPOINT, model, "d2-clone", 0)
    clone_export = export_public_policy(model, validation)

    random_result = evaluate_policy(
        extension, "random", DEVELOPMENT_SEED_START, DEVELOPMENT_GAMES
    )
    d1_result = evaluate_policy(
        extension, "d1", DEVELOPMENT_SEED_START, DEVELOPMENT_GAMES
    )
    d2_result = evaluate_policy(
        extension, "d2", DEVELOPMENT_SEED_START, DEVELOPMENT_GAMES
    )
    clone_result = evaluate_policy(
        extension,
        "network",
        DEVELOPMENT_SEED_START,
        DEVELOPMENT_GAMES,
        model,
    )
    passed_warm_gate = warm_gate(
        validation_metrics, clone_result, random_result, d1_result
    )
    artifact.update(
        {
            "teacherCorpora": {
                "training": corpus_summary(training),
                "validation": corpus_summary(validation),
                "dagger": corpus_summary(dagger),
                "combinedTraining": corpus_summary(combined),
            },
            "cloneTraining": {
                "teacherHistory": clone_history,
                "daggerHistory": dagger_history,
                "finalValidation": validation_metrics,
                "checkpoint": str(CLONE_CHECKPOINT),
                "checkpointBytes": CLONE_CHECKPOINT.stat().st_size,
                "checkpointSha256": sha256_file(CLONE_CHECKPOINT),
                "export": clone_export,
            },
            "development": {
                "random": random_result.summary(),
                "d1": d1_result.summary(),
                "d2": d2_result.summary(),
                "clone": clone_result.summary(),
            },
            "warmGatePassed": passed_warm_gate,
        }
    )
    if not passed_warm_gate:
        artifact["status"] = "warm-start-gate-rejected"
        artifact["decision"] = (
            "diagnose one concrete defect before at most one corrected run; PPO not run"
        )
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact)
        print("TORCH_RL_DECISION", json.dumps({"status": artifact["status"]}))
        return artifact

    generator = torch.Generator().manual_seed(POLICY_SAMPLE_SEED)
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    ppo_history = run_ppo_iterations(
        extension,
        model,
        combined,
        PPO_PILOT_SEED_START,
        PPO_PILOT_ITERATIONS,
        0,
        generator,
        optimizer,
    )
    save_checkpoint(PILOT_CHECKPOINT, model, "ppo-pilot", PPO_PILOT_ITERATIONS)
    pilot_result = evaluate_policy(
        extension,
        "network",
        DEVELOPMENT_SEED_START,
        DEVELOPMENT_GAMES,
        model,
    )
    passed_pilot_gate = pilot_gate(
        pilot_result,
        clone_result,
        random_result,
        d1_result,
        d2_result,
    )
    artifact["ppoPilot"] = {
        "history": ppo_history,
        "evaluation": pilot_result.summary(),
        "gatePassed": passed_pilot_gate,
        "checkpoint": str(PILOT_CHECKPOINT),
        "checkpointBytes": PILOT_CHECKPOINT.stat().st_size,
        "checkpointSha256": sha256_file(PILOT_CHECKPOINT),
    }
    if not passed_pilot_gate:
        artifact["status"] = "ppo-pilot-gate-rejected"
        artifact["decision"] = (
            "diagnose one concrete defect before at most one corrected run; continuation not run"
        )
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact)
        print("TORCH_RL_DECISION", json.dumps({"status": artifact["status"]}))
        return artifact

    best_state = copy.deepcopy(model.state_dict())
    best_evaluation = pilot_result
    continuation_history: list[dict[str, Any]] = []
    evaluations: list[dict[str, Any]] = []
    iterations_completed = PPO_PILOT_ITERATIONS
    continuation_cursor = PPO_CONTINUATION_SEED_START
    for stage_iterations in PPO_CONTINUATION_STAGES:
        stage_history = run_ppo_iterations(
            extension,
            model,
            combined,
            continuation_cursor,
            stage_iterations,
            iterations_completed,
            generator,
            optimizer,
        )
        continuation_history.extend(stage_history)
        iterations_completed += stage_iterations
        continuation_cursor += stage_iterations * PPO_EPISODES_PER_ITERATION
        evaluation = evaluate_policy(
            extension,
            "network",
            DEVELOPMENT_SEED_START,
            DEVELOPMENT_GAMES,
            model,
        )
        retained = ratio_at_least(
            evaluation, best_evaluation, CONTINUATION_RETENTION
        )
        evaluations.append(
            {
                "iterations": iterations_completed,
                "evaluation": evaluation.summary(),
                "retained": retained,
            }
        )
        if retained and evaluation.scores.mean() > best_evaluation.scores.mean():
            best_state = copy.deepcopy(model.state_dict())
            best_evaluation = evaluation
        if not retained or best_evaluation.scores.mean() >= TARGET_SCORE:
            break
    model.load_state_dict(best_state)
    save_checkpoint(
        CONTINUATION_CHECKPOINT, model, "ppo-continuation", iterations_completed
    )
    confirmation = evaluate_policy(
        extension,
        "network",
        CONFIRMATION_SEED_START,
        CONFIRMATION_GAMES,
        model,
    )
    final_export = export_public_policy(model, validation)
    artifact["continuation"] = {
        "history": continuation_history,
        "developmentEvaluations": evaluations,
        "selectedDevelopment": best_evaluation.summary(),
        "confirmation": confirmation.summary(),
        "checkpoint": str(CONTINUATION_CHECKPOINT),
        "checkpointBytes": CONTINUATION_CHECKPOINT.stat().st_size,
        "checkpointSha256": sha256_file(CONTINUATION_CHECKPOINT),
        "export": final_export,
    }
    artifact["status"] = (
        "million-point-target-passed"
        if confirmation.scores.mean() >= TARGET_SCORE
        else "positive-staged-continuation-complete"
    )
    artifact["decision"] = (
        "qualification target reached"
        if confirmation.scores.mean() >= TARGET_SCORE
        else "retain compact policy and investigate h25 clone continuation"
    )
    artifact["elapsedSeconds"] = time.perf_counter() - started
    save_artifact(artifact)
    print("TORCH_RL_DECISION", json.dumps({"status": artifact["status"]}))
    return artifact


def run_qualified_ppo(
    extension: Any,
    model: PublicActorCritic,
    anchor: TeacherCorpus,
    clone_result: Evaluation,
    random_result: Evaluation,
    d1_result: Evaluation,
    d2_result: Evaluation,
    hard_anchor: bool,
) -> dict[str, Any]:
    generator = torch.Generator().manual_seed(POLICY_SAMPLE_SEED)
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    ppo_history = run_ppo_iterations(
        extension,
        model,
        anchor,
        PPO_PILOT_SEED_START,
        PPO_PILOT_ITERATIONS,
        0,
        generator,
        optimizer,
        hard_anchor,
    )
    save_checkpoint(PILOT_CHECKPOINT, model, "ppo-pilot", PPO_PILOT_ITERATIONS)
    pilot_result = evaluate_policy(
        extension,
        "network",
        DEVELOPMENT_SEED_START,
        DEVELOPMENT_GAMES,
        model,
    )
    passed_pilot_gate = pilot_gate(
        pilot_result,
        clone_result,
        random_result,
        d1_result,
        d2_result,
    )
    result: dict[str, Any] = {
        "status": (
            "ppo-pilot-gate-passed"
            if passed_pilot_gate
            else "ppo-pilot-gate-rejected"
        ),
        "decision": (
            "continue in frozen stages"
            if passed_pilot_gate
            else "stop; the one corrected path is exhausted"
        ),
        "ppoPilot": {
            "history": ppo_history,
            "evaluation": pilot_result.summary(),
            "gatePassed": passed_pilot_gate,
            "hardImitationAnchor": hard_anchor,
            "checkpoint": str(PILOT_CHECKPOINT),
            "checkpointBytes": PILOT_CHECKPOINT.stat().st_size,
            "checkpointSha256": sha256_file(PILOT_CHECKPOINT),
        },
    }
    if not passed_pilot_gate:
        return result

    best_state = copy.deepcopy(model.state_dict())
    best_evaluation = pilot_result
    continuation_history: list[dict[str, Any]] = []
    evaluations: list[dict[str, Any]] = []
    iterations_completed = PPO_PILOT_ITERATIONS
    continuation_cursor = PPO_CONTINUATION_SEED_START
    for stage_iterations in PPO_CONTINUATION_STAGES:
        stage_history = run_ppo_iterations(
            extension,
            model,
            anchor,
            continuation_cursor,
            stage_iterations,
            iterations_completed,
            generator,
            optimizer,
            hard_anchor,
        )
        continuation_history.extend(stage_history)
        iterations_completed += stage_iterations
        continuation_cursor += stage_iterations * PPO_EPISODES_PER_ITERATION
        evaluation = evaluate_policy(
            extension,
            "network",
            DEVELOPMENT_SEED_START,
            DEVELOPMENT_GAMES,
            model,
        )
        retained = ratio_at_least(
            evaluation, best_evaluation, CONTINUATION_RETENTION
        )
        evaluations.append(
            {
                "iterations": iterations_completed,
                "evaluation": evaluation.summary(),
                "retained": retained,
            }
        )
        if retained and evaluation.scores.mean() > best_evaluation.scores.mean():
            best_state = copy.deepcopy(model.state_dict())
            best_evaluation = evaluation
        if not retained or best_evaluation.scores.mean() >= TARGET_SCORE:
            break
    model.load_state_dict(best_state)
    save_checkpoint(
        CONTINUATION_CHECKPOINT, model, "ppo-continuation", iterations_completed
    )
    confirmation = evaluate_policy(
        extension,
        "network",
        CONFIRMATION_SEED_START,
        CONFIRMATION_GAMES,
        model,
    )
    # The caller supplies a validation corpus separately for its final export;
    # this helper owns only policy optimization and gate evaluation.
    result["continuation"] = {
        "history": continuation_history,
        "developmentEvaluations": evaluations,
        "selectedDevelopment": best_evaluation.summary(),
        "confirmation": confirmation.summary(),
        "checkpoint": str(CONTINUATION_CHECKPOINT),
        "checkpointBytes": CONTINUATION_CHECKPOINT.stat().st_size,
        "checkpointSha256": sha256_file(CONTINUATION_CHECKPOINT),
    }
    result["status"] = (
        "million-point-target-passed"
        if confirmation.scores.mean() >= TARGET_SCORE
        else "positive-staged-continuation-complete"
    )
    result["decision"] = (
        "qualification target reached"
        if confirmation.scores.mean() >= TARGET_SCORE
        else "retain compact policy and investigate h25 clone continuation"
    )
    return result


def run_corrected(extension: Any) -> dict[str, Any]:
    if not ARTIFACT_PATH.exists() or not CLONE_CHECKPOINT.exists():
        raise RuntimeError("corrected run requires the rejected frozen pilot")
    prior = json.loads(ARTIFACT_PATH.read_text(encoding="utf-8"))
    if prior.get("status") != "warm-start-gate-rejected":
        raise RuntimeError("corrected run is allowed only after warm-gate rejection")
    started = time.perf_counter()
    artifact: dict[str, Any] = {
        "format": "drop7-pytorch-public-actor-critic-corrected-v1",
        "status": "running",
        "levelBonus": int(extension.level_bonus),
        "architecture": architecture_manifest(PublicActorCritic()),
        "singleCorrection": True,
        "parameterSweep": False,
        "newGameplaySeed": False,
        "defect": {
            "name": "full-range soft-Q dilution",
            "description": (
                "Thirty percent of the rejected clone loss matched a soft target "
                "normalized by each root's complete Q range. A catastrophic terminal "
                "alternative can expand that range and flatten distinctions among "
                "surviving actions, weakening the hard greedy margin used by the "
                "frozen agreement gate and rollout policy."
            ),
            "rejectedValidationAgreement": prior["cloneTraining"][
                "finalValidation"
            ]["agreement"],
            "rejectedValidationTop2": prior["cloneTraining"][
                "finalValidation"
            ]["top2"],
            "rejectedValidationCrossEntropy": prior["cloneTraining"][
                "finalValidation"
            ]["crossEntropy"],
        },
        "correctionFrozenBeforeReplay": {
            "resumeCheckpoint": str(CLONE_CHECKPOINT),
            "resumeCheckpointSha256": sha256_file(CLONE_CHECKPOINT),
            "trainingSeeds": "0x3d310000..0x3d3102ff (identical replay)",
            "validationSeeds": "0x3d320000..0x3d3200ff (identical replay)",
            "daggerReplay": False,
            "epochs": CORRECTED_HARD_EPOCHS,
            "loss": "pure hard teacher-action cross-entropy",
            "learningRate": CLONE_LEARNING_RATE,
            "batchSize": CLONE_BATCH_SIZE,
            "unchangedWarmGate": True,
        },
        "seedBoundary": {
            "allowed": "0x3d300000..0x3d3fffff",
            "conflictingStartsRejectedByCompiledTest": [
                "0x3d400000",
                "0x3d500000",
                "0x3d600000",
            ],
            "forbiddenOpened": False,
        },
        "gates": prior["gates"],
        "reward": prior["reward"],
    }
    save_artifact(artifact, CORRECTED_ARTIFACT_PATH)

    model = load_model(CLONE_CHECKPOINT)
    training = collect_teacher_corpus(
        extension,
        CLONE_TRAIN_SEED_START,
        CLONE_TRAIN_GAMES,
        "teacher",
    )
    validation = collect_teacher_corpus(
        extension,
        CLONE_VALIDATION_SEED_START,
        CLONE_VALIDATION_GAMES,
        "teacher",
    )
    if training.states != 56_484 or validation.states != 17_951:
        raise RuntimeError("corrected replay did not reproduce frozen corpus counts")
    if training.censored or validation.censored:
        raise RuntimeError("corrected replay unexpectedly censored a game")
    diagnostic = soft_target_diagnostic(training)
    history = train_hard_clone(model, training, validation)
    validation_metrics = clone_metrics(model, validation)
    save_checkpoint(CORRECTED_CLONE_CHECKPOINT, model, "corrected-d2-clone", 0)
    corrected_export = export_public_policy(model, validation)

    random_result = evaluate_policy(
        extension, "random", DEVELOPMENT_SEED_START, DEVELOPMENT_GAMES
    )
    d1_result = evaluate_policy(
        extension, "d1", DEVELOPMENT_SEED_START, DEVELOPMENT_GAMES
    )
    d2_result = evaluate_policy(
        extension, "d2", DEVELOPMENT_SEED_START, DEVELOPMENT_GAMES
    )
    clone_result = evaluate_policy(
        extension,
        "network",
        DEVELOPMENT_SEED_START,
        DEVELOPMENT_GAMES,
        model,
    )
    passed_warm_gate = warm_gate(
        validation_metrics, clone_result, random_result, d1_result
    )
    artifact.update(
        {
            "softTargetDilutionDiagnostic": diagnostic,
            "replayedCorpora": {
                "training": corpus_summary(training),
                "validation": corpus_summary(validation),
            },
            "correctedClone": {
                "history": history,
                "finalValidation": validation_metrics,
                "checkpoint": str(CORRECTED_CLONE_CHECKPOINT),
                "checkpointBytes": CORRECTED_CLONE_CHECKPOINT.stat().st_size,
                "checkpointSha256": sha256_file(CORRECTED_CLONE_CHECKPOINT),
                "export": corrected_export,
            },
            "development": {
                "random": random_result.summary(),
                "d1": d1_result.summary(),
                "d2": d2_result.summary(),
                "correctedClone": clone_result.summary(),
            },
            "warmGatePassed": passed_warm_gate,
        }
    )
    if not passed_warm_gate:
        artifact["status"] = "single-corrected-warm-gate-rejected"
        artifact["decision"] = "stop; no PPO and no second correction"
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact, CORRECTED_ARTIFACT_PATH)
        print("TORCH_RL_CORRECTED_DECISION", json.dumps({"status": artifact["status"]}))
        return artifact

    ppo = run_qualified_ppo(
        extension,
        model,
        training,
        clone_result,
        random_result,
        d1_result,
        d2_result,
        True,
    )
    artifact.update(ppo)
    if "continuation" in artifact:
        artifact["continuation"]["export"] = export_public_policy(
            model, validation
        )
    artifact["elapsedSeconds"] = time.perf_counter() - started
    save_artifact(artifact, CORRECTED_ARTIFACT_PATH)
    print("TORCH_RL_CORRECTED_DECISION", json.dumps({"status": artifact["status"]}))
    return artifact


def public_export_fixture(extension: Any) -> TeacherCorpus:
    """Create a golden export batch only from the fixed self-test range."""
    environment = extension.VectorEnvironment(
        SELF_TEST_GAMES,
        SELF_TEST_SEED_START,
        SELF_TEST_GAMES,
        100,
    )
    boards, discs, phases, masks, _ = environment.observations()
    masks_array = np.asarray(masks, dtype=np.uint8).copy()
    actions = np.empty(SELF_TEST_GAMES, dtype=np.int64)
    q_values = np.full((SELF_TEST_GAMES, 7), -1.0e30, dtype=np.float32)
    for index, mask in enumerate(masks_array):
        legal = [action for action in range(7) if int(mask) & (1 << action)]
        if not legal:
            raise RuntimeError("export fixture unexpectedly has no legal action")
        actions[index] = legal[0]
        q_values[index, legal] = 0.0
    return TeacherCorpus(
        boards=np.asarray(boards, dtype=np.uint8).copy(),
        discs=np.asarray(discs, dtype=np.uint8).copy(),
        phases=np.asarray(phases, dtype=np.uint8).copy(),
        legal_masks=masks_array,
        actions=actions,
        q_values=q_values,
        games=SELF_TEST_GAMES,
        states=SELF_TEST_GAMES,
        teacher_work=0,
        elapsed_seconds=0.0,
        mean_score=0.0,
        mean_moves=0.0,
        censored=0,
    )


def paired_score_lower_bound(
    candidate: Evaluation, baseline: Evaluation
) -> tuple[float, float, float]:
    if candidate.scores.shape != baseline.scores.shape:
        raise ValueError("paired evaluations must contain the same games")
    differences = candidate.scores.astype(np.float64) - baseline.scores.astype(
        np.float64
    )
    if differences.size < 2:
        raise ValueError("paired lower bound requires at least two games")
    mean = float(differences.mean())
    standard_error = float(differences.std(ddof=1) / math.sqrt(differences.size))
    lower_bound = mean - DIRECT_PPO_PAIRED_T_CRITICAL * standard_error
    return mean, standard_error, lower_bound


def direct_ppo_development_gate(
    candidate: Evaluation,
    clone: Evaluation,
    d2: Evaluation,
    d1: Evaluation,
    random: Evaluation,
) -> dict[str, Any]:
    paired_mean, paired_standard_error, paired_lower_bound = (
        paired_score_lower_bound(candidate, clone)
    )
    clone_score_ratio = float(candidate.scores.mean() / clone.scores.mean())
    clone_move_ratio = float(candidate.moves.mean() / clone.moves.mean())
    d2_score_ratio = float(candidate.scores.mean() / d2.scores.mean())
    d2_move_ratio = float(candidate.moves.mean() / d2.moves.mean())
    zero_censoring = all(
        result.censored.sum() == 0
        for result in (candidate, clone, d2, d1, random)
    )
    passed = (
        clone_score_ratio >= DIRECT_PPO_CLONE_RATIO
        and clone_move_ratio >= DIRECT_PPO_CLONE_RATIO
        and d2_score_ratio >= DIRECT_PPO_D2_RATIO
        and d2_move_ratio >= DIRECT_PPO_D2_RATIO
        and paired_lower_bound > 0.0
        and zero_censoring
    )
    return {
        "passed": passed,
        "zeroCensoring": zero_censoring,
        "cloneScoreRatio": clone_score_ratio,
        "cloneMoveRatio": clone_move_ratio,
        "minimumCloneScoreAndMoveRatio": DIRECT_PPO_CLONE_RATIO,
        "d2ScoreRatio": d2_score_ratio,
        "d2MoveRatio": d2_move_ratio,
        "minimumD2ScoreAndMoveRatio": DIRECT_PPO_D2_RATIO,
        "pairedCandidateMinusCloneMeanScore": paired_mean,
        "pairedCandidateMinusCloneStandardError": paired_standard_error,
        "pairedCandidateMinusCloneOneSided95LowerBound": paired_lower_bound,
        "pairedStudentTCritical": DIRECT_PPO_PAIRED_T_CRITICAL,
        "pairedDegreesOfFreedom": int(candidate.scores.size - 1),
    }


def direct_ppo_gate_self_test() -> dict[str, float | bool]:
    games = DIRECT_PPO_DEVELOPMENT_GAMES

    def fixture(score: np.ndarray, moves: np.ndarray) -> Evaluation:
        return Evaluation(
            scores=score.astype(np.int64),
            moves=moves.astype(np.int32),
            cleared=moves.astype(np.int64),
            revealed=moves.astype(np.int64),
            maximum_chain=np.ones(games, dtype=np.int32),
            censored=np.zeros(games, dtype=np.uint8),
            elapsed_seconds=0.0,
            decisions=int(moves.sum()),
        )

    offsets = np.arange(games, dtype=np.int64) * 100
    clone = fixture(100_000 + offsets, np.full(games, 50))
    d2 = fixture(125_000 + offsets, np.full(games, 60))
    d1 = fixture(80_000 + offsets, np.full(games, 45))
    random = fixture(50_000 + offsets, np.full(games, 30))
    passing_candidate = fixture(150_000 + offsets, np.full(games, 65))
    passing = direct_ppo_development_gate(
        passing_candidate, clone, d2, d1, random
    )
    failing = direct_ppo_development_gate(clone, clone, d2, d1, random)
    if not passing["passed"] or failing["passed"]:
        raise RuntimeError("direct-PPO development gate fixture failed")
    if passing["pairedCandidateMinusCloneOneSided95LowerBound"] <= 0.0:
        raise RuntimeError("direct-PPO paired lower-bound fixture failed")
    return {
        "passed": True,
        "acceptFixture": bool(passing["passed"]),
        "rejectFixture": not bool(failing["passed"]),
        "acceptPairedLowerBound": float(
            passing["pairedCandidateMinusCloneOneSided95LowerBound"]
        ),
    }


def evaluate_direct_ppo_development(
    extension: Any,
    model: PublicActorCritic,
    deadline: float,
) -> dict[str, Any]:
    """Read the sole paired cohort once for the fixed candidate and baselines."""
    results: dict[str, Evaluation] = {}
    results["candidate"] = evaluate_policy(
        extension,
        "network",
        DIRECT_PPO_DEVELOPMENT_SEED_START,
        DIRECT_PPO_DEVELOPMENT_GAMES,
        model,
    )
    enforce_direct_ppo_resource_limits(deadline)
    results["originalClone"] = evaluate_policy(
        extension,
        "network",
        DIRECT_PPO_DEVELOPMENT_SEED_START,
        DIRECT_PPO_DEVELOPMENT_GAMES,
        load_model(CLONE_CHECKPOINT),
    )
    enforce_direct_ppo_resource_limits(deadline)
    results["fairD2"] = evaluate_policy(
        extension,
        "d2",
        DIRECT_PPO_DEVELOPMENT_SEED_START,
        DIRECT_PPO_DEVELOPMENT_GAMES,
    )
    enforce_direct_ppo_resource_limits(deadline)
    results["fairD1"] = evaluate_policy(
        extension,
        "d1",
        DIRECT_PPO_DEVELOPMENT_SEED_START,
        DIRECT_PPO_DEVELOPMENT_GAMES,
    )
    enforce_direct_ppo_resource_limits(deadline)
    results["random"] = evaluate_policy(
        extension,
        "random",
        DIRECT_PPO_DEVELOPMENT_SEED_START,
        DIRECT_PPO_DEVELOPMENT_GAMES,
    )
    enforce_direct_ppo_resource_limits(deadline)
    gate = direct_ppo_development_gate(
        results["candidate"],
        results["originalClone"],
        results["fairD2"],
        results["fairD1"],
        results["random"],
    )
    return {
        **{name: result.summary() for name, result in results.items()},
        "gate": gate,
    }


def run_direct_ppo(extension: Any) -> dict[str, Any]:
    """Run the fixed anchor-free direct-score PPO configuration."""
    if not ARTIFACT_PATH.exists() or not CLONE_CHECKPOINT.exists():
        raise RuntimeError("direct PPO requires the original rejected clone pilot")
    prior = json.loads(ARTIFACT_PATH.read_text(encoding="utf-8"))
    if prior.get("status") != "warm-start-gate-rejected":
        raise RuntimeError("direct PPO requires the frozen rejected pilot artifact")
    clone_hash = sha256_file(CLONE_CHECKPOINT)
    if clone_hash != ORIGINAL_CLONE_SHA256:
        raise RuntimeError(
            f"original clone hash changed: {clone_hash}, expected {ORIGINAL_CLONE_SHA256}"
        )
    started = time.perf_counter()
    deadline = started + DIRECT_PPO_WALL_LIMIT_SECONDS
    model = load_model(CLONE_CHECKPOINT)
    artifact: dict[str, Any] = {
        "format": "drop7-pytorch-direct-ppo-v1",
        "status": "running",
        "decision": "training; development cohort remains sealed",
        "levelBonus": int(extension.level_bonus),
        "architecture": architecture_manifest(model),
        "hypothesis": (
            "Whole-game on-policy PPO can optimize score and survival directly "
            "from the useful but imperfect original D2 clone without requiring "
            "teacher-action agreement or an imitation anchor."
        ),
        "origin": {
            "checkpoint": str(CLONE_CHECKPOINT),
            "checkpointSha256": clone_hash,
            "stage": "original rejected D2 clone, not corrected overfit clone",
            "pilotArtifact": str(ARTIFACT_PATH),
            "pilotArtifactSha256": sha256_file(ARTIFACT_PATH),
            "pilotValidationAgreement": prior["cloneTraining"][
                "finalValidation"
            ]["agreement"],
        },
        "frozenProtocol": {
            "anchorFree": True,
            "parameterSweep": False,
            "midTrainingEvaluation": False,
            "checkpointSelection": False,
            "iterations": DIRECT_PPO_ITERATIONS,
            "episodesPerIteration": PPO_EPISODES_PER_ITERATION,
            "trainingGames": DIRECT_PPO_ITERATIONS
            * PPO_EPISODES_PER_ITERATION,
            "ppoEpochs": PPO_EPOCHS,
            "minibatch": PPO_MINIBATCH,
            "learningRate": PPO_LEARNING_RATE,
            "clipRatio": CLIP_RATIO,
            "gamma": GAMMA,
            "gaeLambda": GAE_LAMBDA,
            "entropyCoefficient": ENTROPY_COEFFICIENT,
            "valueCoefficient": VALUE_COEFFICIENT,
            "maximumGradientNorm": MAX_GRADIENT_NORM,
            "optimizer": "AdamW(eps=1e-5, weight_decay=1e-5)",
            "policySamplingSeed": f"0x{POLICY_SAMPLE_SEED:08x}",
            "maximumMoves": MAXIMUM_MOVES,
        },
        "reward": {
            "formula": (
                "scoreDelta/17000 + 0.02 + 0.005*clears + "
                "0.005*reveals - 1.0*terminal"
            ),
            "scoreScale": EXPECTED_LEVEL_BONUS,
            "survival": SURVIVAL_REWARD,
            "numberedClear": CLEAR_REWARD,
            "coverReveal": REVEAL_REWARD,
            "terminal": TERMINAL_PENALTY,
        },
        "seedRanges": {
            "trainingStageOne": ["0x3d340000", "0x3d340fff"],
            "trainingStageTwo": ["0x3d350000", "0x3d352fff"],
            "singleDevelopmentCohort": ["0x3d360000", "0x3d36003f"],
            "allowedBoundary": "0x3d300000..0x3d3fffff",
            "forbiddenOpened": False,
        },
        "developmentProtocol": {
            "cohortReads": 0,
            "opened": False,
            "games": DIRECT_PPO_DEVELOPMENT_GAMES,
            "candidateFrozenBeforeOpen": False,
            "baselines": ["originalClone", "fairD2", "fairD1", "random"],
            "gate": {
                "zeroCensoring": True,
                "minimumCloneScoreAndMoveRatio": DIRECT_PPO_CLONE_RATIO,
                "minimumD2ScoreAndMoveRatio": DIRECT_PPO_D2_RATIO,
                "positivePairedOneSided95ScoreLowerBoundVsClone": True,
                "studentTCriticalDf63": DIRECT_PPO_PAIRED_T_CRITICAL,
            },
        },
        "resourceLimits": {
            "wallSeconds": DIRECT_PPO_WALL_LIMIT_SECONDS,
            "rssBytes": MAXIMUM_RSS_BYTES,
        },
        "trainingHistory": [],
        "trainingIterationsCompleted": 0,
        "trainingGamesCompleted": 0,
        "elapsedSeconds": 0.0,
    }
    save_artifact(artifact, DIRECT_PPO_ARTIFACT_PATH)
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    generator = torch.Generator().manual_seed(POLICY_SAMPLE_SEED)
    try:
        history = run_direct_ppo_schedule(
            extension,
            model,
            generator,
            optimizer,
            deadline,
            artifact,
        )
        if len(history) != DIRECT_PPO_ITERATIONS:
            raise RuntimeError("direct PPO did not complete its frozen schedule")
        enforce_direct_ppo_resource_limits(deadline)
        save_checkpoint(
            DIRECT_PPO_CHECKPOINT,
            model,
            "direct-ppo-frozen-before-development",
            DIRECT_PPO_ITERATIONS,
        )
        artifact["frozenCheckpoint"] = {
            "path": str(DIRECT_PPO_CHECKPOINT),
            "bytes": DIRECT_PPO_CHECKPOINT.stat().st_size,
            "sha256": sha256_file(DIRECT_PPO_CHECKPOINT),
            "iterations": DIRECT_PPO_ITERATIONS,
            "frozenBeforeDevelopment": True,
        }
        artifact["developmentProtocol"]["candidateFrozenBeforeOpen"] = True
        export = export_public_policy(
            model,
            public_export_fixture(extension),
            DIRECT_PPO_EXPORT_MANIFEST,
            DIRECT_PPO_EXPORT_WEIGHTS,
            DIRECT_PPO_EXPORT_GOLDEN,
            DIRECT_PPO_EXPORT_TORCHSCRIPT,
        )
        artifact["export"] = export
        artifact["status"] = "training-complete-development-sealed"
        artifact["decision"] = "checkpoint frozen; begin sole development cohort"
        artifact["elapsedSeconds"] = time.perf_counter() - started
        enforce_direct_ppo_resource_limits(deadline)
        save_artifact(artifact, DIRECT_PPO_ARTIFACT_PATH)
    except DirectPpoResourceLimit as error:
        save_checkpoint(
            DIRECT_PPO_PARTIAL_CHECKPOINT,
            model,
            "direct-ppo-resource-limit-partial-not-candidate",
            int(artifact["trainingIterationsCompleted"]),
        )
        artifact["status"] = "direct-ppo-resource-limit-aborted"
        artifact["decision"] = "stop without development read or tuning"
        artifact["resourceLimitError"] = str(error)
        artifact["partialCheckpoint"] = {
            "path": str(DIRECT_PPO_PARTIAL_CHECKPOINT),
            "bytes": DIRECT_PPO_PARTIAL_CHECKPOINT.stat().st_size,
            "sha256": sha256_file(DIRECT_PPO_PARTIAL_CHECKPOINT),
            "notADeployableCandidate": True,
        }
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact, DIRECT_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_DIRECT_PPO_DECISION",
            json.dumps({"status": artifact["status"], "error": str(error)}),
            flush=True,
        )
        return artifact

    # The candidate is immutable before the first use of the sole dev cohort.
    artifact["developmentProtocol"]["opened"] = True
    artifact["developmentProtocol"]["cohortReads"] = 1
    artifact["decision"] = "sole paired development cohort in progress"
    save_artifact(artifact, DIRECT_PPO_ARTIFACT_PATH)
    try:
        development = evaluate_direct_ppo_development(
            extension, model, deadline
        )
    except DirectPpoResourceLimit as error:
        artifact["status"] = "direct-ppo-development-resource-limit-aborted"
        artifact["decision"] = "stop without tuning or any further seed"
        artifact["resourceLimitError"] = str(error)
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact, DIRECT_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_DIRECT_PPO_DECISION",
            json.dumps({"status": artifact["status"], "error": str(error)}),
            flush=True,
        )
        return artifact
    gate = development["gate"]
    artifact["developmentResults"] = development
    artifact["status"] = (
        "direct-ppo-development-gate-passed"
        if gate["passed"]
        else "direct-ppo-development-gate-rejected"
    )
    artifact["decision"] = (
        "report before opening any further seed"
        if gate["passed"]
        else "stop without tuning or any further seed"
    )
    artifact["elapsedSeconds"] = time.perf_counter() - started
    save_artifact(artifact, DIRECT_PPO_ARTIFACT_PATH)
    print(
        "TORCH_RL_DIRECT_PPO_DECISION",
        json.dumps({"status": artifact["status"], "gate": gate}, sort_keys=True),
        flush=True,
    )
    return artifact


def load_model(path: Path) -> PublicActorCritic:
    checkpoint = torch.load(path, map_location="cpu", weights_only=False)
    if checkpoint.get("format") != "drop7-public-conv-actor-critic-v1":
        raise RuntimeError("incompatible actor/critic checkpoint")
    if int(checkpoint.get("levelBonus", -1)) != EXPECTED_LEVEL_BONUS:
        raise RuntimeError("checkpoint was trained under the wrong score constant")
    model = PublicActorCritic().eval()
    model.load_state_dict(checkpoint["model"], strict=True)
    return model


def require_isolated_self_test() -> dict[str, Any]:
    if not SELF_TEST_ARTIFACT_PATH.exists():
        raise RuntimeError("fresh-process PPO requires a separate passed self-test")
    report = json.loads(SELF_TEST_ARTIFACT_PATH.read_text(encoding="utf-8"))
    current_source_hash = sha256_file(Path(__file__).resolve())
    if report.get("status") != "passed" or not report.get("allocatorHeavy"):
        raise RuntimeError("fresh-process self-test marker is not a passing full test")
    if report.get("sourceHashes", {}).get("trainer") != current_source_hash:
        raise RuntimeError("fresh-process self-test marker is stale for this source")
    return report


def rejected_direct_artifact_audit() -> list[dict[str, Any]]:
    result: list[dict[str, Any]] = []
    for path in (
        DIRECT_PPO_PARTIAL_CHECKPOINT,
        DIRECT_PPO_CHECKPOINT,
        DIRECT_PPO_ARTIFACT_PATH,
        CORRECTED_CLONE_CHECKPOINT,
    ):
        item: dict[str, Any] = {
            "path": str(path),
            "exists": path.exists(),
            "loaded": False,
            "permittedAsOrigin": False,
        }
        if path.exists():
            item["bytes"] = path.stat().st_size
            item["sha256"] = sha256_file(path)
        result.append(item)
    return result


def load_fresh_process_origin() -> tuple[PublicActorCritic, dict[str, Any]]:
    checkpoint_hash = sha256_file(CLONE_CHECKPOINT)
    if checkpoint_hash != ORIGINAL_CLONE_SHA256:
        raise RuntimeError("fresh-process origin is not the frozen original clone")
    checkpoint = torch.load(CLONE_CHECKPOINT, map_location="cpu", weights_only=False)
    if checkpoint.get("format") != "drop7-public-conv-actor-critic-v1":
        raise RuntimeError("fresh-process origin format is incompatible")
    if checkpoint.get("stage") != "d2-clone" or int(checkpoint.get("iterations", -1)) != 0:
        raise RuntimeError("fresh-process origin is not the original D2 clone stage")
    if int(checkpoint.get("levelBonus", -1)) != EXPECTED_LEVEL_BONUS:
        raise RuntimeError("fresh-process origin used the wrong level bonus")
    model = PublicActorCritic().eval()
    model.load_state_dict(checkpoint["model"], strict=True)
    bit_equal = all(
        torch.equal(model.state_dict()[name], tensor)
        for name, tensor in checkpoint["model"].items()
    )
    if not bit_equal:
        raise RuntimeError("fresh-process loaded model is not bit-equal to origin")
    checkpoint_state_hash = state_dict_sha256(checkpoint["model"])
    loaded_state_hash = state_dict_sha256(dict(model.state_dict()))
    if checkpoint_state_hash != loaded_state_hash:
        raise RuntimeError("fresh-process origin state hash changed while loading")
    provenance = {
        "path": str(CLONE_CHECKPOINT),
        "bytes": CLONE_CHECKPOINT.stat().st_size,
        "sha256": checkpoint_hash,
        "stage": checkpoint["stage"],
        "iterations": int(checkpoint["iterations"]),
        "stateSha256": loaded_state_hash,
        "bitEqualAfterLoad": bit_equal,
        "rejectedArtifacts": rejected_direct_artifact_audit(),
    }
    return model, provenance


def tile_rollout_for_preflight(
    rollout: RolloutBatch, transitions: int
) -> RolloutBatch:
    if rollout.transitions < 2 or transitions < PPO_MINIBATCH:
        raise ValueError("preflight rollout must tile to at least one logical batch")
    indices = np.arange(transitions, dtype=np.int64) % rollout.transitions
    return RolloutBatch(
        boards=np.ascontiguousarray(rollout.boards[indices]),
        discs=np.ascontiguousarray(rollout.discs[indices]),
        phases=np.ascontiguousarray(rollout.phases[indices]),
        legal_masks=np.ascontiguousarray(rollout.legal_masks[indices]),
        actions=np.ascontiguousarray(rollout.actions[indices]),
        old_log_probabilities=np.ascontiguousarray(
            rollout.old_log_probabilities[indices]
        ),
        old_values=np.ascontiguousarray(rollout.old_values[indices]),
        advantages=np.ascontiguousarray(rollout.advantages[indices]),
        returns=np.ascontiguousarray(rollout.returns[indices]),
        rewards=np.ascontiguousarray(rollout.rewards[indices]),
        episode_scores=rollout.episode_scores.copy(),
        episode_moves=rollout.episode_moves.copy(),
        censored=rollout.censored,
        transitions=transitions,
        elapsed_seconds=rollout.elapsed_seconds,
    )


def lightweight_native_checks(extension: Any) -> dict[str, Any]:
    report = dict(extension.self_test())
    if not report.get("passed"):
        raise RuntimeError("fresh-process native self-test failed")
    if int(extension.level_bonus) != EXPECTED_LEVEL_BONUS:
        raise RuntimeError("fresh-process extension has the wrong level bonus")
    return {
        "passed": True,
        "levelBonus": int(extension.level_bonus),
        "exactEngineParity": bool(report["exactEngineParity"]),
        "terminalResetSemantics": bool(report["terminalResetSemantics"]),
        "conflictingSeedStartsRejected": bool(
            report["conflictingSeedStartsRejected"]
        ),
    }


def run_fresh_process_preflight(extension: Any) -> dict[str, Any]:
    self_test_marker = require_isolated_self_test()
    native = lightweight_native_checks(extension)
    started = time.perf_counter()
    deadline = started + DIRECT_PPO_WALL_LIMIT_SECONDS
    model, origin = load_fresh_process_origin()
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    if optimizer.state:
        raise RuntimeError("fresh-process preflight optimizer is not empty")
    generator = torch.Generator().manual_seed(POLICY_SAMPLE_SEED)
    artifact: dict[str, Any] = {
        "format": "drop7-direct-ppo-fresh-process-preflight-v1",
        "status": "running",
        "gameplaySeedClass": "already-opened self-test only",
        "selfTestMarker": {
            "path": str(SELF_TEST_ARTIFACT_PATH),
            "sha256": sha256_file(SELF_TEST_ARTIFACT_PATH),
            "trainerSourceSha256": self_test_marker["sourceHashes"]["trainer"],
            "separateProcess": True,
        },
        "native": native,
        "origin": origin,
        "logicalMinibatch": PPO_MINIBATCH,
        "physicalMinibatch": PPO_MINIBATCH,
        "gradientAccumulation": False,
        "ppoEpochs": PPO_EPOCHS,
        "maximumAllowedPeakRssBytes": FRESH_PROCESS_PREFLIGHT_MAXIMUM_RSS_BYTES,
        "freshTrainingSeedsOpened": False,
        "freshDevelopmentSeedsOpened": False,
        "optimizerStateInitiallyEmpty": True,
        "generatorStateSha256": generator_state_sha256(generator),
    }
    save_artifact(artifact, FRESH_PROCESS_PREFLIGHT_ARTIFACT_PATH)
    try:
        smoke = collect_ppo_games(
            extension,
            model,
            SELF_TEST_SEED_START,
            SELF_TEST_GAMES,
            generator,
            deadline,
        )
        logical = tile_rollout_for_preflight(smoke, PPO_MINIBATCH)
        update = direct_ppo_update(
            model,
            optimizer,
            logical,
            generator,
            deadline,
        )
        peak = peak_rss_bytes()
        passed = (
            peak <= FRESH_PROCESS_PREFLIGHT_MAXIMUM_RSS_BYTES
            and int(update["optimizerUpdates"]) == PPO_EPOCHS
            and logical.transitions >= PPO_MINIBATCH
            and smoke.censored == 0
        )
        artifact.update(
            {
                "status": "passed" if passed else "failed-memory-threshold",
                "decision": (
                    "authorize one clean production process"
                    if passed
                    else "stop without opening any fresh seed"
                ),
                "smokeSeeds": ["0x3d300040", "0x3d300043"],
                "smokeGames": SELF_TEST_GAMES,
                "smokeTransitions": smoke.transitions,
                "replicatedTransitions": logical.transitions,
                "smokeCensored": smoke.censored,
                "update": update,
                "peakRssBytes": peak,
                "under480MiB": peak
                <= FRESH_PROCESS_PREFLIGHT_MAXIMUM_RSS_BYTES,
                "elapsedSeconds": time.perf_counter() - started,
            }
        )
    except DirectPpoResourceLimit as error:
        artifact.update(
            {
                "status": "failed-512MiB-hard-limit",
                "decision": "stop without opening any fresh seed",
                "resourceLimitError": str(error),
                "peakRssBytes": peak_rss_bytes(),
                "under480MiB": False,
                "elapsedSeconds": time.perf_counter() - started,
            }
        )
    save_artifact(artifact, FRESH_PROCESS_PREFLIGHT_ARTIFACT_PATH)
    print(
        "TORCH_RL_FRESH_PROCESS_PREFLIGHT",
        json.dumps(
            {
                "status": artifact["status"],
                "peakRssBytes": artifact["peakRssBytes"],
                "limitBytes": FRESH_PROCESS_PREFLIGHT_MAXIMUM_RSS_BYTES,
            },
            sort_keys=True,
        ),
        flush=True,
    )
    return artifact


def run_gradaccum256_preflight(extension: Any) -> dict[str, Any]:
    self_test_marker = require_isolated_self_test()
    native = lightweight_native_checks(extension)
    started = time.perf_counter()
    deadline = started + DIRECT_PPO_WALL_LIMIT_SECONDS
    model, origin = load_fresh_process_origin()
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    if optimizer.state:
        raise RuntimeError("gradaccum256 preflight optimizer is not empty")
    generator = torch.Generator().manual_seed(POLICY_SAMPLE_SEED)
    artifact: dict[str, Any] = {
        "format": "drop7-direct-ppo-gradaccum256-preflight-v1",
        "status": "running",
        "gameplaySeedClass": "already-opened self-test only",
        "selfTestMarker": {
            "path": str(SELF_TEST_ARTIFACT_PATH),
            "sha256": sha256_file(SELF_TEST_ARTIFACT_PATH),
            "trainerSourceSha256": self_test_marker["sourceHashes"]["trainer"],
            "separateProcess": True,
            "fullLogicalEquivalence": self_test_marker["report"][
                "gradaccum256FullLogicalBatch"
            ],
            "finalLogicalEquivalence": self_test_marker["report"][
                "gradaccum256FinalLogicalBatch"
            ],
        },
        "native": native,
        "origin": origin,
        "logicalMinibatch": PPO_MINIBATCH,
        "physicalMinibatch": GRADACCUM_PHYSICAL_MINIBATCH,
        "gradientAccumulation": True,
        "optimizerStepPerLogicalBatch": 1,
        "ppoEpochs": PPO_EPOCHS,
        "maximumAllowedPeakRssBytes": GRADACCUM_PREFLIGHT_MAXIMUM_RSS_BYTES,
        "freshTrainingSeedsOpened": False,
        "freshDevelopmentSeedsOpened": False,
        "optimizerStateInitiallyEmpty": True,
        "generatorStateSha256": generator_state_sha256(generator),
    }
    save_artifact(artifact, GRADACCUM_PREFLIGHT_ARTIFACT_PATH)
    try:
        smoke = collect_ppo_games(
            extension,
            model,
            SELF_TEST_SEED_START,
            SELF_TEST_GAMES,
            generator,
            deadline,
        )
        logical = tile_rollout_for_preflight(smoke, PPO_MINIBATCH)
        update = gradaccum256_ppo_update(
            model,
            optimizer,
            logical,
            generator,
            deadline,
        )
        peak = peak_rss_bytes()
        expected_chunks = (
            PPO_EPOCHS
            * math.ceil(PPO_MINIBATCH / GRADACCUM_PHYSICAL_MINIBATCH)
        )
        passed = (
            peak <= GRADACCUM_PREFLIGHT_MAXIMUM_RSS_BYTES
            and int(update["optimizerUpdates"]) == PPO_EPOCHS
            and int(update["physicalChunks"]) == expected_chunks
            and logical.transitions == PPO_MINIBATCH
            and smoke.censored == 0
        )
        artifact.update(
            {
                "status": "passed" if passed else "failed-memory-threshold",
                "decision": (
                    "authorize one clean gradaccum256 production process"
                    if passed
                    else "stop without opening any fresh seed"
                ),
                "smokeSeeds": ["0x3d300040", "0x3d300043"],
                "smokeGames": SELF_TEST_GAMES,
                "smokeTransitions": smoke.transitions,
                "replicatedTransitions": logical.transitions,
                "smokeCensored": smoke.censored,
                "expectedPhysicalChunks": expected_chunks,
                "update": update,
                "peakRssBytes": peak,
                "under400MiB": peak <= GRADACCUM_PREFLIGHT_MAXIMUM_RSS_BYTES,
                "elapsedSeconds": time.perf_counter() - started,
            }
        )
    except DirectPpoResourceLimit as error:
        artifact.update(
            {
                "status": "failed-512MiB-hard-limit",
                "decision": "stop without opening any fresh seed",
                "resourceLimitError": str(error),
                "peakRssBytes": peak_rss_bytes(),
                "under400MiB": False,
                "elapsedSeconds": time.perf_counter() - started,
            }
        )
    save_artifact(artifact, GRADACCUM_PREFLIGHT_ARTIFACT_PATH)
    print(
        "TORCH_RL_GRADACCUM256_PREFLIGHT",
        json.dumps(
            {
                "status": artifact["status"],
                "peakRssBytes": artifact["peakRssBytes"],
                "limitBytes": GRADACCUM_PREFLIGHT_MAXIMUM_RSS_BYTES,
            },
            sort_keys=True,
        ),
        flush=True,
    )
    return artifact


def require_fresh_process_preflight() -> dict[str, Any]:
    if not FRESH_PROCESS_PREFLIGHT_ARTIFACT_PATH.exists():
        raise RuntimeError("fresh-process production requires its memory preflight")
    artifact = json.loads(
        FRESH_PROCESS_PREFLIGHT_ARTIFACT_PATH.read_text(encoding="utf-8")
    )
    current_source_hash = sha256_file(Path(__file__).resolve())
    if artifact.get("status") != "passed":
        raise RuntimeError("fresh-process memory preflight did not pass")
    if artifact.get("sourceHashes", {}).get("trainer") != current_source_hash:
        raise RuntimeError("fresh-process memory preflight is stale for this source")
    if int(artifact.get("peakRssBytes", MAXIMUM_RSS_BYTES)) > (
        FRESH_PROCESS_PREFLIGHT_MAXIMUM_RSS_BYTES
    ):
        raise RuntimeError("fresh-process memory preflight exceeded 480 MiB")
    if artifact.get("freshTrainingSeedsOpened") or artifact.get(
        "freshDevelopmentSeedsOpened"
    ):
        raise RuntimeError("fresh-process preflight unexpectedly opened a fresh seed")
    if int(artifact.get("logicalMinibatch", -1)) != PPO_MINIBATCH or int(
        artifact.get("physicalMinibatch", -1)
    ) != PPO_MINIBATCH:
        raise RuntimeError("fresh-process preflight changed PPO minibatching")
    return artifact


def require_gradaccum256_preflight() -> dict[str, Any]:
    if not GRADACCUM_PREFLIGHT_ARTIFACT_PATH.exists():
        raise RuntimeError("gradaccum256 production requires its memory preflight")
    artifact = json.loads(
        GRADACCUM_PREFLIGHT_ARTIFACT_PATH.read_text(encoding="utf-8")
    )
    current_source_hash = sha256_file(Path(__file__).resolve())
    if artifact.get("status") != "passed":
        raise RuntimeError("gradaccum256 memory preflight did not pass")
    if artifact.get("sourceHashes", {}).get("trainer") != current_source_hash:
        raise RuntimeError("gradaccum256 memory preflight is stale for this source")
    if int(artifact.get("peakRssBytes", MAXIMUM_RSS_BYTES)) > (
        GRADACCUM_PREFLIGHT_MAXIMUM_RSS_BYTES
    ):
        raise RuntimeError("gradaccum256 memory preflight exceeded 400 MiB")
    if artifact.get("freshTrainingSeedsOpened") or artifact.get(
        "freshDevelopmentSeedsOpened"
    ):
        raise RuntimeError("gradaccum256 preflight unexpectedly opened a fresh seed")
    if int(artifact.get("logicalMinibatch", -1)) != PPO_MINIBATCH or int(
        artifact.get("physicalMinibatch", -1)
    ) != GRADACCUM_PHYSICAL_MINIBATCH:
        raise RuntimeError("gradaccum256 preflight changed frozen batching")
    if not artifact.get("gradientAccumulation"):
        raise RuntimeError("gradaccum256 preflight did not accumulate gradients")
    return artifact


def assert_fresh_training_batch(seed_start: int, games: int) -> None:
    training_end = (
        FRESH_DIRECT_PPO_TRAINING_SEED_START + FRESH_DIRECT_PPO_TRAINING_GAMES
    )
    if (
        games != PPO_EPISODES_PER_ITERATION
        or seed_start < FRESH_DIRECT_PPO_TRAINING_SEED_START
        or seed_start + games > training_end
        or (seed_start - FRESH_DIRECT_PPO_TRAINING_SEED_START) % games != 0
    ):
        raise RuntimeError("fresh-process PPO attempted an unregistered training seed")
    if 0x3D34_0000 <= seed_start < 0x3D36_0000:
        raise RuntimeError("fresh-process PPO attempted a rejected direct-PPO seed")


def run_fresh_process_schedule(
    extension: Any,
    model: PublicActorCritic,
    generator: torch.Generator,
    optimizer: torch.optim.Optimizer,
    deadline: float,
    artifact: dict[str, Any],
) -> list[dict[str, Any]]:
    history: list[dict[str, Any]] = []
    for iteration in range(DIRECT_PPO_ITERATIONS):
        enforce_direct_ppo_resource_limits(deadline)
        seed_start = (
            FRESH_DIRECT_PPO_TRAINING_SEED_START
            + iteration * PPO_EPISODES_PER_ITERATION
        )
        assert_fresh_training_batch(seed_start, PPO_EPISODES_PER_ITERATION)
        rollout = collect_ppo_games(
            extension,
            model,
            seed_start,
            PPO_EPISODES_PER_ITERATION,
            generator,
            deadline,
        )
        artifact["inFlightIteration"] = {
            "iteration": iteration + 1,
            "seedStart": f"0x{seed_start:08x}",
            "seedEnd": (
                f"0x{seed_start + PPO_EPISODES_PER_ITERATION - 1:08x}"
            ),
            "collectionComplete": True,
            "episodesCollected": PPO_EPISODES_PER_ITERATION,
            "transitions": rollout.transitions,
            "censored": rollout.censored,
            "optimizerUpdateComplete": False,
        }
        artifact["trainingGamesCollected"] = (
            iteration + 1
        ) * PPO_EPISODES_PER_ITERATION
        artifact["elapsedSeconds"] = (
            DIRECT_PPO_WALL_LIMIT_SECONDS
            - max(0.0, deadline - time.perf_counter())
        )
        save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
        update = direct_ppo_update(
            model,
            optimizer,
            rollout,
            generator,
            deadline,
        )
        record = {
            "iteration": iteration + 1,
            "seedStart": f"0x{seed_start:08x}",
            "seedEnd": f"0x{seed_start + PPO_EPISODES_PER_ITERATION - 1:08x}",
            "episodes": PPO_EPISODES_PER_ITERATION,
            "transitions": rollout.transitions,
            "meanTrainingScore": float(rollout.episode_scores.mean()),
            "meanTrainingMoves": float(rollout.episode_moves.mean()),
            "meanReward": float(rollout.rewards.mean()),
            "censored": rollout.censored,
            "collectionSeconds": rollout.elapsed_seconds,
            "transitionsPerSecond": rollout.transitions
            / max(rollout.elapsed_seconds, 1.0e-9),
            "peakRssBytes": peak_rss_bytes(),
            **update,
        }
        history.append(record)
        artifact.pop("inFlightIteration", None)
        artifact["trainingHistory"] = history
        artifact["trainingIterationsCompleted"] = iteration + 1
        artifact["trainingGamesCompleted"] = (
            iteration + 1
        ) * PPO_EPISODES_PER_ITERATION
        artifact["elapsedSeconds"] = (
            DIRECT_PPO_WALL_LIMIT_SECONDS
            - max(0.0, deadline - time.perf_counter())
        )
        save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_FRESH_PROCESS_PPO",
            json.dumps(record, sort_keys=True),
            flush=True,
        )
    if len(history) != DIRECT_PPO_ITERATIONS:
        raise RuntimeError("fresh-process PPO did not complete exactly 32 iterations")
    return history


def run_gradaccum256_schedule(
    extension: Any,
    model: PublicActorCritic,
    generator: torch.Generator,
    optimizer: torch.optim.Optimizer,
    deadline: float,
    artifact: dict[str, Any],
) -> list[dict[str, Any]]:
    history: list[dict[str, Any]] = []
    for iteration in range(DIRECT_PPO_ITERATIONS):
        enforce_direct_ppo_resource_limits(deadline)
        seed_start = (
            FRESH_DIRECT_PPO_TRAINING_SEED_START
            + iteration * PPO_EPISODES_PER_ITERATION
        )
        assert_fresh_training_batch(seed_start, PPO_EPISODES_PER_ITERATION)
        rollout = collect_ppo_games(
            extension,
            model,
            seed_start,
            PPO_EPISODES_PER_ITERATION,
            generator,
            deadline,
        )
        artifact["inFlightIteration"] = {
            "iteration": iteration + 1,
            "seedStart": f"0x{seed_start:08x}",
            "seedEnd": (
                f"0x{seed_start + PPO_EPISODES_PER_ITERATION - 1:08x}"
            ),
            "collectionComplete": True,
            "episodesCollected": PPO_EPISODES_PER_ITERATION,
            "transitions": rollout.transitions,
            "censored": rollout.censored,
            "optimizerUpdateComplete": False,
        }
        artifact["trainingGamesCollected"] = (
            iteration + 1
        ) * PPO_EPISODES_PER_ITERATION
        artifact["elapsedSeconds"] = (
            DIRECT_PPO_WALL_LIMIT_SECONDS
            - max(0.0, deadline - time.perf_counter())
        )
        save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
        update = gradaccum256_ppo_update(
            model,
            optimizer,
            rollout,
            generator,
            deadline,
        )
        record = {
            "iteration": iteration + 1,
            "seedStart": f"0x{seed_start:08x}",
            "seedEnd": f"0x{seed_start + PPO_EPISODES_PER_ITERATION - 1:08x}",
            "episodes": PPO_EPISODES_PER_ITERATION,
            "transitions": rollout.transitions,
            "meanTrainingScore": float(rollout.episode_scores.mean()),
            "meanTrainingMoves": float(rollout.episode_moves.mean()),
            "meanReward": float(rollout.rewards.mean()),
            "censored": rollout.censored,
            "collectionSeconds": rollout.elapsed_seconds,
            "transitionsPerSecond": rollout.transitions
            / max(rollout.elapsed_seconds, 1.0e-9),
            "peakRssBytes": peak_rss_bytes(),
            **update,
        }
        history.append(record)
        artifact.pop("inFlightIteration", None)
        artifact["trainingHistory"] = history
        artifact["trainingIterationsCompleted"] = iteration + 1
        artifact["trainingGamesCompleted"] = (
            iteration + 1
        ) * PPO_EPISODES_PER_ITERATION
        artifact["elapsedSeconds"] = (
            DIRECT_PPO_WALL_LIMIT_SECONDS
            - max(0.0, deadline - time.perf_counter())
        )
        save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_GRADACCUM256_PPO",
            json.dumps(record, sort_keys=True),
            flush=True,
        )
    if len(history) != DIRECT_PPO_ITERATIONS:
        raise RuntimeError("gradaccum256 PPO did not complete exactly 32 iterations")
    return history


def evaluate_fresh_process_development(
    extension: Any,
    model: PublicActorCritic,
    deadline: float,
) -> dict[str, Any]:
    results: dict[str, Evaluation] = {}
    policies: tuple[
        tuple[str, Literal["random", "d1", "d2", "network"], PublicActorCritic | None],
        ...,
    ] = (
        ("candidate", "network", model),
        ("originalClone", "network", load_fresh_process_origin()[0]),
        ("fairD2", "d2", None),
        ("fairD1", "d1", None),
        ("random", "random", None),
    )
    for name, policy, policy_model in policies:
        results[name] = evaluate_policy(
            extension,
            policy,
            FRESH_DIRECT_PPO_DEVELOPMENT_SEED_START,
            FRESH_DIRECT_PPO_DEVELOPMENT_GAMES,
            policy_model,
        )
        enforce_direct_ppo_resource_limits(deadline)
    gate = direct_ppo_development_gate(
        results["candidate"],
        results["originalClone"],
        results["fairD2"],
        results["fairD1"],
        results["random"],
    )
    return {
        **{name: result.summary() for name, result in results.items()},
        "gate": gate,
    }


def run_fresh_process_ppo(extension: Any) -> dict[str, Any]:
    self_test_marker = require_isolated_self_test()
    preflight = require_fresh_process_preflight()
    native = lightweight_native_checks(extension)
    started = time.perf_counter()
    deadline = started + DIRECT_PPO_WALL_LIMIT_SECONDS
    model, origin = load_fresh_process_origin()
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    if optimizer.state:
        raise RuntimeError("fresh-process production optimizer is not empty")
    generator = torch.Generator().manual_seed(POLICY_SAMPLE_SEED)
    artifact: dict[str, Any] = {
        "format": "drop7-direct-ppo-fresh-process-v1",
        "status": "running",
        "decision": "training; sole fresh development cohort remains sealed",
        "hypothesis": (
            "Isolating the already-passed allocator-heavy neural self-test in a "
            "separate process permits the unchanged logical-1024 direct PPO "
            "protocol to remain below 512 MiB."
        ),
        "levelBonus": int(extension.level_bonus),
        "native": native,
        "architecture": architecture_manifest(model),
        "origin": origin,
        "isolation": {
            "fullSelfTestRanInThisProcess": False,
            "selfTestMarker": str(SELF_TEST_ARTIFACT_PATH),
            "selfTestMarkerSha256": sha256_file(SELF_TEST_ARTIFACT_PATH),
            "selfTestTrainerSourceSha256": self_test_marker["sourceHashes"][
                "trainer"
            ],
            "preflightArtifact": str(FRESH_PROCESS_PREFLIGHT_ARTIFACT_PATH),
            "preflightArtifactSha256": sha256_file(
                FRESH_PROCESS_PREFLIGHT_ARTIFACT_PATH
            ),
            "preflightPeakRssBytes": int(preflight["peakRssBytes"]),
            "rejected3d34ArtifactsLoaded": False,
            "optimizerStateInitiallyEmpty": True,
            "initialModelStateSha256": state_dict_sha256(dict(model.state_dict())),
            "generatorStateSha256": generator_state_sha256(generator),
        },
        "frozenProtocol": {
            "anchorFree": True,
            "parameterSweep": False,
            "midTrainingEvaluation": False,
            "checkpointSelection": False,
            "iterations": DIRECT_PPO_ITERATIONS,
            "episodesPerIteration": PPO_EPISODES_PER_ITERATION,
            "trainingGames": FRESH_DIRECT_PPO_TRAINING_GAMES,
            "ppoEpochs": PPO_EPOCHS,
            "logicalMinibatch": PPO_MINIBATCH,
            "physicalMinibatch": PPO_MINIBATCH,
            "gradientAccumulation": False,
            "learningRate": PPO_LEARNING_RATE,
            "clipRatio": CLIP_RATIO,
            "gamma": GAMMA,
            "gaeLambda": GAE_LAMBDA,
            "entropyCoefficient": ENTROPY_COEFFICIENT,
            "valueCoefficient": VALUE_COEFFICIENT,
            "maximumGradientNorm": MAX_GRADIENT_NORM,
            "optimizer": "AdamW(eps=1e-5, weight_decay=1e-5)",
            "policySamplingSeed": f"0x{POLICY_SAMPLE_SEED:08x}",
            "maximumMoves": MAXIMUM_MOVES,
        },
        "reward": {
            "formula": (
                "scoreDelta/17000 + 0.02 + 0.005*clears + "
                "0.005*reveals - 1.0*terminal"
            ),
            "scoreScale": EXPECTED_LEVEL_BONUS,
            "survival": SURVIVAL_REWARD,
            "numberedClear": CLEAR_REWARD,
            "coverReveal": REVEAL_REWARD,
            "terminal": TERMINAL_PENALTY,
        },
        "seedRanges": {
            "training": ["0x3d390000", "0x3d393fff"],
            "singleDevelopmentCohort": ["0x3d3a0000", "0x3d3a003f"],
            "allowedBoundary": "0x3d300000..0x3d3fffff",
            "prior3d34DataUsed": False,
            "forbiddenOpened": False,
        },
        "developmentProtocol": {
            "cohortReads": 0,
            "opened": False,
            "games": FRESH_DIRECT_PPO_DEVELOPMENT_GAMES,
            "candidateFrozenBeforeOpen": False,
            "baselines": ["originalClone", "fairD2", "fairD1", "random"],
            "gate": {
                "zeroCensoring": True,
                "minimumCloneScoreAndMoveRatio": DIRECT_PPO_CLONE_RATIO,
                "minimumD2ScoreAndMoveRatio": DIRECT_PPO_D2_RATIO,
                "positivePairedOneSided95ScoreLowerBoundVsClone": True,
                "studentTCriticalDf63": DIRECT_PPO_PAIRED_T_CRITICAL,
            },
        },
        "resourceLimits": {
            "wallSeconds": DIRECT_PPO_WALL_LIMIT_SECONDS,
            "rssBytes": MAXIMUM_RSS_BYTES,
        },
        "trainingHistory": [],
        "trainingIterationsCompleted": 0,
        "trainingGamesCollected": 0,
        "trainingGamesCompleted": 0,
        "elapsedSeconds": 0.0,
    }
    save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
    try:
        history = run_fresh_process_schedule(
            extension,
            model,
            generator,
            optimizer,
            deadline,
            artifact,
        )
        if len(history) != DIRECT_PPO_ITERATIONS:
            raise RuntimeError("fresh-process PPO did not finish its frozen schedule")
        enforce_direct_ppo_resource_limits(deadline)
        save_checkpoint(
            FRESH_DIRECT_PPO_CHECKPOINT,
            model,
            "fresh-process-direct-ppo-frozen-before-development",
            DIRECT_PPO_ITERATIONS,
        )
        artifact["frozenCheckpoint"] = {
            "path": str(FRESH_DIRECT_PPO_CHECKPOINT),
            "bytes": FRESH_DIRECT_PPO_CHECKPOINT.stat().st_size,
            "sha256": sha256_file(FRESH_DIRECT_PPO_CHECKPOINT),
            "stateSha256": state_dict_sha256(dict(model.state_dict())),
            "iterations": DIRECT_PPO_ITERATIONS,
            "frozenBeforeDevelopment": True,
        }
        artifact["developmentProtocol"]["candidateFrozenBeforeOpen"] = True
        artifact["export"] = export_public_policy(
            model,
            public_export_fixture(extension),
            FRESH_DIRECT_PPO_EXPORT_MANIFEST,
            FRESH_DIRECT_PPO_EXPORT_WEIGHTS,
            FRESH_DIRECT_PPO_EXPORT_GOLDEN,
            FRESH_DIRECT_PPO_EXPORT_TORCHSCRIPT,
        )
        artifact["status"] = "training-complete-development-sealed"
        artifact["decision"] = "checkpoint frozen; begin sole development cohort"
        artifact["elapsedSeconds"] = time.perf_counter() - started
        enforce_direct_ppo_resource_limits(deadline)
        save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
    except DirectPpoResourceLimit as error:
        save_checkpoint(
            FRESH_DIRECT_PPO_PARTIAL_CHECKPOINT,
            model,
            "fresh-process-direct-ppo-resource-limit-partial-not-candidate",
            int(artifact["trainingIterationsCompleted"]),
        )
        artifact["status"] = "fresh-process-direct-ppo-resource-limit-aborted"
        artifact["decision"] = "stop without development read or tuning"
        artifact["resourceLimitError"] = str(error)
        artifact["partialCheckpoint"] = {
            "path": str(FRESH_DIRECT_PPO_PARTIAL_CHECKPOINT),
            "bytes": FRESH_DIRECT_PPO_PARTIAL_CHECKPOINT.stat().st_size,
            "sha256": sha256_file(FRESH_DIRECT_PPO_PARTIAL_CHECKPOINT),
            "notADeployableCandidate": True,
        }
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_FRESH_PROCESS_DECISION",
            json.dumps({"status": artifact["status"], "error": str(error)}),
            flush=True,
        )
        return artifact

    artifact["developmentProtocol"]["opened"] = True
    artifact["developmentProtocol"]["cohortReads"] = 1
    artifact["decision"] = "sole paired fresh development cohort in progress"
    save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
    try:
        development = evaluate_fresh_process_development(
            extension, model, deadline
        )
    except DirectPpoResourceLimit as error:
        artifact["status"] = "fresh-development-resource-limit-aborted"
        artifact["decision"] = "stop without tuning or any further seed"
        artifact["resourceLimitError"] = str(error)
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_FRESH_PROCESS_DECISION",
            json.dumps({"status": artifact["status"], "error": str(error)}),
            flush=True,
        )
        return artifact
    gate = development["gate"]
    artifact["developmentResults"] = development
    artifact["status"] = (
        "fresh-process-development-gate-passed"
        if gate["passed"]
        else "fresh-process-development-gate-rejected"
    )
    artifact["decision"] = (
        "report before opening any further seed"
        if gate["passed"]
        else "stop without tuning or any further seed"
    )
    artifact["elapsedSeconds"] = time.perf_counter() - started
    save_artifact(artifact, FRESH_DIRECT_PPO_ARTIFACT_PATH)
    print(
        "TORCH_RL_FRESH_PROCESS_DECISION",
        json.dumps({"status": artifact["status"], "gate": gate}, sort_keys=True),
        flush=True,
    )
    return artifact


def run_gradaccum256_ppo(extension: Any) -> dict[str, Any]:
    self_test_marker = require_isolated_self_test()
    preflight = require_gradaccum256_preflight()
    native = lightweight_native_checks(extension)
    started = time.perf_counter()
    deadline = started + DIRECT_PPO_WALL_LIMIT_SECONDS
    model, origin = load_fresh_process_origin()
    optimizer = torch.optim.AdamW(
        model.parameters(), lr=PPO_LEARNING_RATE, eps=1.0e-5, weight_decay=1.0e-5
    )
    if optimizer.state:
        raise RuntimeError("gradaccum256 production optimizer is not empty")
    generator = torch.Generator().manual_seed(POLICY_SAMPLE_SEED)
    artifact: dict[str, Any] = {
        "format": "drop7-direct-ppo-gradaccum256-v1",
        "status": "running",
        "decision": "training; sole fresh development cohort remains sealed",
        "hypothesis": (
            "Four ordered physical-256 activation chunks can reproduce each "
            "unchanged logical-1024 PPO mean loss and single AdamW step while "
            "remaining below the memory limits."
        ),
        "levelBonus": int(extension.level_bonus),
        "native": native,
        "architecture": architecture_manifest(model),
        "origin": origin,
        "isolation": {
            "fullSelfTestRanInThisProcess": False,
            "selfTestMarker": str(SELF_TEST_ARTIFACT_PATH),
            "selfTestMarkerSha256": sha256_file(SELF_TEST_ARTIFACT_PATH),
            "selfTestTrainerSourceSha256": self_test_marker["sourceHashes"][
                "trainer"
            ],
            "fullLogicalEquivalence": self_test_marker["report"][
                "gradaccum256FullLogicalBatch"
            ],
            "finalLogicalEquivalence": self_test_marker["report"][
                "gradaccum256FinalLogicalBatch"
            ],
            "preflightArtifact": str(GRADACCUM_PREFLIGHT_ARTIFACT_PATH),
            "preflightArtifactSha256": sha256_file(
                GRADACCUM_PREFLIGHT_ARTIFACT_PATH
            ),
            "preflightPeakRssBytes": int(preflight["peakRssBytes"]),
            "rejected3d34ArtifactsLoaded": False,
            "optimizerStateInitiallyEmpty": True,
            "initialModelStateSha256": state_dict_sha256(dict(model.state_dict())),
            "generatorStateSha256": generator_state_sha256(generator),
        },
        "frozenProtocol": {
            "anchorFree": True,
            "parameterSweep": False,
            "midTrainingEvaluation": False,
            "checkpointSelection": False,
            "iterations": DIRECT_PPO_ITERATIONS,
            "episodesPerIteration": PPO_EPISODES_PER_ITERATION,
            "trainingGames": FRESH_DIRECT_PPO_TRAINING_GAMES,
            "ppoEpochs": PPO_EPOCHS,
            "logicalMinibatch": PPO_MINIBATCH,
            "physicalMinibatch": GRADACCUM_PHYSICAL_MINIBATCH,
            "gradientAccumulation": True,
            "optimizerStepsPerLogicalBatch": 1,
            "lossWeightPerFullPhysicalChunk": (
                GRADACCUM_PHYSICAL_MINIBATCH / PPO_MINIBATCH
            ),
            "sampleOrderPreserved": True,
            "learningRate": PPO_LEARNING_RATE,
            "clipRatio": CLIP_RATIO,
            "gamma": GAMMA,
            "gaeLambda": GAE_LAMBDA,
            "entropyCoefficient": ENTROPY_COEFFICIENT,
            "valueCoefficient": VALUE_COEFFICIENT,
            "maximumGradientNorm": MAX_GRADIENT_NORM,
            "optimizer": "AdamW(eps=1e-5, weight_decay=1e-5)",
            "policySamplingSeed": f"0x{POLICY_SAMPLE_SEED:08x}",
            "maximumMoves": MAXIMUM_MOVES,
        },
        "reward": {
            "formula": (
                "scoreDelta/17000 + 0.02 + 0.005*clears + "
                "0.005*reveals - 1.0*terminal"
            ),
            "scoreScale": EXPECTED_LEVEL_BONUS,
            "survival": SURVIVAL_REWARD,
            "numberedClear": CLEAR_REWARD,
            "coverReveal": REVEAL_REWARD,
            "terminal": TERMINAL_PENALTY,
        },
        "seedRanges": {
            "training": ["0x3d390000", "0x3d393fff"],
            "singleDevelopmentCohort": ["0x3d3a0000", "0x3d3a003f"],
            "allowedBoundary": "0x3d300000..0x3d3fffff",
            "prior3d34DataUsed": False,
            "forbiddenOpened": False,
        },
        "developmentProtocol": {
            "cohortReads": 0,
            "opened": False,
            "games": FRESH_DIRECT_PPO_DEVELOPMENT_GAMES,
            "candidateFrozenBeforeOpen": False,
            "baselines": ["originalClone", "fairD2", "fairD1", "random"],
            "gate": {
                "zeroCensoring": True,
                "minimumCloneScoreAndMoveRatio": DIRECT_PPO_CLONE_RATIO,
                "minimumD2ScoreAndMoveRatio": DIRECT_PPO_D2_RATIO,
                "positivePairedOneSided95ScoreLowerBoundVsClone": True,
                "studentTCriticalDf63": DIRECT_PPO_PAIRED_T_CRITICAL,
            },
        },
        "resourceLimits": {
            "preflightRssBytes": GRADACCUM_PREFLIGHT_MAXIMUM_RSS_BYTES,
            "productionRssBytes": MAXIMUM_RSS_BYTES,
            "wallSeconds": DIRECT_PPO_WALL_LIMIT_SECONDS,
        },
        "trainingHistory": [],
        "trainingIterationsCompleted": 0,
        "trainingGamesCollected": 0,
        "trainingGamesCompleted": 0,
        "elapsedSeconds": 0.0,
    }
    save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
    try:
        history = run_gradaccum256_schedule(
            extension,
            model,
            generator,
            optimizer,
            deadline,
            artifact,
        )
        if len(history) != DIRECT_PPO_ITERATIONS:
            raise RuntimeError("gradaccum256 PPO did not finish its frozen schedule")
        enforce_direct_ppo_resource_limits(deadline)
        save_checkpoint(
            GRADACCUM_PPO_CHECKPOINT,
            model,
            "gradaccum256-direct-ppo-frozen-before-development",
            DIRECT_PPO_ITERATIONS,
        )
        artifact["frozenCheckpoint"] = {
            "path": str(GRADACCUM_PPO_CHECKPOINT),
            "bytes": GRADACCUM_PPO_CHECKPOINT.stat().st_size,
            "sha256": sha256_file(GRADACCUM_PPO_CHECKPOINT),
            "stateSha256": state_dict_sha256(dict(model.state_dict())),
            "iterations": DIRECT_PPO_ITERATIONS,
            "frozenBeforeDevelopment": True,
        }
        artifact["developmentProtocol"]["candidateFrozenBeforeOpen"] = True
        artifact["export"] = export_public_policy(
            model,
            public_export_fixture(extension),
            GRADACCUM_EXPORT_MANIFEST,
            GRADACCUM_EXPORT_WEIGHTS,
            GRADACCUM_EXPORT_GOLDEN,
            GRADACCUM_EXPORT_TORCHSCRIPT,
        )
        artifact["status"] = "training-complete-development-sealed"
        artifact["decision"] = "checkpoint frozen; begin sole development cohort"
        artifact["elapsedSeconds"] = time.perf_counter() - started
        enforce_direct_ppo_resource_limits(deadline)
        save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
    except DirectPpoResourceLimit as error:
        save_checkpoint(
            GRADACCUM_PPO_PARTIAL_CHECKPOINT,
            model,
            "gradaccum256-direct-ppo-resource-limit-partial-not-candidate",
            int(artifact["trainingIterationsCompleted"]),
        )
        artifact["status"] = "gradaccum256-direct-ppo-resource-limit-aborted"
        artifact["decision"] = "stop without development read or tuning"
        artifact["resourceLimitError"] = str(error)
        artifact["partialCheckpoint"] = {
            "path": str(GRADACCUM_PPO_PARTIAL_CHECKPOINT),
            "bytes": GRADACCUM_PPO_PARTIAL_CHECKPOINT.stat().st_size,
            "sha256": sha256_file(GRADACCUM_PPO_PARTIAL_CHECKPOINT),
            "notADeployableCandidate": True,
        }
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_GRADACCUM256_DECISION",
            json.dumps({"status": artifact["status"], "error": str(error)}),
            flush=True,
        )
        return artifact

    artifact["developmentProtocol"]["opened"] = True
    artifact["developmentProtocol"]["cohortReads"] = 1
    artifact["decision"] = "sole paired fresh development cohort in progress"
    save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
    try:
        development = evaluate_fresh_process_development(
            extension, model, deadline
        )
    except DirectPpoResourceLimit as error:
        artifact["status"] = "gradaccum256-development-resource-limit-aborted"
        artifact["decision"] = "stop without tuning or any further seed"
        artifact["resourceLimitError"] = str(error)
        artifact["elapsedSeconds"] = time.perf_counter() - started
        save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
        print(
            "TORCH_RL_GRADACCUM256_DECISION",
            json.dumps({"status": artifact["status"], "error": str(error)}),
            flush=True,
        )
        return artifact
    gate = development["gate"]
    artifact["developmentResults"] = development
    artifact["status"] = (
        "gradaccum256-development-gate-passed"
        if gate["passed"]
        else "gradaccum256-development-gate-rejected"
    )
    artifact["decision"] = (
        "report before opening any further seed"
        if gate["passed"]
        else "stop without tuning or any further seed"
    )
    artifact["elapsedSeconds"] = time.perf_counter() - started
    save_artifact(artifact, GRADACCUM_PPO_ARTIFACT_PATH)
    print(
        "TORCH_RL_GRADACCUM256_DECISION",
        json.dumps({"status": artifact["status"], "gate": gate}, sort_keys=True),
        flush=True,
    )
    return artifact


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--self-test", action="store_true")
    group.add_argument("--pilot", action="store_true")
    group.add_argument("--corrected", action="store_true")
    group.add_argument("--direct-ppo", action="store_true")
    group.add_argument("--fresh-process-preflight", action="store_true")
    group.add_argument("--direct-ppo-fresh-process", action="store_true")
    group.add_argument("--gradaccum256-preflight", action="store_true")
    group.add_argument("--direct-ppo-gradaccum256", action="store_true")
    group.add_argument("--evaluate", type=Path, metavar="CHECKPOINT")
    group.add_argument("--export", type=Path, metavar="CHECKPOINT")
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    configure_torch()
    extension = build_extension()
    if arguments.fresh_process_preflight:
        result = run_fresh_process_preflight(extension)
        return 0 if result["status"] == "passed" else 1
    if arguments.direct_ppo_fresh_process:
        run_fresh_process_ppo(extension)
        return 0
    if arguments.gradaccum256_preflight:
        result = run_gradaccum256_preflight(extension)
        return 0 if result["status"] == "passed" else 1
    if arguments.direct_ppo_gradaccum256:
        run_gradaccum256_ppo(extension)
        return 0
    self_test_report = self_test(extension)
    if arguments.self_test:
        marker = {
            "format": "drop7-torch-full-self-test-v1",
            "status": "passed",
            "allocatorHeavy": True,
            "mustRunOutsideFreshProductionProcess": True,
            "report": self_test_report,
        }
        save_artifact(marker, SELF_TEST_ARTIFACT_PATH)
        return 0
    if arguments.pilot:
        run_pilot(extension)
        return 0
    if arguments.corrected:
        run_corrected(extension)
        return 0
    if arguments.direct_ppo:
        run_direct_ppo(extension)
        return 0
    if arguments.evaluate is not None:
        model = load_model(arguments.evaluate)
        result = evaluate_policy(
            extension,
            "network",
            DEVELOPMENT_SEED_START,
            DEVELOPMENT_GAMES,
            model,
        )
        print(json.dumps(result.summary(), indent=2, sort_keys=True))
        return 0
    if arguments.export is not None:
        model = load_model(arguments.export)
        environment = extension.VectorEnvironment(
            SELF_TEST_GAMES,
            SELF_TEST_SEED_START,
            SELF_TEST_GAMES,
            100,
        )
        boards, discs, phases, masks, _ = environment.observations()
        actions, q_values, work = environment.teacher_actions(2, TEACHER_THREADS)
        corpus = TeacherCorpus(
            np.asarray(boards),
            np.asarray(discs),
            np.asarray(phases),
            np.asarray(masks),
            np.asarray(actions),
            np.asarray(q_values, dtype=np.float32),
            SELF_TEST_GAMES,
            SELF_TEST_GAMES,
            int(np.asarray(work).sum()),
            0,
            0,
            0,
            0,
        )
        print(json.dumps(export_public_policy(model, corpus), indent=2, sort_keys=True))
        return 0
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
