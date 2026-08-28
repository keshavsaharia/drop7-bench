//! Deterministic within-decision parallelism for completed fair search.
//!
//! The old scheduler owns at most one root column per worker, so a decision
//! can use no more than seven CPUs and finishes with a long idle tail when one
//! column has a larger tree.  The central-frontier scheduler expands a fixed
//! public prefix of the expectimax tree, interns equal continuation states,
//! and registers the remaining subtrees in one atomic work queue.  Workers
//! claim those subtrees dynamically and the coordinator reduces their values
//! in the original column/sample order.  Scheduling can therefore change
//! cache hits and logical work, but never floating-point accumulation order,
//! a root-column value, or the selected action.

use crate::board::{BOARD_SIZE, EMPTY};
use crate::engine::{play_move_sampled, MinimalWaveSink, State};
use crate::rng::{sampled_next_disc, scenario_seed_for_state, StratifiedRandom};
use crate::search::{
    canonical_state, DepthTable, FairLeaf, Leaf, SearchMetrics, SearchParams, Searcher,
    COLUMN_ORDER,
};

use std::collections::HashMap;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Barrier, Mutex};
use std::time::Instant;

const SHALLOW_TASKS_PER_WORKER: usize = 32;
const DEEP_TASKS_PER_WORKER: usize = 4;
pub const DEFAULT_MAX_FRONTIER_TASKS: usize = 1_000_000;
pub const DEFAULT_MAX_HOST_BYTES: usize = 8 * 1024 * 1024 * 1024;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ParallelScheduler {
    RootColumns,
    CentralFrontier,
}

#[derive(Clone, Copy, Debug)]
pub struct ParallelConfig {
    pub threads: usize,
    pub table_capacity_per_worker: usize,
    pub table_from_depth: i32,
    /// None chooses the shallowest split with an adaptive queueing cushion:
    /// 32 tasks/worker for shallow continuations and 4 for expensive d5+ work.
    pub split_plies: Option<usize>,
    pub max_frontier_tasks: usize,
    pub max_host_bytes: usize,
}

impl Default for ParallelConfig {
    fn default() -> Self {
        Self {
            threads: std::thread::available_parallelism().map_or(1, |n| n.get()),
            table_capacity_per_worker: 262_144,
            table_from_depth: 1,
            split_plies: None,
            max_frontier_tasks: DEFAULT_MAX_FRONTIER_TASKS,
            max_host_bytes: DEFAULT_MAX_HOST_BYTES,
        }
    }
}

#[derive(Clone, Debug, Default)]
pub struct WorkerMetrics {
    pub worker: usize,
    pub tasks: u64,
    pub busy_seconds: f64,
    pub work: u64,
    pub nodes: u64,
    pub leaf_calls: u64,
    pub move_calls: u64,
    pub cache_hits: u64,
}

#[derive(Clone, Debug)]
pub struct ParallelMetrics {
    pub scheduler: ParallelScheduler,
    pub action: i32,
    pub completed_depth: i32,
    pub requested_threads: usize,
    pub worker_threads: usize,
    pub split_plies: usize,
    pub frontier_tasks: usize,
    pub completed_tasks: usize,
    pub planner_nodes: u64,
    pub planner_work: u64,
    pub planner_move_calls: u64,
    pub planner_leaf_calls: u64,
    pub planner_cache_hits: u64,
    pub work: u64,
    pub nodes: u64,
    pub leaf_calls: u64,
    pub move_calls: u64,
    pub cache_hits: u64,
    pub table_bytes_per_worker: usize,
    pub projected_table_bytes: usize,
    pub projected_plan_bytes: usize,
    pub initialization_seconds: f64,
    pub planning_seconds: f64,
    pub execution_seconds: f64,
    pub reduction_seconds: f64,
    pub wall_seconds: f64,
    pub worker_busy_fraction: f64,
    pub tail_idle_core_seconds: f64,
    pub workers: Vec<WorkerMetrics>,
}

#[derive(Clone, Debug)]
pub struct ParallelDecision {
    pub action: i32,
    /// Canonical columns in COLUMN_ORDER, before the action is unmirrored.
    pub column_values: Vec<(usize, f64)>,
    pub metrics: ParallelMetrics,
}

