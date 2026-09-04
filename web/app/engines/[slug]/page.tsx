import "../engines.css";
import Link from "next/link";
import { notFound } from "next/navigation";
import matter from "gray-matter";
import type { ReactNode } from "react";
import { ArticleLayout } from "@/components/ArticleLayout";
import { AsideRecords } from "@/components/AsideRecords";
import { ApproachBadges } from "@/components/ApproachBadges";
import { Badge } from "@/components/Badge";
import { Button } from "@/components/Button";
import { latentModeText } from "@/components/EngineCard";
import { SourceRef, sourceHref } from "@/components/Engines";
import { Mdx } from "@/components/Mdx";
import { PageHeader } from "@/components/PageHeader";
import { TechnicalRecord } from "@/components/Reveal";
import { TechniqueArt } from "@/components/technique-art/TechniqueArt";
import {
  AUDIT_01,
  AUDIT_06,
  ENGINE_GATES,
  ENGINES,
  FINDING_02,
  FINDING_13,
  HISTORY,
  REPRODUCIBILITY,
  RS_RUST,
  getEngine,
  readmeApproach,
  type EngineEntry,
  type EngineSlug,
} from "@/lib/engines";
import { extractHeadings } from "@/lib/headings";
import { pageMetadata } from "@/lib/metadata";
import { recordsForApproach, type ApproachRecords } from "@/lib/records";
import { readRepoFile } from "@/lib/repo";
import type { TocItem } from "@/components/Toc";

export const dynamic = "force-dynamic";

