# AMD Ryzen Halo research plan

This guide treats a 128 GB Ryzen Halo Linux workstation as a hybrid system: a
many-core CPU for exact, branch-heavy simulation and an integrated GPU for
dense batched learning and inference. Detect the actual SKU and software stack
before choosing limits; “Ryzen Halo” is not a complete machine profile.

## Current platform facts

For the common Ryzen AI Max+ 395 configuration, AMD lists 16 Zen 5 CPU cores,
32 threads, AVX-512 support, up to 128 GB of LPDDR5X-8000 memory, and Radeon
8060S graphics with 40 graphics cores. Other Ryzen Halo SKUs differ, so record
the detected model rather than copying those numbers. See AMD's
[Ryzen AI Max+ 395 specifications](https://www.amd.com/en/products/processors/laptop/ryzen/ai-300-series/amd-ryzen-ai-max-plus-395.html).

ROCm support changes with kernels, distributions, and framework releases. Use
AMD's current [compatibility matrix](https://rocm.docs.amd.com/en/latest/compatibility/compatibility-matrix.html)
and [RDNA 3.5 system-optimization guide](https://rocm.docs.amd.com/en/latest/reference/system-optimization/rdna3-5.html)
at setup time. Do not assume that a detected GPU, a working display driver, and
a supported ROCm compute stack are equivalent.

Ryzen Halo's integrated GPU uses shared system memory. AMD's RDNA 3.5 guidance
describes GPU virtual memory backed by system RAM and configurable GTT/TTM
behavior. Therefore:

- host and GPU memory figures overlap and must not be added together;
- CPU search and GPU tensors compete for memory bandwidth as well as capacity;
- a large allocation that succeeds can still slow the complete pipeline; and
- BIOS, firmware, kernel, and GTT/TTM changes require explicit operator approval
  and their own before/after profile.

PyTorch intentionally exposes ROCm devices through the `torch.cuda` API. A
valid preflight records `torch.__version__`, `torch.version.hip`,
`torch.cuda.is_available()`, the device name, and a small forward/backward
checksum. See the official [PyTorch HIP semantics](https://docs.pytorch.org/docs/main/notes/hip.html).

## Safe initial memory policy

Do not interpret 128 GB as a target allocation. As a conservative first pass,
reserve 24–32 GB for the operating system, compilers, filesystem cache, and
unexpected CPU/GPU peaks, then benchmark smaller allocations. This is a project
safety default, not an AMD specification. The experiment records:

- physical memory and cgroup/container limit;
- available memory and swap at process start;
- CPU cache/transposition-table allocation;
- model, optimizer, replay, and batch allocations;
- peak whole-job-tree RSS and peak reported GPU/shared allocation; and
- out-of-memory, major-fault, and throttling events.

Increase memory only when a measured cache-hit, batch-throughput, or data-
pipeline gain improves the end-to-end objective. Leave the default GTT/TTM and
firmware settings alone until ordinary configurations have been profiled.

## Workload placement

| Workload | Default device | Reason |
| --- | --- | --- |
| Exact transitions and chain cascades | CPU | Irregular control flow, tiny 7×7 state, exact integer semantics |
| Recursive expectimax and transposition lookup | CPU | Variable tree shape and random-access tables |
| Independent complete games | CPU processes/threads | Natural coarse-grained parallelism |
| Neural training | GPU | Dense batches and backpropagation |
| Batched leaf/sibling evaluation | GPU candidate | Many uniform tensors can amortize launch cost |
| Dataset transforms and checksums | CPU, parallel I/O | Deterministic streaming work |
| Full simulator GPU port | Research prototype only | Requires exact RNG/transition parity and end-to-end proof |
| NPU inference | Deferred | Not the practical first target for training or irregular search |

Drop7 cascades and search frontiers have changing lengths and legal-action
counts. A GPU simulator may suffer wavefront divergence and launch overhead.
The GPU is not rejected in advance, but it must beat an optimized CPU pipeline
after transfers, synchronization, batching, and CPU/GPU contention are counted.

## Machine preflight

Run:

```sh
make research-doctor
python3 .agents/skills/million-point-research/scripts/researchctl.py doctor \
  --output research/system-profiles
```

Promote the printed JSON to `research/system-profiles/` for a performance claim.
The profile should include:

- distribution, kernel, architecture, container/cgroup limits, and a generated
  profile ID that omits the hostname;
- CPU model, sockets, cores, threads, affinity, SMT, NUMA, and governor;
- total/available RAM and swap;
- GPU name, target from `rocminfo`, visible devices, driver, ROCm/HIP, and
  whether memory is shared;
- Python, PyTorch, Node, C++ compiler, and relevant libraries;
- available profilers such as `perf`, `rocprofv3`, and ROCm SMI; and
- power/temperature fields or an explicit reason they are unavailable.

Before a long run, also record firmware/BIOS versions when the operating system
exposes them, AC power state, cooling mode, and any system tuning that the doctor
cannot observe.

## Stage A: establish CPU scaling

Benchmark separately:

- raw transitions and complete games per second;
- D1, D2, D4, and buildable D5 logical work per second;
- D4 moves per second;
- game-level workers versus within-decision threads;
- physical cores versus SMT; and
- cache sizes such as 256 MB, 1 GB, 4 GB, 16 GB, and—only with headroom—32 or
  64 GB.

Use fixed work, fixed seeds that are already permitted for this diagnostic, and
three repeated measurements. Record cache hit rate, page faults, bandwidth,
CPU time, wall time, RSS, and thermal throttling. Select the stable Pareto point;
do not assume all threads or the largest table is best.

Semantics-preserving CPU priorities are incremental feature updates, compact
mirror-canonical keys, sharded low-contention transposition tables, parallel
whole games/sibling panels, measured allocator changes, and profile-guided
optimization. AVX-512 is useful only where exact integer behavior stays clear.

## Stage B: test a GPU leaf service

The first GPU prototype should not port the simulator. CPU search workers enqueue
public leaf or afterstate tensors; one GPU service batches inference and returns
values. Keep weights and buffers resident and double-buffer work so CPU search
and GPU inference overlap.

Measure inference at batches 1, 7, 64, 256, 1,024, and 4,096, then measure the
real search pipeline. Compare FP32 first. Mixed precision or nondeterministic
reductions are algorithmic changes if close root rankings or actions change.

Advance only if:

- the exact/tolerance contract is met on every reference root;
- realistic end-to-end search is materially faster than optimized CPU inference;
- batching latency does not starve recursive CPU workers;
- utilization is sustained without exhausting shared memory bandwidth; and
- the run records both policy work and device/resource metrics.

## Stage C: optional batched simulator

Only after profiling justifies it, prototype a structure-of-arrays simulator
with event-keyed counter randomness and many independent environments. Batch by
rise phase or cascade stage where useful. Require at least one million mixed
transition cases with exact native checksum agreement, including reveals,
gravity, chain depth, scoring, row rises, and terminal behavior.

The acceptance metric is at least a preregistered end-to-end throughput gain,
not an isolated kernel headline. AMD's HIP guidance recommends profiling,
amortizing small launches, reusing allocations, and measuring whether kernels
are compute-, memory-, or overhead-bound; see the official
[HIP performance model](https://rocm.docs.amd.com/projects/HIP/en/latest/understand/performance_optimization.html)
and [performance guidelines](https://rocm.docs.amd.com/projects/HIP/en/latest/how-to/performance_guidelines.html).

Use `rocprofv3` where available to inspect launch latency, occupancy, divergence,
memory stalls, and CPU/GPU overlap.

## Reproducible maximum-use profile

A `machine-max` result means maximum **measured useful** resources, not maximum
possible allocation:

- exclusive GPU access;
- physical CPU cores assigned without nested oversubscription;
- explicit affinity/NUMA policy;
- fixed OS/shared-memory safety margin;
- cgroup or equivalent whole-job limit;
- preregistered thread, cache, and batch scaling preflight; and
- a frozen choice before the policy-quality cohort is read.

If a systems change alters completed depth, chance samples, action order, or
selected moves, record two results: the engineering parity failure and the new
algorithmic candidate. Do not hide the strategy change inside a speed claim.

## Likely failure modes

- branch divergence in cascades and tree traversal;
- launch overhead dominating tiny board operations;
- random table access becoming bandwidth-bound;
- CPU and iGPU contending for LPDDR bandwidth;
- GTT/tensor growth displacing CPU caches or system headroom;
- mixed precision changing near-tied actions;
- batching breaking sibling common-random-number alignment;
- a fast model hidden behind a slower simulator; and
- comparing isolated kernel speed rather than complete experiment throughput.

All are falsifiable through the standard benchmark contract; none should be
accepted or dismissed from nominal specifications alone.
