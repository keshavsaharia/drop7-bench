/**
 * The two accordions at the foot of an approach page.
 *
 * "Records" (technical) lists the theories, experiments and results whose
 * text references this directory, rendered by the sentence-form summaries in
 * Research.tsx, which read only the records' own fields.
 *
 * "Agent context" holds what a visitor does not need but an agent does: the
 * source-file list, the pointer to hand-written operational notes, and, for a
 * directory with no README, the note on where a page would come from.
 *
 * Server component; every list may be empty on a checkout without research/.
 */
import Link from "next/link";
import { ApproachSourceList } from "./ApproachSourceList";
import { ExperimentSummary, ResultSummary, TheorySummary } from "./Research";
import { Reveal } from "./Reveal";
import type { ApproachRecords as LinkedRecords } from "@/lib/records";
import { approachOperationalNotes, type ApproachEntry } from "@/lib/repo";

export interface ApproachRecordsProps {
  entry: ApproachEntry;
  /** From `recordsForApproach(entry.family, entry.slug)`. */
  records: LinkedRecords;
  /** True when the directory has no README to render; opens the agent accordion. */
  noDocs?: boolean;
}

export function ApproachRecords({ entry, records, noDocs = false }: ApproachRecordsProps) {
  const { family, slug } = entry;
  const dir = `approaches/${family}/${slug}`;
  const operationalNotes = approachOperationalNotes(family, slug);
  const linked = records.theories.length + records.experiments.length + records.results.length;

  return (
    <section className="approach-records" aria-label="Records and agent context">
      <Reveal
        variant="technical"
        label="Records"
        summary="Theories, experiments and results that reference this directory"
        id="records"
      >
        {linked === 0 ? (
          <p>No theory, experiment or result record references this directory.</p>
        ) : (
          <>
            {records.theories.map((theory) => (
              <TheorySummary key={theory.$id} id={theory.theoryId} />
            ))}
            {records.experiments.map((experiment) => (
              <ExperimentSummary key={experiment.$id} id={experiment.experimentId} />
            ))}
            {records.results.map((result) => (
              <ResultSummary key={result.$id} id={result.resultId} />
            ))}
          </>
        )}
      </Reveal>

      <Reveal
        variant="agent"
        summary="Source files, operational notes and how to reproduce"
        id="source-files"
        open={noDocs || undefined}
      >
        {noDocs && (
          <p>
            This directory has no page yet. Add a <code>README.mdx</code> to <code>{dir}/</code> and it
            will render here, with the same header and these two sections.
          </p>
        )}
        <p>
          Directory: <code>{dir}</code>
        </p>
        <ApproachSourceList family={family} slug={slug} files={entry.sourceFiles} />
        {operationalNotes && (
          <p>
            Operational notes (build commands, gate commands, seed leases) are kept in{" "}
            <Link href={`/approaches/${family}/${slug}/README.md`}>
              <code>{operationalNotes}</code>
            </Link>
            , alongside the page above.
          </p>
        )}
      </Reveal>
    </section>
  );
}
