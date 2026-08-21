# Cluster shapes

Detect the environment; never assume it from a name. Record, for every node
type: CPU model/cores/threads, RAM, GPU model and count, driver and runtime
(CUDA or ROCm version), interconnect, filesystem (shared vs local), scheduler
and its limits, container runtime, and the machine profile from
`make research-doctor`. Performance numbers without this profile are not
retained.

## Single multi-GPU machine

- Actors: one process per physical core minus two for the learner/orchestrator;
  `games_per_actor` sized so the working set fits L3; no nested thread pools
  (`OMP_NUM_THREADS=1` in actors).
- Learner: `torchrun --nproc_per_node=<gpus>`; NCCL on NVIDIA, RCCL on AMD.
- Shards on local NVMe; evaluator reads them directly.
- This is also the workstation pilot with `devices: 1`.

## SLURM

- One job array for actors (`--array=0-N`, each task reads `actors.count` and
  its index), one job for the learner (`--gres=gpu:<n>`), one short job per
  gate; dependencies via `--dependency=afterok`.
- Shards on the shared parallel filesystem; write locally, then move on
  rotation to avoid small-file pressure.
- Respect the site's wall-time limits by checkpointing inside the limit;
  `budget.wall_hours` must be below it.
- Record `SLURM_JOB_ID`, partition, node list, and `scontrol show job` output.

## Kubernetes

- Actors as a `Job` with `parallelism: N` and an indexed completion mode; the
  learner as a `StatefulSet` (or a `PyTorchJob` if the training operator is
  installed) with GPU resource requests; the gate as a `CronJob` or a
  one-shot `Job` triggered by the orchestrator.
- Shards on a `ReadWriteMany` volume or an object store; manifests committed
  to the store only after the shard is sealed.
- Pin container images by digest; the simulator gate report is produced inside
  the same image that the actors run.

## Ray

- Actors as Ray actors with CPU resources only; the learner as a `TorchTrainer`
  with `num_workers = devices`; shards through the object store or a shared
  volume; the orchestrator is the driver.
- Ray autoscaling changes `actors.count` at runtime; the configuration file
  still fixes search, data, and gate fields, which must not vary with count.

## Cloud spot / preemptible pools

- Actors are stateless and safe to preempt; shards are sealed on rotation, and
  an unsealed shard is discarded, never repaired.
- The learner checkpoints every N minutes and resumes from the last
  checkpoint; the gate is re-run if interrupted.
- Cost per retained game and per accepted shard is part of the run record.

## What must not be assumed

- That a GPU simulator exists or is faster. Actors use the CPU batch simulator
  until a GPU version passes the differential gates and beats it end to end.
- That shared APU memory is two pools. On integrated GPUs, host RAM and GPU
  memory are one pool; report them once.
- That more actors means better data. `label: every_sibling` and shared chance
  scenarios are what make the data useful; count only scales volume.
