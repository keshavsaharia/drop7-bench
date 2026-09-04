import "./approaches.css";
import Link from "next/link";
import { ApproachCard, sortApproaches } from "@/components/ApproachCard";
import { FilterSearch } from "@/components/FilterSearch";
import { PageHeader } from "@/components/PageHeader";
import { approachCountLabel, techniqueHref } from "@/components/TechniqueCard";
import { listTechniquePages } from "@/lib/learn";
import { familyMeta, listAllApproaches, listFamilies, type ApproachEntry } from "@/lib/repo";
import { getTechnique, listTechniques } from "@/lib/techniques";

export const dynamic = "force-dynamic";

export const metadata = {
  title: "Approaches",
  description: "Every theory of how to choose a column, grouped by the technique it uses.",
};

/*
 * Filters are links, not state. Each chip is an anchor to this page with one
 * query key changed, so the page works with JavaScript off and every view is
 * bookmarkable. The one client island is the search field.
 */

const STATUS_VALUES = ["completed", "rejected", "runtime-paused", "preregistered", "support-only", "proposal"] as const;

/** `oracle` matches both `oracle` and `teacher`, the two privileged information classes. */
const READS_VALUES = [
  { value: "public", label: "public" },
  { value: "oracle", label: "oracle/teacher" },
  { value: "diagnostic", label: "diagnostic" },
] as const;

const FILTER_KEYS = ["technique", "status", "reads", "view"] as const;
type FilterKey = (typeof FILTER_KEYS)[number];
type Filters = Record<FilterKey, string>;

type SearchParams = Record<string, string | string[] | undefined>;

function firstValue(value: string | string[] | undefined): string {
  return Array.isArray(value) ? (value[0] ?? "") : (value ?? "");
}

function readFilters(params: SearchParams): Filters {
  const technique = firstValue(params.technique);
  const status = firstValue(params.status);
  const reads = firstValue(params.reads);
  const view = firstValue(params.view);
  return {
    technique: getTechnique(technique) ? technique : "all",
    status: (STATUS_VALUES as readonly string[]).includes(status) ? status : "all",
    reads: READS_VALUES.some((option) => option.value === reads) ? reads : "all",
    view: view === "family" ? "family" : "technique",
  };
}

function chipHref(filters: Filters, key: FilterKey, value: string): string {
  const next = { ...filters, [key]: value };
  const params = new URLSearchParams();
  for (const k of FILTER_KEYS) {
    const v = next[k];
    if (v === "all" || (k === "view" && v === "technique")) continue;
    params.set(k, v);
  }
  const query = params.toString();
  return query ? `/approaches?${query}` : "/approaches";
}

function matchesFilters(entry: ApproachEntry, filters: Filters): boolean {
  if (filters.technique !== "all" && entry.technique !== filters.technique) return false;
  if (filters.status !== "all" && entry.status !== filters.status) return false;
  if (filters.reads === "oracle") return entry.reads === "oracle" || entry.reads === "teacher";
  if (filters.reads !== "all" && entry.reads !== filters.reads) return false;
  return true;
}

function Chip({ href, pressed, children }: { href: string; pressed: boolean; children: string }) {
  return (
    <Link href={href} className="chip" aria-pressed={pressed}>
      {children}
    </Link>
  );
}

interface Group {
  key: string;
  title: string;
  href: string;
  line: string;
  primerHref: string | null;
  /** The mono label above every card title in this group. */
  eyebrow: string;
  entries: ApproachEntry[];
}

function techniqueGroups(strategies: ApproachEntry[]): Group[] {
  const primers = new Map(listTechniquePages().map((page) => [page.technique, page.slug]));
  const groups: Group[] = listTechniques().map((technique) => {
    const primer = primers.get(technique.slug);
    return {
      key: technique.slug,
      title: technique.title,
      href: techniqueHref(technique.slug),
      line: technique.oneLine,
      primerHref: primer ? `/learn/techniques/${primer}` : null,
      eyebrow: technique.title,
      entries: sortApproaches(strategies.filter((entry) => entry.technique === technique.slug)),
    };
  });
  const other = strategies.filter((entry) => entry.technique === null || getTechnique(entry.technique) === null);
  if (other.length > 0) {
    groups.push({
      key: "other",
      title: "Other",
      href: "/approaches",
      line: "Strategy pages whose README names no technique from the catalogue.",
      primerHref: null,
      eyebrow: "Other",
      entries: sortApproaches(other),
    });
  }
  return groups.filter((group) => group.entries.length > 0);
}

