/**
 * Per-approach card art.
 *
 * Most approach pages carry a theory of their own, so most have an art that
 * draws that theory and nothing else. A directory with no entry here falls
 * back to the art of its technique, which is the right default for a page
 * that is one example of a general idea rather than a mechanism of its own.
 *
 * Everything about these arts matches the technique arts: a server component
 * returning one inline SVG on the 320x180 frame, `data-anim` on every animated
 * element, keyframes prefixed `tart-approach-<slug>-` in the art's own
 * `<slug>.css`, a resting frame that is what the SVG's own attributes draw,
 * and the shared play/pause contract from `../art.css`. The full contract is
 * `.agents/skills/drop7-web-console/references/card-art.md`, and
 * `web/scripts/check-art.mjs` holds the machine-checkable part of it.
 *
 * Keys are `"<family>/<slug>"`, matching the approach directory on disk.
 * `TechniqueArt` looks one up when it is given an `approach` prop, so adding
 * a key here is the whole of adding an art.
 */
import type { ReactElement } from "react";
import type { ArtProps } from "../registry";
import { AccessibleEnergyArt } from "./AccessibleEnergyArt";
import { AfterstateNetArt } from "./AfterstateNetArt";
import { BellmanNtupleArt } from "./BellmanNtupleArt";
import { CemArt } from "./CemArt";
import { ChainRevealLeafArt } from "./ChainRevealLeafArt";
import { ChanceStateNnueArt } from "./ChanceStateNnueArt";
import { ChanceStrataArt } from "./ChanceStrataArt";
import { ConservativeFittedPolicyIterationArt } from "./ConservativeFittedPolicyIterationArt";
import { ConstructiveSpectrumArt } from "./ConstructiveSpectrumArt";
import { CriticalRiskArt } from "./CriticalRiskArt";
import { CurriculumOptionPpoArt } from "./CurriculumOptionPpoArt";
import { CycleAbstractionArt } from "./CycleAbstractionArt";
import { D4DistillationArt } from "./D4DistillationArt";
import { D4QCloneArt } from "./D4QCloneArt";
import { DenoisedValueArt } from "./DenoisedValueArt";
import { DirectPolicyArt } from "./DirectPolicyArt";
import { DistributionalAfterstateArt } from "./DistributionalAfterstateArt";
import { DqnArt } from "./DqnArt";
import { EdgePriorityArt } from "./EdgePriorityArt";
import { EvolutionApproachArt } from "./EvolutionApproachArt";
import { EvolvedPublicPolicyArt } from "./EvolvedPublicPolicyArt";
import { ExactSearchArt } from "./ExactSearchArt";
import { FairExpectimaxReferenceArt } from "./FairExpectimaxReferenceArt";
import { FairPolicyArt } from "./FairPolicyArt";
import { FullActionTermsArt } from "./FullActionTermsArt";
import { GrayThroughputArt } from "./GrayThroughputArt";
import { H200SiblingNnueArt } from "./H200SiblingNnueArt";
import { HindsightPlannerArt } from "./HindsightPlannerArt";
import { KleinFriedmannLinearQArt } from "./KleinFriedmannLinearQArt";
import { LeafEvolutionArt } from "./LeafEvolutionArt";
import { LeafReweightArt } from "./LeafReweightArt";
import { LearnedLeafArt } from "./LearnedLeafArt";
import { LongOutcomeArt } from "./LongOutcomeArt";
import { ManifoldPpoArt } from "./ManifoldPpoArt";
import { MctsApproachArt } from "./MctsApproachArt";
import { MonteCarloReturnArt } from "./MonteCarloReturnArt";
import { MonteCarloValueArt } from "./MonteCarloValueArt";
import { NativePpoArt } from "./NativePpoArt";
import { NnueEvolutionArt } from "./NnueEvolutionArt";
import { NnueGuidedArt } from "./NnueGuidedArt";
import { ObservableMctsArt } from "./ObservableMctsArt";
import { OneplyQPruneArt } from "./OneplyQPruneArt";
import { OpenLoopArt } from "./OpenLoopArt";
import { OptimisticPhaseArt } from "./OptimisticPhaseArt";
import { OracleDaggerArt } from "./OracleDaggerArt";
import { OracleDistillationApproachArt } from "./OracleDistillationApproachArt";
import { PanelValueArt } from "./PanelValueArt";
import { PerfectInformationOracleArt } from "./PerfectInformationOracleArt";
import { PhaseBlendArt } from "./PhaseBlendArt";
import { PhaseDistillationArt } from "./PhaseDistillationArt";
import { PhaseFairCombinationArt } from "./PhaseFairCombinationArt";
import { PhaseHorizonArt } from "./PhaseHorizonArt";
import { PlannerDistillArt } from "./PlannerDistillArt";
import { PrimalDualActorCriticArt } from "./PrimalDualActorCriticArt";
import { PublicRegenerativeB0Art } from "./PublicRegenerativeB0Art";
import { PublicRolloutPolicyIterationArt } from "./PublicRolloutPolicyIterationArt";
import { PublicSurvivalRolloutArt } from "./PublicSurvivalRolloutArt";
import { PuctArt } from "./PuctArt";
import { RainbowQArt } from "./RainbowQArt";
import { RegenerativeExpertIterationArt } from "./RegenerativeExpertIterationArt";
import { RevealSamplingArt } from "./RevealSamplingArt";
import { RiseOptionQdArt } from "./RiseOptionQdArt";
import { RiskCalibrationArt } from "./RiskCalibrationArt";
import { RiskSensitiveArt } from "./RiskSensitiveArt";
import { RolloutArt } from "./RolloutArt";
import { RolloutImprovementArt } from "./RolloutImprovementArt";
import { RolloutVeto17kArt } from "./RolloutVeto17kArt";
import { RolloutVetoArt } from "./RolloutVetoArt";
import { RootRiskArt } from "./RootRiskArt";
import { SelectiveDepthArt } from "./SelectiveDepthArt";
import { SiblingAdvantageArt } from "./SiblingAdvantageArt";
import { SparseExpectimaxArt } from "./SparseExpectimaxArt";
import { StructuralTerminalVetoArt } from "./StructuralTerminalVetoArt";
import { StructuredNnueArt } from "./StructuredNnueArt";
import { SurvivalInstinctArt } from "./SurvivalInstinctArt";
import { TailSurvivalCemArt } from "./TailSurvivalCemArt";
import { TemporalCoherenceArt } from "./TemporalCoherenceArt";
import { TerminalPolicyIterationArt } from "./TerminalPolicyIterationArt";
import { TerminalRolloutArt } from "./TerminalRolloutArt";
import { TopologyArt } from "./TopologyArt";
import { TorchPpoArt } from "./TorchPpoArt";
import { TransitionRewardsArt } from "./TransitionRewardsArt";
import { TunnelingArt } from "./TunnelingArt";
import { VerticalLadderArt } from "./VerticalLadderArt";
import { VerticalReservoirArt } from "./VerticalReservoirArt";
import { ViabilityControllerArt } from "./ViabilityControllerArt";
import { VirtualIgnitionArt } from "./VirtualIgnitionArt";