export async function generateMetadata({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const entry = getEngine(slug);
  const path = `/engines/${slug}`;
  if (!entry) return pageMetadata({ title: "Engine", path });
  return pageMetadata({ title: entry.title, description: entry.role, path });
}

/* ---- the pages written here, for engines without a README of their own ---- */

const WRITTEN_TOC: readonly TocItem[] = [
  { id: "the-problem", text: "The problem" },
  { id: "how-it-works", text: "How it works" },
  { id: "what-happened", text: "What happened" },
  { id: "what-we-learned", text: "What we learned" },
];

function gateTests(file: string): readonly string[] {
  return ENGINE_GATES.find((group) => group.file === file)?.tests ?? [];
}

function TypescriptBody() {
  return (
    <>
      <h2 id="the-problem">The problem</h2>
      <p>
        Drop7&apos;s rules are short to state and fiddly to get right. A disc pops when the run it sits in, across or
        down, has exactly as many discs as its number; gray discs beside a pop take a hit, crack, and on a second hit
        reveal a number; gravity runs; the board is scanned again; and every fifth move a gray row rises from the
        bottom. A program that plays millions of games in native code needs one version of those rules that a person
        can read, argue about and test, so that when two engines disagree there is a place where the rule is settled.
        This engine is that place.
      </p>

      <h2 id="how-it-works">How it works</h2>
      <ol>
        <li>
          The board is a 49-cell array, row-major from the top: 0 for an empty cell, 1 to 7 for a numbered disc, 8 for
          a solid gray disc and 9 for a cracked one. Every step returns a new array instead of editing in place, which
          keeps each function small enough to test on its own.
        </li>
        <li>
          A move is <code>placeDisc</code> followed by a cascade: find the poppers, read the hits on neighbouring gray
          discs from the board before the clear, clear and reveal in row-major order, apply gravity, and scan again
          until nothing pops. A wave scores floor(7 × depth^2.5) per disc cleared. Every fifth move raises the covered
          row and pays the level bonus; the constants are <code>MOVES_PER_LEVEL = 5</code>,{" "}
          <code>LEVEL_BONUS = 17,000</code> and <code>CLEAR_BONUS = 70,000</code> (<SourceRef source="src/core/typescript/engine.ts" label="engine.ts" />).
        </li>
        <li>
          Chance is enumerated exactly. <code>forEachCascadeOutcome</code> streams every assignment of revealed
          values with probability 1/7 per revealed cell, and <code>forEachMoveOutcome</code> adds the seven possible
          next discs. The reference solver (<SourceRef source="src/core/typescript/solver.ts" label="solver.ts" />) uses this to compute an exact
          expectation: iterative deepening to a depth between 1 and 8, a fixed column order starting from the centre,
          and a 40,000-entry table keyed on the mirror-canonical board string.
        </li>
        <li>
          Latent mode (<code>LatentBoardOptions</code>) lets a caller fix every covered cell&apos;s hidden value in
          advance. The scripted rounds on <Link href="/leaderboard">the leaderboard</Link> and recorded-game replay
          use it, and the default random-reveal behaviour stays byte-identical, which{" "}
          <code>engine-latent.test.ts</code> checks.
        </li>
        <li>
          The same family of files holds a second ruleset, Classic (<SourceRef source="src/core/typescript/classic-engine.ts" label="classic-engine.ts" />: a decreasing
          30-drop clock, the Classic 7,000-point rise bonus and gray discs among the incoming ones), and{" "}
          <SourceRef source="src/core/typescript/recorded-game.ts" label="recorded-game.ts" />, which replays a tape
          with no random draws for server-side validation of mobile submissions.
        </li>
      </ol>
      <p>
        The solver&apos;s leaf is <code>evaluateHeuristic</code> with the combined profile of 14 features. The C++
        fair leaf has 18 weighted terms, so the playground&apos;s expectimax-d4 is described in its own registry as
        &ldquo;a playground analogue of the native research reference, not a source-identical port&rdquo; (<SourceRef source="src/bench/policies.ts" label="policies.ts" />).
      </p>

      <h2 id="what-happened">What happened</h2>
      <p>
        The parity check against the C++ engine passes exactly: 256 seeded games and 6,852 moves produced identical
        records in both engines (<SourceRef source={REPRODUCIBILITY} label="reproducibility guide" />, current
        verification snapshot). The Rust engine&apos;s TypeScript-driver arm later replayed 256 games against it with
        zero mismatches (<SourceRef source={RS_RUST} />).
      </p>
      <TechnicalRecord summary="Coverage of the parity sweep and the test counts">
        <ul>
          <li>
            Coverage of the 256-seed sweep (<SourceRef source={AUDIT_01} />, M1): mean 26.8 moves per game (min 20,
            max 45), deepest chain 9, 1,115 level advances, 2,389 gray reveals, 0 board clears, 0 games at the move
            cap; termination 254 by blocked rise and 2 with no legal column.
          </li>
          <li>
            TypeScript tests: 122 at the time of audit-01; 137 to 145 in later records (<SourceRef source={RS_RUST} />, CT-20260901T070609Z-3028349f).
          </li>
          <li>
            <code>engine.ts</code> is 874 lines in this checkout; audit-01&apos;s 769-line figure and its hash are
            historical.
          </li>
        </ul>
      </TechnicalRecord>

      <h2 id="what-we-learned">What we learned</h2>
      <p>
        Two engines can agree exactly on the wrong rules. The fidelity audit recorded three divergences from the cited
        reference implementation (the level bonus is forfeited on the terminating rise, a fifth-drop board clear is
        overpaid by 70,000, and the opening position differs) and one protocol hazard: twelve public next-disc
        observations recover the 32-bit seed in 9.8 s (<SourceRef source={AUDIT_01} />, H1 to H4). Because every
        other engine is proven identical to this one, all of them share those properties. The parity sweep also leaves
        the 70,000-point board-clear branches with zero cross-engine coverage. Correcting a divergence would be a new
        ruleset, and every engine would have to be re-proven against it.
      </p>

      <h3 id="in-the-browser">In the browser</h3>
      <p>
        The browser game at <Link href="/play">/play</Link> runs a solver in a Web Worker. It is{" "}
        <code>fastEvaluateMoves</code>: the reference <code>evaluateMoves</code> with a faster move generator, an
        allocation-free leaf and a packed-key transposition table, porting the board-state optimisations of finding-13
        to TypeScript. It must stay value-identical to the reference, with the same outcomes in the same order,
        bit-identical column values and the same work and cache counts, which the console&apos;s own tests check
        against real positions. The browser solver is a demonstration of one policy; it is never tier evidence, and it
        is a different program from the C++ fair-D4 reference whose cohort numbers the research pages quote. It has{" "}
        <Link href="/engines/browser">a page of its own</Link>.
      </p>
    </>
  );
}

function BrowserBody() {
  return (
    <>
      <h2 id="the-problem">The problem</h2>
      <p>
        A visitor who plays on this site wants the solver&apos;s recommendation within a second, in the browser, with
        no server behind it. The reference solver computes the right answer, but a source comment in the browser cache
        records that when it was profiled in V8 the string keys and Map churn were about 85% of a depth-3 decision (a
        comment in <code>fast-cache.ts</code>, and no record). The port exists to remove that cost without changing a
        single answer.
      </p>

      <h2 id="how-it-works">How it works</h2>
      <ol>
        <li>Wave scores and readiness come from tables instead of a power call per wave.</li>
        <li>
          Poppers come from one pass over the board that builds seven row and seven column occupancy masks, then a
          128-entry run-length table, in place of two rescans per numbered cell per wave.
        </li>
        <li>
          Gravity runs in place and only on the columns that lost a disc, and the cover-hit scan uses a flag array in
          place of a Set.
        </li>
        <li>The search tracks the wave count and never spreads a game state per chance branch.</li>
        <li>
          The transposition table keys on the position packed into seven 32-bit words in canonical, mirror-aware
          orientation, and lives in typed arrays with an intrusive LRU list (<code>fast-cache.ts</code>).
        </li>
        <li>
          The leaf is allocation-free (<code>fast-leaf.ts</code>).
        </li>
      </ol>
      <p>
        The whole thing runs in a Web Worker (<code>solver.worker.ts</code>), so the page stays responsive while it
        thinks.
      </p>

      <h2 id="what-happened">What happened</h2>
      <p>
        It is test-gated, with no research record and no counts retained. <code>cd web &amp;&amp; npm test</code>{" "}
        runs the twelve tests in <code>fast-search.test.ts</code> against real positions; these are the parity tests
        the port must pass:
      </p>
      <ul>
        {gateTests("web/lib/play/fast-search.test.ts").map((test) => (
          <li key={test}>{test}</li>
        ))}
      </ul>
      <p>
        The timing test prints and retains nothing, and no Node, V8 or browser version is recorded anywhere, so this
        site prints no speed figure for the browser solver.
      </p>

      <h2 id="what-we-learned">What we learned</h2>
      <p>
        The browser solver is a demonstration of one policy; it is never tier evidence, and it is a different program
        from the C++ fair-D4 reference whose cohort numbers the research pages quote. Its leaf is the TypeScript
        combined profile with 14 features, where the C++ fair leaf has 18 weighted terms, so a depth-4 search here
        picks the column the <Link href="/engines/typescript">TypeScript reference</Link> would pick. The open
        question is whether its counts should ever become a record: a search that runs in a visitor&apos;s browser has
        no machine profile, and <Link href="/docs/benchmarks">the benchmark rules</Link> require one.
      </p>
    </>
  );
}

function NativeBody() {
  return (
    <>
      <h2 id="the-problem">The problem</h2>
      <p>
        Nearly every search and training experiment here plays hundreds of thousands of games, and that is only
        affordable in native code. The C++ engine exists to play those games at that scale while meaning exactly the
        same thing as the readable <Link href="/engines/typescript">TypeScript rulebook</Link>, so that a score from a
        C++ cohort and a score from a TypeScript one can be read on the same scale.
      </p>

      <h2 id="how-it-works">How it works</h2>
      <ol>
        <li>
          <SourceRef source="src/core/native/engine.hpp" label="engine.hpp" /> mirrors <code>engine.ts</code>{" "}
          statement for statement. Its cascade carries the comment that <code>engine.ts</code> scans the board in
          row-major order and consumes reveal values before gravity, and it does the same. The board is a{" "}
          <code>std::array</code> of 49 bytes, row-major from the top, and the cascade keeps a <code>std::vector</code>{" "}
          of waves.
        </li>
        <li>
          Randomness is shared with the TypeScript engine: the same Mulberry32 generator as <code>seededRandom</code>,
          the same 32-bit mixing function, and the same <code>headlessDisc</code> keyed by seed and move number, with
          one fixed domain constant for next discs and another for reveals; <code>playHeadlessMove</code> reseeds the
          reveals every move. This is what lets two engines play the identical game from one seed.
        </li>
        <li>
          <SourceRef source="src/core/native/public-behavior.hpp" label="public-behavior.hpp" /> is the policy layer
          above the rules: stratified chance sampling for search, a scenario seed derived from each state, and a
          sampled copy of the move loop (<code>playMoveSampled</code>) that every native rollout runs on.
        </li>
        <li>
          The <Link href="/approaches/fair-expectimax/reference">fair depth-3 and depth-4 reference policy</Link>{" "}
          (built as <code>build/fair-depth4</code>) and{" "}
          <Link href="/approaches/ntuple-rl/native-suite">the native suite</Link> are built on it, as are PPO with its
          torch environment and nearly all of the 110 C++ entry points under the approaches directory (<SourceRef source={REPRODUCIBILITY} label="reproducibility guide" />). The engine&apos;s source hash is pinned in <code>research/benchmarks/baselines-v1.json</code>, and it
          still matches.
        </li>
        <li>
          <Link href="/approaches/lifetime-objective/common">The shared game harness</Link> drives whole games and
          cohorts on top of it.
        </li>
      </ol>

      <h2 id="what-happened">What happened</h2>
      <p>
        Every parity claim on this site anchors here. The TypeScript engine matches it exactly on 256 seeded games and
        6,852 moves (<SourceRef source={REPRODUCIBILITY} label="reproducibility guide" />), and the scenario, fast
        and Rust engines each replay it with zero mismatches (<SourceRef source={FINDING_02} label="finding-02" />,{" "}
        <SourceRef source={FINDING_13} label="finding-13" />, <SourceRef source={RS_RUST} />; the move counts are in
        the record below). Its single-core rate is recorded once, in the Rust comparison: 6,799,180 moves per second
        over whole games on one thread of the shared workstation (<SourceRef source={RS_RUST} />).
      </p>
      <TechnicalRecord summary="Parity scales, the throughput arm and the historical line">
        <ul>
          <li>
            Scenario engine against this engine: 8,192 game-plays, 218,470 moves, 0 mismatches (<SourceRef source={FINDING_02} />, section 1).
          </li>
          <li>
            Fast engine against this engine: 8,288 games, 438,020 moves, 548,263 waves, 0 mismatches (<SourceRef source={FINDING_13} />, section 4D).
          </li>
          <li>
            Rust engine against this engine: 512 center-policy and 256 search-policy games in three trajectory arms,
            36,427 moves and 40,286 waves, 0 mismatches (<SourceRef source={RS_RUST} />).
          </li>
          <li>
            Throughput arm of <SourceRef source={RS_RUST} />: 32,768 whole center-policy games on one thread; machine
            MACH-20260824T072426Z-fa409222 (AMD Ryzen AI MAX+ 395, 16 physical and 32 logical cores, governor
            powersave), load average 1.1 to 1.7.
          </li>
          <li>
            Historical line: &ldquo;the native engine ran 2,673,362 moves in 0.927 seconds (2.88 million
            moves/second)&rdquo; on unstated hardware (<SourceRef source={HISTORY} />). A historical measurement; the
            two rates cannot be compared.
          </li>
        </ul>
      </TechnicalRecord>

      <h2 id="what-we-learned">What we learned</h2>
      <p>
        The fidelity audit identifies <code>playMoveSampled</code> as a second, untested copy of the move loop that
        the parity run never touches; the search-parity gates of the fast and Rust engines exercise it indirectly,
        but no gate compares it to <code>engine.hpp</code>&apos;s <code>playMove</code> directly (<SourceRef source={AUDIT_01} />, M3). Floating-point contraction is the other hazard the engine work turned
        up: compiled with <code>-march=native</code> alone, 2,890 of 17,045 leaf values differed from the pinned build,
        and adding <code>-ffp-contract=off</code> brought the count to 0 (<SourceRef source={AUDIT_06} />, D). The
        builds are clang++ only (<SourceRef source={FINDING_13} />). The open question is whether the sampled move
        loop gets a direct gate of its own.
      </p>
    </>
  );
}

function RustClassicBody() {
  return (
    <>
      <h2 id="the-problem">The problem</h2>
      <p>
        The mobile game collection plays Classic mode: a decreasing 30-drop clock, the Classic 7,000-point rise
        bonus, and gray discs among the incoming ones. Its submissions are validated on the server by the TypeScript
        engine&apos;s tape replay. A native engine for the same ruleset was written for future research use
        (CT-20260901T070609Z-3028349f).
      </p>

      <h2 id="how-it-works">How it works</h2>
      <ol>
        <li>
          It is its own crate, <code>drop7-classic-engine</code>, independent of the{" "}
          <Link href="/engines/rust">bitboard engine</Link>: a different ruleset and no bit packing.
        </li>
        <li>The board is a 49-byte array with a 49-byte latent array beside it, moved by plain loops.</li>
        <li>
          Every random future is an explicit input, so a game is a pure function of its tape and can be replayed to
          completion without a random source.
        </li>
      </ol>

      <h2 id="what-happened">What happened</h2>
      <p>The crate carries four cargo tests and one conformance transition shared with the TypeScript Classic test:</p>
      <ul>
        {gateTests("src/core/rust/classic-engine (cargo test)").map((test) => (
          <li key={test}>
            <code>{test}</code>
          </li>
        ))}
      </ul>
      <p>
        Nothing in the tree consumes it yet: the only references are its own <code>Cargo.toml</code>, the TypeScript
        test and the contribution record.
      </p>

      <h2 id="what-we-learned">What we learned</h2>
      <p>
        The contribution record states that the crate is not yet integrated into the native parity suite or a direct
        whole-trajectory parity harness. Until it is, its parity evidence is one transition and four unit tests, and
        no record compares it with the TypeScript Classic engine in any quantity. The open question is who its first
        consumer will be; the infrastructure notes mention a multi-hour Rust policy game in the submission context, but
        nothing calls the crate today.
      </p>
    </>
  );
}

const WRITTEN: Partial<Record<EngineSlug, () => ReactNode>> = {
  typescript: TypescriptBody,
  browser: BrowserBody,
  native: NativeBody,
  "rust-classic": RustClassicBody,
};

/* ---- aside ---- */

function approachHref(path: string): string | null {
  const match = /^approaches\/([a-z0-9-]+)\/([a-z0-9-]+)\//.exec(path);
  return match ? `/approaches/${match[1]}/${match[2]}` : null;
}

function SourceBlock({ entry }: { entry: EngineEntry }) {
  const href = sourceHref(entry.path) ?? approachHref(entry.path);
  return (
    <div className="aside-block">
      <span className="label">Source</span>
      <ul className="engine-aside-list">
        <li>{href ? <Link href={href}><code>{entry.path}</code></Link> : <code>{entry.path}</code>}</li>
        {entry.readme && (
          <li>
            <Link href={approachHref(entry.readme) ?? "/approaches"}>Approach page</Link>
          </li>
        )}
      </ul>
    </div>
  );
}


function UsedByBlock({ entry }: { entry: EngineEntry }) {
  return (
    <div className="aside-block">
      <span className="label">Used by</span>
      {entry.usedBy.length === 0 ? (
        <p className="engine-aside-empty">Nothing consumes it yet.</p>
      ) : (
        <ul className="engine-aside-list">
          {entry.usedBy.map((link) => (
            <li key={link.label}>{link.href ? <Link href={link.href}>{link.label}</Link> : link.label}</li>
          ))}
        </ul>
      )}
    </div>
  );
}

/* ---- page ---- */

export default async function EnginePage({ params }: { params: Promise<{ slug: string }> }) {
  const { slug } = await params;
  const entry = getEngine(slug);
  if (!entry) notFound();

  const index = ENGINES.findIndex((candidate) => candidate.slug === entry.slug);
  const previous = index > 0 ? ENGINES[index - 1] : null;
  const next = index < ENGINES.length - 1 ? ENGINES[index + 1] : null;

  let body: ReactNode;
  let toc: readonly TocItem[] = WRITTEN_TOC;
  let lead: string = entry.role;
  let frontmatter: Record<string, unknown> = {};
  let records: ApproachRecords | null = null;

  if (entry.readme) {
    const approach = readmeApproach(entry.readme);
    if (approach) records = recordsForApproach(approach.family, approach.slug);
    const raw = readRepoFile(entry.readme);
    if (raw) {
      const parsed = matter(raw);
      frontmatter = parsed.data as Record<string, unknown>;
      if (typeof frontmatter.summary === "string") lead = frontmatter.summary;
      toc = extractHeadings(parsed.content, { minDepth: 2, maxDepth: 2 });
      body = <Mdx source={parsed.content} fromPath={entry.readme} />;
    } else {
      toc = [];
      body = (
        <div className="prose-drop7">
          <p>
            This engine is documented in <code>{entry.readme}</code>, which is absent from this checkout.
          </p>
        </div>
      );
    }
  } else {
    const Body = WRITTEN[entry.slug];
    body = <div className="prose-drop7">{Body ? <Body /> : <p>This engine has no page yet.</p>}</div>;
  }

  const status = typeof frontmatter.status === "string" ? frontmatter.status : null;
  const evidence = typeof frontmatter.evidence === "string" ? frontmatter.evidence : null;
  const reads = typeof frontmatter.reads === "string" ? frontmatter.reads : null;

  const aside = (
    <>
      <SourceBlock entry={entry} />
      {records && <AsideRecords records={records} />}
      <UsedByBlock entry={entry} />
    </>
  );

  return (
    <div className="engine-page">
      <PageHeader crumbs={[{ href: "/engines", label: "engines" }]} title={entry.title} lead={lead}>
        <Badge label={entry.language} />
        <Badge label={`latent mode: ${latentModeText(entry.latentMode)}`} />
        <ApproachBadges status={status} evidence={evidence} reads={reads} />
      </PageHeader>
      {entry.hero && (
        <div className="engine-hero fig-frame" aria-hidden="true">
          <TechniqueArt name={entry.art} mode="loop" title={entry.title} />
        </div>
      )}
      <ArticleLayout toc={toc} aside={aside}>
        {body}
        <nav className="engine-nav" aria-label="Other engines">
          {previous ? (
            <Button variant="ghost" href={`/engines/${previous.slug}`}>
              ← {previous.title}
            </Button>
          ) : (
            <span />
          )}
          {next && (
            <Button variant="ghost" href={`/engines/${next.slug}`}>
              {next.title} →
            </Button>
          )}
        </nav>
      </ArticleLayout>
    </div>
  );
}