function familyGroups(strategies: ApproachEntry[]): Group[] {
  return listFamilies()
    .map((family) => {
      const meta = familyMeta(family);
      return {
        key: family,
        title: meta.title,
        href: `/approaches/${family}`,
        line: meta.summary,
        primerHref: null,
        eyebrow: meta.title,
        entries: sortApproaches(strategies.filter((entry) => entry.family === family)),
      };
    })
    .filter((group) => group.entries.length > 0);
}

export default async function ApproachesPage({ searchParams }: { searchParams: Promise<SearchParams> }) {
  const filters = readFilters(await searchParams);
  const strategies = listAllApproaches().filter((entry) => entry.kind === "strategy");
  const shown = strategies.filter((entry) => matchesFilters(entry, filters));
  const groups = filters.view === "family" ? familyGroups(shown) : techniqueGroups(shown);
  const filtered = shown.length !== strategies.length;

  return (
    <div>
      <PageHeader
        title="Approaches"
        lead="Each page here is one theory of how to choose a column, grouped by the technique it uses; the engines that play the games and the instruments that measure them live under Engines and Diagnostics."
      />

      <nav className="approaches-filters" aria-label="Filters">
        <div className="approaches-filter-row">
          <span className="label">Technique</span>
          <Chip href={chipHref(filters, "technique", "all")} pressed={filters.technique === "all"}>
            all
          </Chip>
          {listTechniques().map((technique) => (
            <Chip
              key={technique.slug}
              href={chipHref(filters, "technique", technique.slug)}
              pressed={filters.technique === technique.slug}
            >
              {technique.slug}
            </Chip>
          ))}
        </div>
        <div className="approaches-filter-row">
          <span className="label">Status</span>
          <Chip href={chipHref(filters, "status", "all")} pressed={filters.status === "all"}>
            all
          </Chip>
          {STATUS_VALUES.map((status) => (
            <Chip key={status} href={chipHref(filters, "status", status)} pressed={filters.status === status}>
              {status}
            </Chip>
          ))}
        </div>
        <div className="approaches-filter-row">
          <span className="label">Reads</span>
          <Chip href={chipHref(filters, "reads", "all")} pressed={filters.reads === "all"}>
            all
          </Chip>
          {READS_VALUES.map((option) => (
            <Chip key={option.value} href={chipHref(filters, "reads", option.value)} pressed={filters.reads === option.value}>
              {option.label}
            </Chip>
          ))}
        </div>
        <div className="approaches-filter-row">
          <span className="label">Group by</span>
          <Chip href={chipHref(filters, "view", "technique")} pressed={filters.view === "technique"}>
            technique
          </Chip>
          <Chip href={chipHref(filters, "view", "family")} pressed={filters.view === "family"}>
            family
          </Chip>
        </div>
        <div className="approaches-filter-foot">
          <p className="approaches-caption">
            {approachCountLabel(shown.length)}
            {filtered && ` of ${strategies.length}`}
          </p>
          <FilterSearch />
        </div>
      </nav>

      {shown.length === 0 && <p className="approaches-empty">No approach page matches these filters.</p>}

      <div className="approaches-groups">
        {groups.map((group) => {
          const headingId = `group-${group.key}`;
          return (
            <section key={group.key} className="approach-group" aria-labelledby={headingId}>
              <div className="approach-group-head">
                <h2 className="approach-group-title" id={headingId}>
                  <Link href={group.href}>{group.title}</Link>
                </h2>
                {group.line && <p className="approach-group-line">{group.line}</p>}
                {group.primerHref && (
                  <Link href={group.primerHref} className="approach-group-more label">
                    Read the primer →
                  </Link>
                )}
              </div>
              <div className="approach-grid">
                {group.entries.map((entry) => (
                  <ApproachCard key={`${entry.family}/${entry.slug}`} entry={entry} eyebrow={group.eyebrow} />
                ))}
              </div>
            </section>
          );
        })}
      </div>

      <p className="approaches-empty" data-search-empty hidden>
        No approach page matches that search.
      </p>
    </div>
  );
}
