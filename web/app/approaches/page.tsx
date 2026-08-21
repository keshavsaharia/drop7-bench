import Link from "next/link";
import { familyDocPath, familyMeta, listApproaches, listFamilies } from "@/lib/repo";

export const dynamic = "force-dynamic";

const FAMILY_BLURBS: Record<string, string> = {
  "afterstate-learning": "Value functions over the state after a move resolves, not the action that produced it.",
  "baselines-diagnostics": "Reference policies, parity checks, and measurement harnesses.",
  "constructive-reservoir": "Deliberately build chain structures that survive across row rises.",
  "d4-long-outcome": "Teachers and students built around the fair-D4 reference.",
  "fair-expectimax": "The reference line: full-width search that averages chance fairly.",
  "heuristic-search": "Hand-written features, shallow lookahead, and bounded planners.",
  "lifetime-objective": "Optimize survival time and flow, not just immediate score.",
  "ntuple-rl": "N-tuple value learning and policy-gradient reinforcement learners.",
  "oracle-curriculum": "Privileged future-aware teachers that produce labels for public students.",
  "terminal-policy-iteration": "Improve policies against long-horizon terminal outcomes.",
  "tree-search": "MCTS, PUCT, and observable-state tree search.",
  "value-policy-learning": "Learned value models and policy networks over public states.",
};

export default function ApproachesPage() {
  const families = listFamilies();
  return (
    <div className="space-y-6">
      <div>
        <h1 className="text-2xl font-black text-zinc-50">Approaches</h1>
        <p className="mt-1 max-w-3xl text-sm text-zinc-400">
          Every strategy family and approach directory in the repository, with
          its documentation. Each approach page renders the MDX notes stored in
          that directory — a learning resource for how the strategy works and
          what the evidence showed.
        </p>
      </div>
      <div className="grid gap-4 sm:grid-cols-2 lg:grid-cols-3">
        {families.map((family) => {
          const approaches = listApproaches(family);
          const meta = familyMeta(family);
          const documented = approaches.filter((a) => a.hasDocs && !a.draft).length;
          const drafts = approaches.filter((a) => a.draft).length;
          return (
            <Link
              key={family}
              href={`/approaches/${family}`}
              className="group rounded-xl border border-zinc-800 bg-zinc-900/50 p-4 hover:border-sky-800"
            >
              <div className="flex items-baseline justify-between gap-2">
                <h2 className="font-bold text-zinc-100 group-hover:text-sky-300">
                  {meta.title}
                </h2>
                <span className="text-xs text-zinc-500">
                  {approaches.length} approaches
                </span>
              </div>
              <p className="mt-1 text-sm text-zinc-400">
                {meta.summary || FAMILY_BLURBS[family] || ""}
              </p>
              <p className="mt-2 text-xs text-zinc-600">
                {documented} written · {drafts} draft · {approaches.length} total
                {familyDocPath(family) ? " · family guide" : ""}
              </p>
            </Link>
          );
        })}
      </div>
    </div>
  );
}