export const APPROACH_ART: Record<string, (props: ArtProps) => ReactElement> = {
  "afterstate-learning/distributional-afterstate": DistributionalAfterstateArt,
  "constructive-reservoir/constructive-spectrum": ConstructiveSpectrumArt,
  "constructive-reservoir/panel-value": PanelValueArt,
  "constructive-reservoir/rise-option-qd": RiseOptionQdArt,
  "constructive-reservoir/structural-terminal-veto": StructuralTerminalVetoArt,
  "constructive-reservoir/tail-survival-cem": TailSurvivalCemArt,
  "constructive-reservoir/vertical-reservoir": VerticalReservoirArt,
  "constructive-reservoir/viability-controller": ViabilityControllerArt,
  "d4-long-outcome/d4-distillation": D4DistillationArt,
  "d4-long-outcome/h200-sibling-nnue": H200SiblingNnueArt,
  "d4-long-outcome/long-outcome": LongOutcomeArt,
  "d4-long-outcome/rollout-veto": RolloutVetoArt,
  "fair-expectimax/cem": CemArt,
  "fair-expectimax/chance-strata": ChanceStrataArt,
  "fair-expectimax/fair-policy": FairPolicyArt,
  "fair-expectimax/full-action-terms": FullActionTermsArt,
  "fair-expectimax/phase-fair-combination": PhaseFairCombinationArt,
  "fair-expectimax/reference": FairExpectimaxReferenceArt,
  "fair-expectimax/rollout-improvement": RolloutImprovementArt,
  "fair-expectimax/root-risk": RootRiskArt,
  "fair-expectimax/selective-depth": SelectiveDepthArt,
  "fair-expectimax/transition-rewards": TransitionRewardsArt,
  "fair-expectimax/vertical-ladder": VerticalLadderArt,
  "heuristic-search/critical-risk": CriticalRiskArt,
  "heuristic-search/cycle-abstraction": CycleAbstractionArt,
  "heuristic-search/edge-priority": EdgePriorityArt,
  "heuristic-search/evolution": EvolutionApproachArt,
  "heuristic-search/evolved-public-policy": EvolvedPublicPolicyArt,
  "heuristic-search/exact-search": ExactSearchArt,
  "heuristic-search/gray-throughput": GrayThroughputArt,
  "heuristic-search/open-loop": OpenLoopArt,
  "heuristic-search/phase-horizon": PhaseHorizonArt,
  "heuristic-search/risk-sensitive": RiskSensitiveArt,
  "heuristic-search/rollout": RolloutArt,
  "heuristic-search/sparse-expectimax": SparseExpectimaxArt,
  "heuristic-search/tunneling": TunnelingArt,
  "heuristic-search/virtual-ignition": VirtualIgnitionArt,
  "lifetime-objective/afterstate-net": AfterstateNetArt,
  "lifetime-objective/chain-reveal-leaf": ChainRevealLeafArt,
  "lifetime-objective/leaf-evolution": LeafEvolutionArt,
  "lifetime-objective/leaf-reweight": LeafReweightArt,
  "lifetime-objective/learned-leaf": LearnedLeafArt,
  "lifetime-objective/nnue-evolution": NnueEvolutionArt,
  "lifetime-objective/planner-distill": PlannerDistillArt,
  "lifetime-objective/reveal-sampling": RevealSamplingArt,
  "lifetime-objective/risk-calibration": RiskCalibrationArt,
  "lifetime-objective/rollout-veto-17k": RolloutVeto17kArt,
  "lifetime-objective/survival-instinct": SurvivalInstinctArt,
  "ntuple-rl/bellman-ntuple": BellmanNtupleArt,
  "ntuple-rl/curriculum-option-ppo": CurriculumOptionPpoArt,
  "ntuple-rl/manifold-ppo": ManifoldPpoArt,
  "ntuple-rl/native-ppo": NativePpoArt,
  "ntuple-rl/optimistic-phase": OptimisticPhaseArt,
  "ntuple-rl/phase-blend": PhaseBlendArt,
  "ntuple-rl/primal-dual-actor-critic": PrimalDualActorCriticArt,
  "ntuple-rl/rainbow-q": RainbowQArt,
  "ntuple-rl/regenerative-expert-iteration": RegenerativeExpertIterationArt,
  "ntuple-rl/temporal-coherence": TemporalCoherenceArt,
  "ntuple-rl/torch-ppo": TorchPpoArt,
  "oracle-curriculum/accessible-energy": AccessibleEnergyArt,
  "oracle-curriculum/hindsight-planner": HindsightPlannerArt,
  "oracle-curriculum/oracle-dagger": OracleDaggerArt,
  "oracle-curriculum/oracle-distillation": OracleDistillationApproachArt,
  "oracle-curriculum/perfect-information-oracle": PerfectInformationOracleArt,
  "oracle-curriculum/topology": TopologyArt,
  "terminal-policy-iteration/public-regenerative-b0": PublicRegenerativeB0Art,
  "terminal-policy-iteration/public-rollout-policy-iteration": PublicRolloutPolicyIterationArt,
  "terminal-policy-iteration/public-survival-rollout": PublicSurvivalRolloutArt,
  "terminal-policy-iteration/terminal-policy-iteration": TerminalPolicyIterationArt,
  "terminal-policy-iteration/terminal-rollout": TerminalRolloutArt,
  "tree-search/mcts": MctsApproachArt,
  "tree-search/nnue-guided": NnueGuidedArt,
  "tree-search/observable-mcts": ObservableMctsArt,
  "tree-search/puct": PuctArt,
  "value-policy-learning/chance-state-nnue": ChanceStateNnueArt,
  "value-policy-learning/conservative-fitted-policy-iteration": ConservativeFittedPolicyIterationArt,
  "value-policy-learning/d4-q-clone": D4QCloneArt,
  "value-policy-learning/denoised-value": DenoisedValueArt,
  "value-policy-learning/direct-policy": DirectPolicyArt,
  "value-policy-learning/dqn": DqnArt,
  "value-policy-learning/klein-friedmann-linear-q": KleinFriedmannLinearQArt,
  "value-policy-learning/monte-carlo-return": MonteCarloReturnArt,
  "value-policy-learning/monte-carlo-value": MonteCarloValueArt,
  "value-policy-learning/oneply-q-prune": OneplyQPruneArt,
  "value-policy-learning/phase-distillation": PhaseDistillationArt,
  "value-policy-learning/sibling-advantage": SiblingAdvantageArt,
  "value-policy-learning/structured-nnue": StructuredNnueArt,
};

/** The art for one approach directory, or null when it has none of its own. */
export function getApproachArt(
  family: string,
  slug: string,
): ((props: ArtProps) => ReactElement) | null {
  return APPROACH_ART[`${family}/${slug}`] ?? null;
}
