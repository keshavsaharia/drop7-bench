import "../engines/engines.css";
import { Badge } from "@/components/Badge";
import { Card } from "@/components/Card";
import { PageHeader } from "@/components/PageHeader";
import { listApproachesByKind, type ApproachEntry } from "@/lib/repo";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Diagnostics",
  description:
    "The measuring instruments, harnesses and model probes of the Drop7 research program. None of these is a way to play.",
};

type GroupKey = "instruments" | "harnesses" | "probes" | "other";

interface Group {
  key: GroupKey;
  title: string;
  text: string;
}

const GROUPS: readonly Group[] = [
  {
    key: "instruments",
    title: "Measuring instruments",
    text: "Tools that take a reading from games already played or from positions: where the points come from, which discs can never clear, how fast discs must clear to keep up, and what a perfect-information solver would have done.",
  },
  {
    key: "harnesses",
    title: "Harnesses and benchmarks",
    text: "The fixed scaffolding that runs policies side by side under one set of conditions, and the checks on the benchmark itself.",
  },
  {
    key: "probes",
    title: "Model probes",
    text: "Small experiments that ask what a network of a given size can hold, before a training run is paid for.",
  },
  {
    key: "other",
    title: "Other",
    text: "Diagnostics that have not been placed in a group yet.",
  },
];

/** Approach slug to group, from the information-architecture audit. */
const GROUP_OF: Record<string, GroupKey> = {
  "score-decomposition": "instruments",
  "entombed-discs": "instruments",
  "flow-ceiling": "instruments",
  "trajectory-throughput": "instruments",
  "d4-flow": "instruments",
  "tie-breaking": "instruments",
  "hpool-d0": "instruments",
  "perfect-information-oracle": "instruments",
  "heuristic-benchmark": "harnesses",
  "phase-benchmark": "harnesses",
  "policy-comparison": "harnesses",
  "deployment-panel": "harnesses",
  "suite-validation": "harnesses",
  "leaf-capacity-sweep": "probes",
  "nnue-d4q-probe": "probes",
  "throughput-probe": "probes",
};

function DiagnosticCard({ entry }: { entry: ApproachEntry }) {
  return (
    <Card
      href={`/approaches/${entry.family}/${entry.slug}`}
      eyebrow={entry.family}
      title={entry.title}
      summary={entry.summary || undefined}
    >
      {(entry.status || entry.reads) && (
        <div className="card-labels">
          {entry.status && <Badge kind="status" value={entry.status} />}
          {entry.reads && <Badge kind="reads" value={entry.reads} />}
        </div>
      )}
    </Card>
  );
}

export default function DiagnosticsPage() {
  const diagnostics = listApproachesByKind("diagnostic");
  const grouped = new Map<GroupKey, ApproachEntry[]>();
  for (const entry of diagnostics) {
    const key = GROUP_OF[entry.slug] ?? "other";
    grouped.set(key, [...(grouped.get(key) ?? []), entry]);
  }

  return (
    <div className="diagnostics-index">
      <PageHeader
        crumbs={[{ href: "/research", label: "research" }]}
        title="Diagnostics"
        lead="Measuring instruments, harnesses and probes; none of these is a way to play."
      />
      {diagnostics.length === 0 && (
        <p className="engine-note">
          No diagnostic approach directories are present in this checkout. They are the directories whose README
          carries <code>kind: diagnostic</code>.
        </p>
      )}
      {GROUPS.map((group) => {
        const entries = grouped.get(group.key) ?? [];
        if (entries.length === 0) return null;
        return (
          <section key={group.key} className="diag-group" aria-labelledby={`diag-${group.key}`}>
            <h2 id={`diag-${group.key}`}>{group.title}</h2>
            <p>{group.text}</p>
            <ul className="diag-grid">
              {entries.map((entry) => (
                <li key={`${entry.family}/${entry.slug}`}>
                  <DiagnosticCard entry={entry} />
                </li>
              ))}
            </ul>
          </section>
        );
      })}
    </div>
  );
}