#[derive(Clone, Copy, Debug)]
pub struct ParallelResourcePlan {
    pub requested_threads: usize,
    pub split_plies: usize,
    pub worst_case_frontier_tasks: usize,
    pub table_bytes_per_worker: usize,
    pub projected_table_bytes: usize,
    pub projected_plan_bytes: usize,
    pub projected_total_bytes: usize,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
struct StateKey {
    cols: [u32; BOARD_SIZE],
    next_disc: u8,
    moves_remaining: i32,
    depth: i32,
}

impl StateKey {
    fn new(state: &State, depth: i32) -> Self {
        Self {
            cols: state.board.cols,
            next_disc: state.next_disc,
            moves_remaining: state.moves_remaining,
            depth,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
struct PlanKey {
    state: StateKey,
    split_plies: usize,
}

#[derive(Clone, Copy)]
struct FrontierTask {
    state: State,
    depth: i32,
}

enum PlanNode {
    Constant(f64),
    Task(usize),
    Add { immediate: f64, child: usize },
    Mean { children: Vec<usize>, divisor: f64 },
    Max { children: Vec<usize> },
}

struct PlanBuilder {
    params: SearchParams,
    max_tasks: usize,
    tasks: Vec<FrontierTask>,
    task_ids: HashMap<StateKey, usize>,
    nodes: Vec<PlanNode>,
    planned_states: HashMap<PlanKey, usize>,
    child_refs: usize,
    planner_nodes: u64,
    planner_work: u64,
    planner_move_calls: u64,
    planner_leaf_calls: u64,
    planner_cache_hits: u64,
    leaf: FairLeaf,
}

impl PlanBuilder {
    fn new(params: SearchParams, max_tasks: usize) -> Self {
        Self {
            params,
            max_tasks,
            tasks: Vec::new(),
            task_ids: HashMap::new(),
            nodes: Vec::new(),
            planned_states: HashMap::new(),
            child_refs: 0,
            planner_nodes: 0,
            planner_work: 0,
            planner_move_calls: 0,
            planner_leaf_calls: 0,
            planner_cache_hits: 0,
            leaf: FairLeaf::default(),
        }
    }

    fn push_node(&mut self, node: PlanNode) -> usize {
        let id = self.nodes.len();
        self.nodes.push(node);
        id
    }

    fn constant(&mut self, value: f64) -> usize {
        self.push_node(PlanNode::Constant(value))
    }

    fn frontier(&mut self, state: &State, depth: i32) -> Result<usize, String> {
        let key = StateKey::new(state, depth);
        let task = if let Some(&task) = self.task_ids.get(&key) {
            task
        } else {
            if self.tasks.len() >= self.max_tasks {
                return Err(format!(
                    "frontier exceeded the configured {}-task bound",
                    self.max_tasks
                ));
            }
            let task = self.tasks.len();
            self.tasks.push(FrontierTask {
                state: *state,
                depth,
            });
            self.task_ids.insert(key, task);
            task
        };
        Ok(self.push_node(PlanNode::Task(task)))
    }

    fn action(
        &mut self,
        state: &State,
        column: usize,
        depth: i32,
        split_plies: usize,
    ) -> Result<usize, String> {
        let state_seed = scenario_seed_for_state(
            &state.board,
            state.next_disc,
            state.moves_remaining,
            self.params.policy_seed,
            depth,
        );
        let mut samples = Vec::with_capacity(self.params.chance_samples as usize);
        for sample in 0..self.params.chance_samples {
            let mut random = StratifiedRandom {
                seed: state_seed,
                sample,
                count: self.params.chance_samples,
                event: 0,
            };
            let mut sink = MinimalWaveSink::default();
            let played = play_move_sampled(state, column, &mut random, &mut sink);
            self.planner_work += 1;
            self.planner_move_calls += 1;
            let sample_node = match played {
                None => self.constant(self.params.terminal_utility),
                Some(move_result) if move_result.state.game_over => {
                    self.constant(move_result.score_delta as f64 + self.params.terminal_utility)
                }
                Some(move_result) => {
                    let mut next = move_result.state;
                    next.score = 0;
                    next.next_disc =
                        sampled_next_disc(state_seed, sample, self.params.chance_samples);
                    let next = canonical_state(&next).0;
                    let child = self.best_future(&next, depth - 1, split_plies)?;
                    self.push_node(PlanNode::Add {
                        immediate: move_result.score_delta as f64,
                        child,
                    })
                }
            };
            samples.push(sample_node);
        }
        self.child_refs += samples.len();
        Ok(self.push_node(PlanNode::Mean {
            children: samples,
            divisor: self.params.chance_samples as f64,
        }))
    }

    fn best_future(
        &mut self,
        state: &State,
        depth: i32,
        split_plies: usize,
    ) -> Result<usize, String> {
        self.planner_nodes += 1;
        if state.game_over {
            return Ok(self.constant(self.params.terminal_utility));
        }
        if split_plies == 0 {
            return self.frontier(state, depth);
        }
        if depth == 0 {
            self.planner_work += 1;
            self.planner_leaf_calls += 1;
            let value = self.leaf.value(state);
            if !value.is_finite() {
                return Err("leaf evaluator returned a non-finite value".into());
            }
            return Ok(self.constant(value));
        }

        let key = PlanKey {
            state: StateKey::new(state, depth),
            split_plies,
        };
        if let Some(&node) = self.planned_states.get(&key) {
            self.planner_cache_hits += 1;
            return Ok(node);
        }

        let mut actions = Vec::with_capacity(BOARD_SIZE);
        for &column in COLUMN_ORDER.iter() {
            if state.board.get(0, column) != EMPTY {
                continue;
            }
            actions.push(self.action(state, column, depth, split_plies - 1)?);
        }
        let node = if actions.is_empty() {
            self.constant(self.params.terminal_utility)
        } else {
            self.child_refs += actions.len();
            self.push_node(PlanNode::Max { children: actions })
        };
        self.planned_states.insert(key, node);
        Ok(node)
    }

    fn root_columns(
        &mut self,
        canonical: &State,
        depth: i32,
        split_plies: usize,
    ) -> Result<Vec<(usize, usize)>, String> {
        let mut roots = Vec::with_capacity(BOARD_SIZE);
        for &column in COLUMN_ORDER.iter() {
            if canonical.board.get(0, column) == EMPTY {
                roots.push((column, self.action(canonical, column, depth, split_plies)?));
            }
        }
        Ok(roots)
    }

    fn projected_bytes(&self) -> usize {
        self.tasks
            .len()
            .saturating_mul(std::mem::size_of::<FrontierTask>())
            .saturating_add(
                self.nodes
                    .len()
                    .saturating_mul(std::mem::size_of::<PlanNode>()),
            )
            .saturating_add(self.child_refs.saturating_mul(std::mem::size_of::<usize>()))
            .saturating_add(
                self.tasks
                    .len()
                    .saturating_mul(std::mem::size_of::<Mutex<Option<TaskOutcome>>>()),
            )
    }
}

#[derive(Clone, Copy)]
struct TaskOutcome {
    value: f64,
}

fn reduce_node(
    node: usize,
    nodes: &[PlanNode],
    outcomes: &[TaskOutcome],
    memo: &mut [Option<f64>],
) -> f64 {
    if let Some(value) = memo[node] {
        return value;
    }
    let value = match &nodes[node] {
        PlanNode::Constant(value) => *value,
        PlanNode::Task(task) => outcomes[*task].value,
        PlanNode::Add { immediate, child } => {
            *immediate + reduce_node(*child, nodes, outcomes, memo)
        }
        PlanNode::Mean { children, divisor } => {
            let mut total = 0.0f64;
            for &child in children {
                total += reduce_node(child, nodes, outcomes, memo);
            }
            total / *divisor
        }
        PlanNode::Max { children } => {
            let mut best = f64::NEG_INFINITY;
            for &child in children {
                let value = reduce_node(child, nodes, outcomes, memo);
                if value > best {
                    best = value;
                }
            }
            if best.is_finite() {
                best
            } else {
                -1_000_000.0
            }
        }
    };
    memo[node] = Some(value);
    value
}

fn checked_table_bytes(config: ParallelConfig, workers: usize) -> Result<(usize, usize), String> {
    if config.threads == 0 {
        return Err("--threads must be at least 1".into());
    }
    if config.threads > 4096 {
        return Err("--threads above the hard 4096-worker safety bound".into());
    }
    if config.max_frontier_tasks == 0 {
        return Err("--max-frontier-tasks must be at least 1".into());
    }
    let per_worker = DepthTable::projected_bytes(config.table_capacity_per_worker)
        .ok_or_else(|| "transposition-table capacity overflows usize".to_string())?;
    let total = per_worker
        .checked_mul(workers)
        .ok_or_else(|| "projected transposition-table memory overflows usize".to_string())?;
    if total > config.max_host_bytes {
        return Err(format!(
            "worker tables need {} bytes ({} x {}), above the declared {}-byte host budget",
            total, workers, per_worker, config.max_host_bytes
        ));
    }
    Ok((per_worker, total))
}

pub fn recommended_split_plies(
    depth: i32,
    chance_samples: i32,
    threads: usize,
    max_frontier_tasks: usize,
) -> Result<usize, String> {
    if depth < 1 || chance_samples < 1 {
        return Err("depth and chance samples must both be at least 1".into());
    }
    let branch = BOARD_SIZE
        .checked_mul(chance_samples as usize)
        .ok_or_else(|| "branching factor overflows usize".to_string())?;
    // Deep continuation tasks are seconds-to-minutes of work apiece and need
    // only a small queueing cushion. Shallow tasks are microseconds and need a
    // wider frontier to amortize scheduling and smooth the tail. This avoids
    // the d5 failure mode where expanding one extra prefix ply raised logical
    // work by ~41% merely to manufacture thousands of already-tiny tasks.
    let tasks_per_worker = if depth >= 5 {
        DEEP_TASKS_PER_WORKER
    } else {
        SHALLOW_TASKS_PER_WORKER
    };
    let target = threads
        .max(1)
        .checked_mul(tasks_per_worker)
        .ok_or_else(|| "task target overflows usize".to_string())?;
    let mut split = 0usize;
    let mut tasks = branch;
    while tasks < target && split < (depth - 1) as usize {
        let Some(next) = tasks.checked_mul(branch) else {
            break;
        };
        if next > max_frontier_tasks {
            break;
        }
        tasks = next;
        split += 1;
    }
    Ok(split)
}

/// Seed-free, allocation-free upper-bound plan for an all-seven-column root.
/// The task-graph allowance is deliberately conservative; the exact planner
/// repeats the check with its actual deduplicated frontier before allocation.
pub fn plan_parallel_resources(
    params: SearchParams,
    config: ParallelConfig,
) -> Result<ParallelResourcePlan, String> {
    let split_plies = match config.split_plies {
        Some(split) => split,
        None => recommended_split_plies(
            params.depth,
            params.chance_samples,
            config.threads,
            config.max_frontier_tasks,
        )?,
    };
    if split_plies > (params.depth - 1).max(0) as usize {
        return Err(format!(
            "split depth {} exceeds the {} expandable internal plies at search depth {}",
            split_plies,
            (params.depth - 1).max(0),
            params.depth
        ));
    }
    let branch = BOARD_SIZE
        .checked_mul(params.chance_samples as usize)
        .ok_or_else(|| "branching factor overflows usize".to_string())?;
    let mut tasks = branch;
    for _ in 0..split_plies {
        tasks = tasks
            .checked_mul(branch)
            .ok_or_else(|| "frontier task bound overflows usize".to_string())?;
    }
    if tasks > config.max_frontier_tasks {
        return Err(format!(
            "worst-case frontier needs {tasks} tasks, above the configured {}-task bound",
            config.max_frontier_tasks
        ));
    }
    let workers = config.threads.min(tasks).max(1);
    let (table_bytes_per_worker, projected_table_bytes) = checked_table_bytes(config, workers)?;
    let projected_plan_bytes = tasks
        .checked_mul(1024)
        .ok_or_else(|| "projected plan memory overflows usize".to_string())?;
    let projected_total_bytes = projected_table_bytes
        .checked_add(projected_plan_bytes)
        .ok_or_else(|| "projected total memory overflows usize".to_string())?;
    if projected_total_bytes > config.max_host_bytes {
        return Err(format!(
            "projected tables + worst-case frontier need {projected_total_bytes} bytes, above the declared {}-byte host budget",
            config.max_host_bytes
        ));
    }
    Ok(ParallelResourcePlan {
        requested_threads: config.threads,
        split_plies,
        worst_case_frontier_tasks: tasks,
        table_bytes_per_worker,
        projected_table_bytes,
        projected_plan_bytes,
        projected_total_bytes,
    })
}

fn aggregate_workers(
    workers: &[WorkerMetrics],
    planner_work: u64,
    planner_nodes: u64,
    planner_leaf_calls: u64,
    planner_move_calls: u64,
    execution_seconds: f64,
) -> (u64, u64, u64, u64, u64, f64, f64) {
    let mut work = planner_work;
    let mut nodes = planner_nodes;
    let mut leaf_calls = planner_leaf_calls;
    let mut move_calls = planner_move_calls;
    let mut cache_hits = 0u64;
    let mut busy = 0.0f64;
    for worker in workers {
        work += worker.work;
        nodes += worker.nodes;
        leaf_calls += worker.leaf_calls;
        move_calls += worker.move_calls;
        cache_hits += worker.cache_hits;
        busy += worker.busy_seconds;
    }
    let capacity = execution_seconds * workers.len() as f64;
    let busy_fraction = if capacity > 0.0 {
        (busy / capacity).clamp(0.0, 1.0)
    } else {
        0.0
    };
    let tail_idle = (capacity - busy).max(0.0);
    (
        work,
        nodes,
        leaf_calls,
        move_calls,
        cache_hits,
        busy_fraction,
        tail_idle,
    )
}

pub fn choose_action_frontier_parallel(
    source: &State,
    params: SearchParams,
    config: ParallelConfig,
) -> Result<ParallelDecision, String> {
    choose_action_frontier_parallel_with_leaf(source, params, config, FairLeaf::default)
}

pub fn choose_action_frontier_parallel_with_leaf<L, F>(
    source: &State,
    params: SearchParams,
    config: ParallelConfig,
    make_leaf: F,
) -> Result<ParallelDecision, String>
where
    L: Leaf + Send,
    F: Fn() -> L + Sync,
{
    let total_start = Instant::now();
    if source.game_over {
        return Err("cannot search a terminal state".into());
    }
    let (canonical, mirrored) = canonical_state(source);
    let legal = COLUMN_ORDER
        .iter()
        .filter(|&&column| canonical.board.get(0, column) == EMPTY)
        .count();
    if legal == 0 {
        return Err("cannot search a state with no legal columns".into());
    }
    let split_plies = match config.split_plies {
        Some(split) => split,
        None => recommended_split_plies(
            params.depth,
            params.chance_samples,
            config.threads,
            config.max_frontier_tasks,
        )?,
    };
    if split_plies > (params.depth - 1).max(0) as usize {
        return Err(format!(
            "split depth {} exceeds the {} expandable internal plies at search depth {}",
            split_plies,
            (params.depth - 1).max(0),
            params.depth
        ));
    }
    let _resource_plan = plan_parallel_resources(params, config)?;

    let planning_start = Instant::now();
    let mut planner = PlanBuilder::new(params, config.max_frontier_tasks);
    let roots = planner.root_columns(&canonical, params.depth, split_plies)?;
    let planning_seconds = planning_start.elapsed().as_secs_f64();
    // An empty frontier is legal near the end of a game: every continuation
    // inside the split prefix reached a terminal state or a depth-0 leaf, so
    // the plan is already a constant tree. The single worker below claims no
    // tasks and the reduction proceeds over constants alone.
    let worker_count = config.threads.min(planner.tasks.len()).max(1);
    let (table_bytes_per_worker, projected_table_bytes) =
        checked_table_bytes(config, worker_count)?;
    let projected_plan_bytes = planner.projected_bytes();
    if projected_table_bytes.saturating_add(projected_plan_bytes) > config.max_host_bytes {
        return Err(format!(
            "projected tables + frontier need {} bytes, above the declared {}-byte host budget",
            projected_table_bytes.saturating_add(projected_plan_bytes),
            config.max_host_bytes
        ));
    }

    let cursor = AtomicUsize::new(0);
    let barrier = Barrier::new(worker_count + 1);
    let results: Vec<Mutex<Option<TaskOutcome>>> =
        (0..planner.tasks.len()).map(|_| Mutex::new(None)).collect();
    let init_start = Instant::now();
    let mut initialization_seconds = 0.0f64;
    let mut execution_seconds = 0.0f64;
    let mut workers = Vec::with_capacity(worker_count);
    std::thread::scope(|scope| -> Result<(), String> {
        let mut handles = Vec::with_capacity(worker_count);
        for worker in 0..worker_count {
            let tasks = &planner.tasks;
            let cursor = &cursor;
            let barrier = &barrier;
            let results = &results;
            let make_leaf = &make_leaf;
            handles.push(scope.spawn(move || -> Result<WorkerMetrics, String> {
                let mut searcher = Searcher::new(
                    params,
                    make_leaf(),
                    DepthTable::new(config.table_capacity_per_worker, config.table_from_depth),
                );
                searcher.begin_parallel_decision();
                barrier.wait();
                let mut summary = WorkerMetrics {
                    worker,
                    ..WorkerMetrics::default()
                };
                loop {
                    let task_id = cursor.fetch_add(1, Ordering::Relaxed);
                    if task_id >= tasks.len() {
                        break;
                    }
                    let task = tasks[task_id];
                    let busy_start = Instant::now();
                    let value = searcher
                        .evaluate_state_value(&task.state, task.depth)
                        .map_err(|_| format!("worker {worker} exhausted the completion budget"))?;
                    summary.busy_seconds += busy_start.elapsed().as_secs_f64();
                    let metrics = *searcher.last_metrics();
                    summary.tasks += 1;
                    summary.work += metrics.work;
                    summary.nodes += metrics.nodes;
                    summary.leaf_calls += metrics.leaf_calls;
                    summary.move_calls += metrics.move_calls;
                    summary.cache_hits += metrics.cache_hits;
                    let mut slot = results[task_id]
                        .lock()
                        .map_err(|_| format!("task {task_id} result lock was poisoned"))?;
                    if slot.is_some() {
                        return Err(format!("frontier task {task_id} completed more than once"));
                    }
                    *slot = Some(TaskOutcome { value });
                }
                Ok(summary)
            }));
        }
        barrier.wait();
        initialization_seconds = init_start.elapsed().as_secs_f64();
        let execution_start = Instant::now();
        for handle in handles {
            let summary = handle
                .join()
                .map_err(|_| "frontier worker panicked".to_string())??;
            workers.push(summary);
        }
        execution_seconds = execution_start.elapsed().as_secs_f64();
        Ok(())
    })?;

    let outcomes: Vec<TaskOutcome> = results
        .into_iter()
        .enumerate()
        .map(|(task, slot)| {
            slot.into_inner()
                .map_err(|_| format!("task {task} result lock was poisoned"))?
                .ok_or_else(|| format!("frontier task {task} was never completed"))
        })
        .collect::<Result<_, _>>()?;

    let reduction_start = Instant::now();
    let mut memo = vec![None; planner.nodes.len()];
    let mut column_values = Vec::with_capacity(roots.len());
    let mut action = -1i32;
    let mut best = f64::NEG_INFINITY;
    for (column, root) in roots {
        let value = reduce_node(root, &planner.nodes, &outcomes, &mut memo);
        column_values.push((column, value));
        if value > best {
            best = value;
            action = column as i32;
        }
    }
    if mirrored && action >= 0 {
        action = BOARD_SIZE as i32 - 1 - action;
    }
    let reduction_seconds = reduction_start.elapsed().as_secs_f64();
    let (work, nodes, leaf_calls, move_calls, cache_hits, worker_busy_fraction, tail_idle) =
        aggregate_workers(
            &workers,
            planner.planner_work,
            planner.planner_nodes,
            planner.planner_leaf_calls,
            planner.planner_move_calls,
            execution_seconds,
        );
    let completed_tasks = workers.iter().map(|worker| worker.tasks as usize).sum();
    let metrics = ParallelMetrics {
        scheduler: ParallelScheduler::CentralFrontier,
        action,
        completed_depth: params.depth,
        requested_threads: config.threads,
        worker_threads: worker_count,
        split_plies,
        frontier_tasks: planner.tasks.len(),
        completed_tasks,
        planner_nodes: planner.planner_nodes,
        planner_work: planner.planner_work,
        planner_move_calls: planner.planner_move_calls,
        planner_leaf_calls: planner.planner_leaf_calls,
        planner_cache_hits: planner.planner_cache_hits,
        work,
        nodes,
        leaf_calls,
        move_calls,
        cache_hits,
        table_bytes_per_worker,
        projected_table_bytes,
        projected_plan_bytes,
        initialization_seconds,
        planning_seconds,
        execution_seconds,
        reduction_seconds,
        wall_seconds: total_start.elapsed().as_secs_f64(),
        worker_busy_fraction,
        tail_idle_core_seconds: tail_idle,
        workers,
    };
    Ok(ParallelDecision {
        action,
        column_values,
        metrics,
    })
}

pub fn choose_action_root_parallel(
    source: &State,
    params: SearchParams,
    config: ParallelConfig,
) -> Result<ParallelDecision, String> {
    choose_action_root_parallel_with_leaf(source, params, config, FairLeaf::default)
}

pub fn choose_action_root_parallel_with_leaf<L, F>(
    source: &State,
    params: SearchParams,
    config: ParallelConfig,
    make_leaf: F,
) -> Result<ParallelDecision, String>
where
    L: Leaf + Send,
    F: Fn() -> L + Sync,
{
    let total_start = Instant::now();
    if source.game_over {
        return Err("cannot search a terminal state".into());
    }
    let (canonical, mirrored) = canonical_state(source);
    let legal: Vec<usize> = COLUMN_ORDER
        .iter()
        .copied()
        .filter(|&column| canonical.board.get(0, column) == EMPTY)
        .collect();
    if legal.is_empty() {
        return Err("cannot search a state with no legal columns".into());
    }
    let worker_count = config.threads.min(legal.len()).max(1);
    let (table_bytes_per_worker, projected_table_bytes) =
        checked_table_bytes(config, worker_count)?;
    let init_start = Instant::now();
    let barrier = Barrier::new(worker_count + 1);
    let mut values: Vec<(usize, f64, SearchMetrics)> = Vec::with_capacity(legal.len());
    let mut workers = Vec::with_capacity(worker_count);
    let mut initialization_seconds = 0.0f64;
    let mut execution_seconds = 0.0f64;
    std::thread::scope(|scope| -> Result<(), String> {
        let mut lanes: Vec<Vec<usize>> = (0..worker_count).map(|_| Vec::new()).collect();
        for (index, &column) in legal.iter().enumerate() {
            lanes[index % worker_count].push(column);
        }
        let mut handles = Vec::with_capacity(worker_count);
        for (worker, lane) in lanes.into_iter().enumerate() {
            let canonical = &canonical;
            let barrier = &barrier;
            let make_leaf = &make_leaf;
            handles.push(scope.spawn(move || -> Result<_, String> {
                let mut searcher = Searcher::new(
                    params,
                    make_leaf(),
                    DepthTable::new(config.table_capacity_per_worker, config.table_from_depth),
                );
                searcher.begin_parallel_decision();
                barrier.wait();
                let mut out = Vec::new();
                let mut summary = WorkerMetrics {
                    worker,
                    ..WorkerMetrics::default()
                };
                for column in lane {
                    let busy_start = Instant::now();
                    let value = searcher
                        .evaluate_root_column(canonical, column, params.depth)
                        .map_err(|_| format!("worker {worker} exhausted the completion budget"))?;
                    summary.busy_seconds += busy_start.elapsed().as_secs_f64();
                    let metrics = *searcher.last_metrics();
                    summary.tasks += 1;
                    summary.work += metrics.work;
                    summary.nodes += metrics.nodes;
                    summary.leaf_calls += metrics.leaf_calls;
                    summary.move_calls += metrics.move_calls;
                    summary.cache_hits += metrics.cache_hits;
                    out.push((column, value, metrics));
                }
                Ok((out, summary))
            }));
        }
        barrier.wait();
        initialization_seconds = init_start.elapsed().as_secs_f64();
        let execution_start = Instant::now();
        for handle in handles {
            let (mut out, summary) = handle
                .join()
                .map_err(|_| "root-column worker panicked".to_string())??;
            values.append(&mut out);
            workers.push(summary);
        }
        execution_seconds = execution_start.elapsed().as_secs_f64();
        Ok(())
    })?;

    let reduction_start = Instant::now();
    let mut column_values = Vec::with_capacity(legal.len());
    let mut action = -1i32;
    let mut best = f64::NEG_INFINITY;
    for &column in COLUMN_ORDER.iter() {
        if let Some(&(_, value, _)) = values
            .iter()
            .find(|&&(candidate, _, _)| candidate == column)
        {
            column_values.push((column, value));
            if value > best {
                best = value;
                action = column as i32;
            }
        }
    }
    if mirrored && action >= 0 {
        action = BOARD_SIZE as i32 - 1 - action;
    }
    let reduction_seconds = reduction_start.elapsed().as_secs_f64();
    let (work, nodes, leaf_calls, move_calls, cache_hits, worker_busy_fraction, tail_idle) =
        aggregate_workers(&workers, 0, 0, 0, 0, execution_seconds);
    let completed_tasks = workers.iter().map(|worker| worker.tasks as usize).sum();
    let metrics = ParallelMetrics {
        scheduler: ParallelScheduler::RootColumns,
        action,
        completed_depth: params.depth,
        requested_threads: config.threads,
        worker_threads: worker_count,
        split_plies: 0,
        frontier_tasks: legal.len(),
        completed_tasks,
        planner_nodes: 0,
        planner_work: 0,
        planner_move_calls: 0,
        planner_leaf_calls: 0,
        planner_cache_hits: 0,
        work,
        nodes,
        leaf_calls,
        move_calls,
        cache_hits,
        table_bytes_per_worker,
        projected_table_bytes,
        projected_plan_bytes: 0,
        initialization_seconds,
        planning_seconds: 0.0,
        execution_seconds,
        reduction_seconds,
        wall_seconds: total_start.elapsed().as_secs_f64(),
        worker_busy_fraction,
        tail_idle_core_seconds: tail_idle,
        workers,
    };
    Ok(ParallelDecision {
        action,
        column_values,
        metrics,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::board::Board;
    use crate::search::work_bound_for;

    fn params(depth: i32, strata: i32) -> SearchParams {
        SearchParams {
            depth,
            chance_samples: strata,
            terminal_utility: -1_000_000.0,
            maximum_work: work_bound_for(depth, strata) + 1,
            policy_seed: 0xd707_5eed,
        }
    }

    fn config(threads: usize, split_plies: Option<usize>) -> ParallelConfig {
        ParallelConfig {
            threads,
            table_capacity_per_worker: 1024,
            split_plies,
            max_host_bytes: 128 * 1024 * 1024,
            ..ParallelConfig::default()
        }
    }

    #[test]
    fn frontier_values_match_root_values_bit_for_bit() {
        let state = State::initial_headless(0xa527_7003);
        let root = choose_action_root_parallel(&state, params(3, 3), config(3, Some(0)))
            .expect("root scheduler");
        let frontier = choose_action_frontier_parallel(&state, params(3, 3), config(4, Some(1)))
            .expect("frontier scheduler");
        assert_eq!(frontier.action, root.action);
        assert_eq!(frontier.column_values.len(), root.column_values.len());
        for ((fc, fv), (rc, rv)) in frontier.column_values.iter().zip(root.column_values.iter()) {
            assert_eq!(fc, rc);
            assert_eq!(fv.to_bits(), rv.to_bits());
        }
        assert_eq!(
            frontier.metrics.frontier_tasks,
            frontier.metrics.completed_tasks
        );
        assert!(frontier.metrics.frontier_tasks > BOARD_SIZE);
    }

    #[test]
    fn frontier_is_worker_count_independent() {
        let state = State::initial_headless(0xa527_7004);
        let one = choose_action_frontier_parallel(&state, params(3, 3), config(1, Some(1)))
            .expect("one worker");
        let four = choose_action_frontier_parallel(&state, params(3, 3), config(4, Some(1)))
            .expect("four workers");
        assert_eq!(one.action, four.action);
        for ((oc, ov), (fc, fv)) in one.column_values.iter().zip(four.column_values.iter()) {
            assert_eq!(oc, fc);
            assert_eq!(ov.to_bits(), fv.to_bits());
        }
    }

    #[test]
    fn near_terminal_prefix_with_no_frontier_tasks_still_decides() {
        // gauntlet-01 crash position: the rise clock is 2 and every column but
        // one is stacked to the top row, so all continuations terminate inside
        // the split prefix and the planner registers zero frontier tasks.
        let board =
            Board::from_serialized("0511020019921001889910388888018888809888889888888")
                .expect("board");
        let state = State {
            board,
            next_disc: 1,
            score: 0,
            level: 1,
            moves_remaining: 2,
            moves_played: 0,
            game_over: false,
        };
        let frontier = choose_action_frontier_parallel(&state, params(6, 7), config(4, Some(1)))
            .expect("empty frontier must reduce over constants, not fail");
        assert_eq!(frontier.metrics.frontier_tasks, 0);
        assert!(frontier.action >= 0);
        let root = choose_action_root_parallel(&state, params(6, 7), config(4, Some(0)))
            .expect("root scheduler");
        assert_eq!(frontier.action, root.action);
        for ((fc, fv), (rc, rv)) in frontier.column_values.iter().zip(root.column_values.iter()) {
            assert_eq!(fc, rc);
            assert_eq!(fv.to_bits(), rv.to_bits());
        }
    }

    #[test]
    fn memory_unsafe_parallel_config_is_rejected_before_allocation() {
        let state = State::initial_headless(0xa527_7005);
        let error = choose_action_frontier_parallel(
            &state,
            params(2, 3),
            ParallelConfig {
                threads: 192,
                table_capacity_per_worker: 1 << 24,
                split_plies: Some(1),
                max_host_bytes: 1024 * 1024,
                ..ParallelConfig::default()
            },
        )
        .expect_err("oversized tables must fail");
        assert!(error.contains("host budget"));
    }
}
