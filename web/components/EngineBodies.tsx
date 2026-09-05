import Link from "next/link";
import type { ReactNode } from "react";
import { CodeSnippet } from "./CodeSnippet";
import { SourceRef } from "./Engines";
import { AgentContext, TechnicalRecord } from "./Reveal";
import {
  AUDIT_01,
  AUDIT_06,
  ENGINE_GATES,
  FINDING_02,
  FINDING_13,
  REPRODUCIBILITY,
  RS_RUST,
  type EngineSlug,
} from "@/lib/engines";
import type { TocItem } from "./Toc";

export const ENGINE_BODY_TOC: readonly TocItem[] = [
  { id: "design", text: "Design" },
  { id: "implementation", text: "Implementation" },
  { id: "uses", text: "Uses" },
  { id: "verification", text: "Verification" },
  { id: "limits", text: "Limits" },
];

function gateTests(file: string): readonly string[] {
  return ENGINE_GATES.find((group) => group.file === file)?.tests ?? [];
}

function TypeScriptBody() {
  return (
    <>
      <h2 id="design">Design</h2>
      <p>
        This is the readable version of the five-move Hardcore rules. The board is a flat, 49-cell array and every
        public operation says what it does with ordinary loops. A move returns a new state, so a test can inspect the
        board before and after any step without reconstructing an in-place mutation. The same source is imported by
        the browser game, the board figures, the scripted-round tools and the cross-language parity harness.
      </p>

      <h2 id="implementation">Implementation</h2>
      <h3>A literal pop test</h3>
      <p>
        A numbered disc clears when its number equals the occupied run through it across or down. The implementation
        scans the board in reading order and calls the same small line-count function for both axes. It favors an
        auditable rule over a compressed representation.
      </p>
      <CodeSnippet
        path="src/core/typescript/engine.ts"
        startLine={265}
        endLine={307}
        title="Count a run, then find every matching disc"
        caption="The implementation reads like the rule: count contiguous occupied cells in each direction, then compare that length with the disc."
      />

      <h3>One simultaneous wave</h3>
      <p>
        Every disc selected for a wave disappears together. Gray discs count hits against the board from before the
        clear, reveals are consumed in row-major order, and gravity runs only after those values have been written.
        That order is observable because a reveal can start the next wave.
      </p>
      <CodeSnippet
        path="src/core/typescript/engine.ts"
        startLine={322}
        endLine={356}
        title="Apply gray-disc hits before gravity"
        caption="The original board supplies the gray-disc state and the popping set, while a copied board receives cracks and queued reveals."
      />

      <h3>Chance as a stream</h3>
      <p>
        Search has to consider every value a gray disc can reveal. The engine visits those branches one at a time and
        hands each settled board to a callback. A large cascade can therefore be explored without first building a
        large array of every possible future.
      </p>
      <CodeSnippet
        path="src/core/typescript/engine.ts"
        startLine={523}
        endLine={546}
        title="Enumerate reveal values without retaining the tree"
        caption="Each hidden value extends the current branch with one seventh of its probability, then the settled result is visited immediately."
      />

      <h2 id="uses">Uses</h2>
      <p>
        The ordinary move function powers <Link href="/play">the playable game</Link>. Its optional latent board
        gives every covered disc a fixed hidden value for scripted rounds and recorded-game replay. The solver uses
        the chance-streaming functions to evaluate columns by expectation. On this site, that solver runs in a Web
        Worker, which keeps the main thread available for drawing, input and cascade animation.
      </p>
      <CodeSnippet
        path="web/lib/play/solver.worker.ts"
        startLine={12}
        endLine={31}
        title="Run expectimax away from the main thread"
        caption="The worker receives one position, posts each completed search depth as progress, and posts the final result when the time budget ends."
      />
      <p>
        The browser uses a value-identical, allocation-conscious port of the same search. Its move generator, cache
        and worker lifecycle are explained on the <Link href="/engine/browser">browser fast-search page</Link>.
      </p>

      <h2 id="verification">Verification</h2>
      <p>
        The retained parity snapshot replayed 256 seeded games and 6,852 moves against the C++ engine with identical
        move records (<SourceRef source={REPRODUCIBILITY} label="reproducibility guide" />). A later Rust gate drove
        another 256 games from this implementation and found no trajectory mismatch (<SourceRef source={RS_RUST} />).
      </p>
      <TechnicalRecord summary="The rules and latent-board checks that guard this implementation">
        <p>The rule tests cover these observable behaviors:</p>
        <ul>
          {gateTests("src/core/typescript/engine.test.ts").map((test) => (
            <li key={test}>{test}</li>
          ))}
        </ul>
        <p>The latent-board tests cover these additional behaviors:</p>
        <ul>
          {gateTests("src/core/typescript/engine-latent.test.ts").map((test) => (
            <li key={test}>{test}</li>
          ))}
        </ul>
      </TechnicalRecord>

      <h2 id="limits">Limits</h2>
      <p>
        Array copies and repeated line scans make the rules easy to inspect, while deeper search needs a faster path.
        The C++ fast engine and the browser solver preserve the same ordering with cheaper storage. The fidelity audit
        also records rule divergences from the cited original game and an uncovered board-clear branch in the parity
        sweep (<SourceRef source={AUDIT_01} />). Agreement between ports preserves those semantics; it does not settle
        the historical rules question.
      </p>
      <AgentContext summary="Extension points, required tests and the browser-worker contract">
        <p>
          Keep rule changes in <code>src/core/typescript/engine.ts</code> readable and update the focused tests before
          porting them elsewhere. Run <code>npm test</code> at the project root for the engine and native parity suite,
          then run <code>cd web &amp;&amp; npm test</code> for browser-search parity.
        </p>
        <p>
          The worker protocol lives in <code>web/lib/play/solver.protocol.ts</code>. A position change must terminate
          its existing worker, completed-depth progress may update the display, and only a final result may trigger an
          automatic move. Do not turn browser-solver output into research evidence.
        </p>
      </AgentContext>
    </>
  );
}

function BrowserBody() {
  return (
    <>
      <h2 id="design">Design</h2>
      <p>
        The playable board needs a useful recommendation while it is still responding to taps and drawing chain
        animations. The browser solver keeps the TypeScript reference search&apos;s result and replaces its expensive
        storage paths. It runs in a dedicated Web Worker, so search never occupies the page&apos;s main thread.
      </p>

      <h2 id="implementation">Implementation</h2>
      <h3>A worker per position</h3>
      <p>
        The game creates one module worker for the current position. Progress messages can show the best completed
        depth while a deeper pass runs. When the position, mode or budget changes, React&apos;s cleanup terminates the old
        worker, which prevents a stale answer from playing a move on a newer board.
      </p>
      <CodeSnippet
        path="web/components/Drop7Game.tsx"
        startLine={267}
        endLine={334}
        title="Own the solver for exactly one position"
        caption="The effect starts a worker, handles progress and final messages, and terminates the worker during cleanup or after a result."
      />

      <h3>Fast rules with the same order</h3>
      <p>
        One pass builds row and column occupancy masks. Run lengths then come from a small lookup table, while the
        returned poppers stay in the reference engine&apos;s row-major order. Gravity edits only columns that can contain
        a hole after the wave.
      </p>
      <CodeSnippet
        path="web/lib/play/fast-search.ts"
        startLine={139}
        endLine={175}
        title="Find matching discs from occupancy masks"
        caption="The fast scan changes how run lengths are obtained while preserving the list and order observed by the chance search."
      />
      <CodeSnippet
        path="web/lib/play/fast-search.ts"
        startLine={225}
        endLine={241}
        title="Settle only the affected columns"
        caption="The column mask avoids seven-column gravity work after a wave that touched only part of the board."
      />

      <h3>Packed cache keys</h3>
      <p>
        A position and its mirror share one cache entry. The browser packs the canonical board and its scalar fields
        into typed arrays, then keeps least-recently-used order with integer links. This removes short-lived strings
        and map nodes from the inner search loop.
      </p>
      <CodeSnippet
        path="web/lib/play/fast-search.ts"
        startLine={433}
        endLine={485}
        title="Pack a mirror-aware position"
        caption="The key preserves the reference cache&apos;s equivalence classes while fitting the position into seven 32-bit words."
      />

      <h2 id="uses">Uses</h2>
      <p>
        Evaluate mode recommends a column and leaves the decision to the visitor. Auto mode plays the best completed
        result after a short pause. Both modes use iterative deepening, so a short time budget still returns the last
        fully evaluated depth. No server receives the board or performs the search.
      </p>

      <h2 id="verification">Verification</h2>
      <p>
        This port is test-gated against the reference TypeScript solver on real positions. The suite compares chance
        outcomes, completed column values, cache eviction, work limits and every completed-depth callback. No browser
        or runtime profile was retained, so this page makes no speed claim.
      </p>
      <TechnicalRecord summary="The parity cases for the browser move generator, leaf and search">
        <ul>
          {gateTests("web/lib/play/fast-search.test.ts").map((test) => (
            <li key={test}>{test}</li>
          ))}
        </ul>
      </TechnicalRecord>

      <h2 id="limits">Limits</h2>
      <p>
        The browser leaf is the TypeScript heuristic profile, while the C++ fair reference uses a different weighted
        leaf. This solver demonstrates one policy and its results are outside the research tiers. Its timing test is
        informational and retains no environment or result.
      </p>
      <AgentContext summary="Worker invariants and the parity command for future changes">
        <p>
          Keep <code>fastEvaluateMoves</code> value-identical to <code>evaluateMoves</code>, including outcome order,
          floating-point accumulation, work accounting and partial results. Run <code>cd web &amp;&amp; npm test</code>
          after changing anything under <code>web/lib/play/</code> or the worker lifecycle in{" "}
          <code>Drop7Game.tsx</code>.
        </p>
        <p>
          A worker response belongs to the position that created it. Terminate the worker on cleanup and ignore any
          message after cancellation. Preserve progress messages because they are the usable fallback when the next
          depth does not finish inside the budget.
        </p>
      </AgentContext>
    </>
  );
}

function NativeBody() {
  return (
    <>
      <h2 id="design">Design</h2>
      <p>
        The C++ engine is the stable research baseline. It keeps the same 49-cell, row-major board as the TypeScript
        rules and translates those operations into plain native loops. The goal is a shared meaning for every move,
        with enough throughput for full-game cohorts and search experiments.
      </p>

      <h2 id="implementation">Implementation</h2>
      <h3>A close port of the readable rules</h3>
      <p>
        The state, scoring constants and wave record mirror the TypeScript types. During a cascade, the engine records
        every popper before it changes any gray disc, consumes reveals in row-major order, and applies gravity after
        the complete wave. The comments call out that ordering because later chain waves can observe it.
      </p>
      <CodeSnippet
        path="src/core/native/engine.hpp"
        startLine={213}
        endLine={267}
        title="Resolve one chain with observable ordering intact"
        caption="The C++ loop keeps the pre-clear board for gray-disc hits, consumes queued reveals in reading order, then settles the result."
      />

      <h3>Reproducible games across languages</h3>
      <p>
        A headless game derives the next visible disc and each move&apos;s reveal stream from separate domains of the same
        seed. That lets the TypeScript and C++ drivers receive the same randomness without depending on how many random
        calls a policy makes between moves.
      </p>
      <CodeSnippet
        path="src/core/native/engine.hpp"
        startLine={341}
        endLine={354}
        title="Derive one move&apos;s reveal stream from the game seed"
        caption="The next disc and gray reveals use named domains, so a parity replay can reconstruct the same game on either side."
      />

      <h3>A separate sampled move loop</h3>
      <p>
        Research search does not draw one random future. The policy layer supplies stratified chance samples and a
        templated random source. It carries a second copy of the cascade and move loop so the sample order and
        floating-point accumulation remain fixed.
      </p>
      <CodeSnippet
        path="src/core/native/public-behavior.hpp"
        startLine={579}
        endLine={607}
        title="Stratify each chance event deterministically"
        caption="Every sample visits one rotated stratum with deterministic jitter, giving the search repeatable chance coverage."
      />

      <h2 id="uses">Uses</h2>
      <p>
        The <Link href="/approach/fair-expectimax/reference">fair depth-3 and depth-4 policy</Link> is built directly
        on this engine. The <Link href="/approach/ntuple-rl/native-suite">native suite</Link>, neural training
        environments and most C++ experiment entry points share it through the common whole-game harness. It is also
        the anchor for every alternative engine&apos;s trajectory gate.
      </p>

      <h2 id="verification">Verification</h2>
      <p>
        The TypeScript parity run matched 256 games and 6,852 moves exactly (
        <SourceRef source={REPRODUCIBILITY} label="reproducibility guide" />). The scenario, fast C++ and Rust engines
        each add independent move-for-move replays against this implementation.
      </p>
      <TechnicalRecord summary="The independent trajectory gates anchored to the C++ reference">
        <ul>
          <li>
            Scenario engine: 8,192 game-plays and 218,470 moves, with no mismatch (
            <SourceRef source={FINDING_02} label="finding-02" />).
          </li>
          <li>
            Fast C++ engine: 8,288 games, 438,020 moves and 548,263 waves, with no mismatch (
            <SourceRef source={FINDING_13} label="finding-13" />).
          </li>
          <li>
            Rust engine: three trajectory arms covering 36,427 moves and 40,286 waves, with no mismatch (
            <SourceRef source={RS_RUST} />).
          </li>
        </ul>
      </TechnicalRecord>

      <h2 id="limits">Limits</h2>
      <p>
        The sampled policy loop duplicates the move rules and has indirect search-parity coverage, while no focused
        gate compares it with <code>playMove</code> directly (<SourceRef source={AUDIT_01} />). Compiler settings are
        also part of the scientific result: the efficiency audit found leaf-value changes from floating-point
        contraction, then restored bit identity by disabling contraction (<SourceRef source={AUDIT_06} />).
      </p>
      <AgentContext summary="Build flags, parity entry points and the duplicated-loop risk">
        <p>
          The pinned source is <code>src/core/native/engine.hpp</code>; the sampled policy path is in{" "}
          <code>src/core/native/public-behavior.hpp</code>. Preserve reveal order, column order and floating-point
          accumulation. Native comparison builds use clang++ with <code>-ffp-contract=off</code> where bit parity is
          required.
        </p>
        <p>
          Run <code>make test-native</code> and <code>make parity</code> before accepting a semantic change. A future
          direct gate should drive identical explicit random values through <code>playMove</code> and{" "}
          <code>playMoveSampled</code> and compare the complete move record.
        </p>
      </AgentContext>
    </>
  );
}

function FastBody() {
  return (
    <>
      <h2 id="design">Design</h2>
      <p>
        The fast C++ engine changes storage and allocation while preserving the C++ reference&apos;s observable order.
        It was built for search, where one decision applies the rules and evaluates a leaf many times. Integer
        mechanics could be reorganized; chance order and floating-point expressions stayed fixed.
      </p>

      <h2 id="implementation">Implementation</h2>
      <h3>One scan per wave</h3>
      <p>
        The reference counts a numbered disc&apos;s row and column by walking outward from that disc. The fast engine
        builds seven row masks, seven column masks and one numbered-cell bitboard in a single pass. A lookup table then
        answers the run length at every occupied position.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/fast-engine/fast-engine.hpp"
        startLine={121}
        endLine={172}
        title="Turn the board into occupancy masks"
        caption="The scan records the same board in the shapes needed by row checks, column checks and numbered-disc iteration."
      />

      <h3>Gravity only where a hole can exist</h3>
      <p>
        A wave can leave holes only in columns where a disc popped. Reveals replace gray discs in place, so they do not
        make a new gap. The cascade records the touched columns as a bit mask and compacts those columns in place.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/fast-engine/fast-engine.hpp"
        startLine={220}
        endLine={239}
        title="Compact the changed columns in place"
        caption="Each set bit selects one column, and the bottom-up copy preserves disc order while clearing the space above it."
      />

      <h3>No wave allocation inside search</h3>
      <p>
        Full-game replays need every wave. Search needs only the count and the last chain depth. A templated sink lets
        the same move loop write either representation, so the hot path avoids constructing a vector it never reads.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/fast-engine/fast-engine.hpp"
        startLine={259}
        endLine={296}
        title="Choose the wave record at compile time"
        caption="Search receives a two-field sink, while trajectory tools retain the bounded full record needed for parity."
      />

      <h3>A packed search key</h3>
      <p>
        The search cache stores the board, next disc, moves to the next rise and depth in four machine words. Its
        open-addressed table and intrusive least-recently-used list keep the reference eviction behavior without
        allocating strings and list nodes at interior search states.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/fast-engine/fast-search.hpp"
        startLine={49}
        endLine={80}
        title="Pack every cache field into four words"
        caption="Four bits per cell leave room for the scalar search fields while keeping key equality to four integer comparisons."
      />

      <h2 id="uses">Uses</h2>
      <p>
        This is the main CPU engine for fair-search cohorts under lifetime-objective. It also supports leaf evolution,
        learned-leaf experiments and the native fair policy in the scripted-round playground. The reference engine
        remains the semantic anchor; this implementation is the high-throughput execution path.
      </p>

      <h2 id="verification">Verification</h2>
      <p>
        On 24 real depth-4, five-strata decisions with three interleaved repeats, the complete engine, cache and leaf
        path measured 3.08 times the reference on the shared host (<SourceRef source={FINDING_13} label="finding-13" />).
        That is an engineering result. It does not measure policy strength.
      </p>
      <TechnicalRecord summary="Trajectory, search and leaf gates behind the speed result">
        <ul>
          <li>
            The trajectory gate compared 438,020 moves and 548,263 waves across 8,288 games with no mismatch (
            <SourceRef source={FINDING_13} label="finding-13" />).
          </li>
          <li>
            Search parity covered 306 moves across nine depth and strata configurations, with matching actions, work
            and completed depth (<SourceRef source={FINDING_13} label="finding-13" />).
          </li>
          <li>
            The leaf gate compared 225,183 real states by floating-point bit pattern, with no mismatch (
            <SourceRef source={FINDING_13} label="finding-13" />).
          </li>
        </ul>
      </TechnicalRecord>

      <h2 id="limits">Limits</h2>
      <p>
        The measurements came from a heavily shared machine, so the retained record treats back-to-back ratios as the
        useful quantity and leaves absolute timing unpromoted. The implementation has no latent-board mode, and its
        native builds and gates were validated with clang++ only.
      </p>
      <AgentContext summary="Equivalence contract, build entry points and required fast-engine gates">
        <p>
          The source contract at the top of <code>fast-engine.hpp</code> permits integer, layout and allocation
          changes. Do not reorder chance draws or floating-point expressions. The reference order includes row-major
          poppers, row-major reveals, complete wave order and strict cache eviction behavior.
        </p>
        <p>
          Build with <code>approaches/lifetime-objective/fast-engine/build.sh</code>. Before using a change, run{" "}
          <code>gate-leaf</code>, <code>gate-search</code> and <code>gate-trajectory</code> from the generated build
          directory. The retained methodology and measurement details are in{" "}
          <SourceRef source={FINDING_13} label="finding-13" />.
        </p>
      </AgentContext>
    </>
  );
}

function ScenarioBody() {
  return (
    <>
      <h2 id="design">Design</h2>
      <p>
        The base engine chooses a gray disc&apos;s number when that disc opens. It therefore has no persistent hidden board
        that can be fixed and solved as one puzzle. The scenario engine pairs every covered cell with a hidden value,
        fixes the future disc tape and future risen rows, then asks for the best line through that complete future.
        The result is an oracle diagnostic that reads information a playable policy cannot see.
      </p>

      <h2 id="implementation">Implementation</h2>
      <h3>Board and hidden values move together</h3>
      <p>
        Gravity and row rises apply the same permutation to two arrays: the visible board and its latent values. The
        visible transform calls the shared C++ rule, while the paired loop moves each hidden value with its gray disc.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/scenario/scenario.hpp"
        startLine={51}
        endLine={85}
        title="Keep latent values attached during gravity and rises"
        caption="The paired transforms make board motion and hidden-value motion explicit, then reuse the reference engine for the visible board."
      />

      <h3>Two reveal sources, one move loop</h3>
      <p>
        Stream mode draws at reveal time and reproduces the base engine. Latent mode reads the value already attached
        to the covered cell, advances an explicit disc tape, and supplies a fixed hidden row on each rise. Both feed
        the same templated cascade.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/scenario/scenario.hpp"
        startLine={92}
        endLine={144}
        title="Swap the source of hidden values"
        caption="The move rules call one small interface for reveals, the next visible disc and future covered rows."
      />

      <h3>A small exact state</h3>
      <p>
        At a fixed search depth, the tape index, rise index and moves to the next rise are already known. A search node
        therefore needs only the visible board and latent board. The solver can memoize that pair plus depth and return
        the exact best remaining score inside the scenario&apos;s horizon.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/scenario/solver.hpp"
        startLine={55}
        endLine={88}
        title="Reconstruct one deterministic move from depth"
        caption="Depth selects the visible disc, rise row and move clock, leaving board plus latent board as the changing search state."
      />

      <h2 id="uses">Uses</h2>
      <p>
        The solver can label a fixed position with its best action, measure how far a shallow policy is from the known
        optimum, and test whether a proposed bound is admissible. The scenario suite also checks exact-solver
        infrastructure. Since it sees hidden values and the full future tape, its scores belong to oracle analysis and
        cannot be compared with public-policy game scores.
      </p>

      <h2 id="verification">Verification</h2>
      <p>
        Stream mode replayed 8,192 game-plays and 218,470 moves against the C++ engine with no mismatch (
        <SourceRef source={FINDING_02} label="finding-02" />). The optimized exact solver was also compared with a
        deliberately naive enumerator on 107 scenarios and four solver variants, for 428 matching comparisons from
        the same finding.
      </p>
      <TechnicalRecord summary="The parity and exactness checks for the scenario instrument">
        <ul>
          <li>Whole trajectories compare boards, scores, move clocks, terminal state and every wave.</li>
          <li>Paired gravity and row-rise transforms are checked independently on generated boards.</li>
          <li>Latent scenarios validate that every covered cell has one hidden value and that JSONL round trips preserve it.</li>
          <li>The exact solver runs with its table, bound and thread settings crossed against naive enumeration.</li>
        </ul>
        <p>
          The recorded command is <code>build/scenario/scenario-parity --seeds 4096</code>; details and retained
          results are in <SourceRef source={FINDING_02} label="finding-02" />.
        </p>
      </TechnicalRecord>

      <h2 id="limits">Limits</h2>
      <p>
        Fixing a hidden board changes the game&apos;s dynamics even though each individual hidden value has the same
        uniform marginal distribution as an on-reveal draw. The oracle&apos;s answer is exact only for its recorded tape
        and horizon. The retained benchmark also found that its admissible score bound pruned no nodes at the tested
        horizons; the transposition table supplied the useful reduction (<SourceRef source={FINDING_02} label="finding-02" />).
      </p>
      <AgentContext summary="Information boundary, scenario invariants and extension commands">
        <p>
          Treat every latent scenario, optimum and principal variation as oracle data. Do not expose hidden values,
          tape entries or scenario identity to a deployable policy. A student may train on an oracle label only under
          a registered teacher protocol and must be frozen before public-interface evaluation.
        </p>
        <p>
          Scenario IDs hash the visible board, latent board, move clock, horizon, disc tape and future rise rows. Run{" "}
          <code>approaches/lifetime-objective/scenario/build.sh</code>, then <code>build/scenario/solve --self-test</code>
          and the parity command above after changing the record shape, paired transforms or solver.
        </p>
      </AgentContext>
    </>
  );
}

function RustClassicBody() {
  return (
    <>
      <h2 id="design">Design</h2>
      <p>
        This crate implements Classic mode for deterministic replay. Classic has a decreasing move clock, can deal a
        gray disc as the incoming piece, and uses historical 7,000-point scoring, archival, for its rise bonus. Every
        random future arrives as an argument, which makes a submitted game reproducible without embedding a random
        generator in the rules.
      </p>

      <h2 id="implementation">Implementation</h2>
      <h3>Randomness at the boundary</h3>
      <p>
        A numbered drop carries no hidden value. A gray drop must carry the value that will appear when it opens. A
        successful rise must receive its next hidden row, and a non-terminal move receives the next visible disc. The
        function checks those combinations before touching the board.
      </p>
      <CodeSnippet
        path="src/core/rust/classic-engine/src/lib.rs"
        startLine={92}
        endLine={123}
        title="Make every random future an explicit move input"
        caption="The type boundary separates visible pieces, hidden values and future covered rows before the cascade begins."
      />

      <h3>Visible and hidden boards stay paired</h3>
      <p>
        The representation is intentionally plain: one byte array for visible cells and another for hidden values.
        Gravity and row rises copy the arrays together, so a gray disc keeps its value as it moves.
      </p>
      <CodeSnippet
        path="src/core/rust/classic-engine/src/lib.rs"
        startLine={300}
        endLine={335}
        title="Move a gray disc and its hidden value together"
        caption="Both transforms apply the same source and destination indexes to the visible and latent arrays."
      />

      <h2 id="uses">Uses</h2>
      <p>
        The design fits server-side validation of a mobile game tape and leaves future research free to choose its own
        sampling policy. The current mobile validation path still uses the TypeScript Classic engine, and no policy or
        service calls this Rust crate today.
      </p>

      <h2 id="verification">Verification</h2>
      <p>
        The crate shares one conformance transition with the TypeScript Classic tests and carries focused unit tests
        for its clock, scoring, gray drops and level boundary. It has no whole-game parity record.
      </p>
      <TechnicalRecord summary="The tests currently carried by the Classic Rust crate">
        <ul>
          {gateTests("src/core/rust/classic-engine (cargo test)").map((test) => (
            <li key={test}>
              <code>{test}</code>
            </li>
          ))}
        </ul>
      </TechnicalRecord>

      <h2 id="limits">Limits</h2>
      <p>
        One shared transition checks a useful boundary, while it cannot establish whole-trajectory parity. The crate
        also has no consumer, benchmark or native parity harness. Its next useful step is integration with an actual
        replay caller followed by paired full-game tapes against the TypeScript Classic implementation.
      </p>
      <AgentContext summary="Missing integration work and the minimum next parity gate">
        <p>
          Run <code>cargo test --manifest-path src/core/rust/classic-engine/Cargo.toml</code> for the local crate. A
          whole-game gate should feed identical explicit tapes to the Rust and TypeScript Classic engines and compare
          every board, latent board, score delta, clock transition, terminal flag and wave in order.
        </p>
        <p>
          Keep historical 7,000-point scoring, archival, separate from corrected five-move Hardcore results. Do not
          place Classic scores in a comparison table with corrected 17,000-point results.
        </p>
      </AgentContext>
    </>
  );
}

function GpuBody() {
  return (
    <>
      <h2 id="design">Design</h2>
      <p>
        This page is a training-system diagnostic. It does not implement Drop7 moves. The harness asks whether the
        workstation&apos;s integrated AMD GPU can train the small board networks used by policy and value experiments,
        whether its answers agree with a higher-precision reference, and which software paths are safe to use.
      </p>

      <h2 id="implementation">Implementation</h2>
      <h3>A board-shaped workload</h3>
      <p>
        The test network receives a stack of board planes, passes them through residual blocks, and ends in a column
        policy head plus a scalar value head. This gives the benchmark the convolution, normalization and optimizer
        work that a Drop7 learner would perform instead of timing an unrelated image model.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/gpu/bench.py"
        startLine={300}
        endLine={348}
        title="A policy and value network for a seven-column board"
        caption="The harness measures a residual trunk with the two outputs a Drop7 learner needs: column logits and one scalar estimate."
      />

      <h3>A normalization path chosen by evidence</h3>
      <p>
        Batch normalization failed to compile in training mode on the installed GPU stack. Preloading a second vendor
        library made that operation start and later introduced unstable device contexts. The retained safe path uses
        group normalization, which stays inside the framework&apos;s native kernels.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/gpu/bench.py"
        startLine={277}
        endLine={297}
        title="Select the supported normalization kernel"
        caption="The factory keeps the broken batch-normalization path available for diagnosis and makes group normalization the working default."
      />

      <h3>Correctness before throughput</h3>
      <p>
        The harness probes the runtime, compares operations with a double-precision CPU reference, checks forward and
        backward passes, and runs a training soak before measuring speed. That ordering exposed silent CPU numerical
        defects that a GPU-versus-CPU comparison would otherwise have blamed on the GPU.
      </p>
      <CodeSnippet
        path="approaches/lifetime-objective/gpu/bench.py"
        startLine={451}
        endLine={469}
        title="Record correctness gates and known warnings separately"
        caption="A known unsupported kernel remains visible as a warning, while failures on the supported path fail the run."
      />

      <h2 id="uses">Uses</h2>
      <p>
        The GPU is useful for batched neural training. Exact game transitions and chain cascades remain on the CPU.
        The activation script supplies the runtime-library workaround, and the benchmark writes environment,
        correctness, throughput and telemetry data for a later machine-scoped record.
      </p>

      <h2 id="verification">Verification</h2>
      <p>
        Three retained findings cover runtime enablement, a multithreaded matrix-multiply race and nondeterministic CPU
        convolution: <SourceRef source="docs/exploratory/gpu-01-rocm-enablement.md" label="GPU enablement" />,{" "}
        <SourceRef source="docs/exploratory/gpu-02-openblas-sgemm-race.md" label="matrix-multiply race" /> and{" "}
        <SourceRef source="docs/exploratory/gpu-03-onednn-conv-nondeterminism.md" label="convolution nondeterminism" />.
        They establish a working training path on this machine and record the unsafe paths beside it.
      </p>
      <TechnicalRecord summary="What the GPU work measured and what it left outside scope">
        <ul>
          <li>The correctness stage compares GPU operations with a double-precision CPU calculation.</li>
          <li>The training stage measures forward, backward and optimizer work on the board-shaped network.</li>
          <li>The probe records runtime versions, visible device properties, system load and the shared memory pool.</li>
          <li>No game was played, no seed lease was opened and no policy-strength claim was made.</li>
        </ul>
      </TechnicalRecord>

      <h2 id="limits">Limits</h2>
      <p>
        Every throughput run in the retained finding used a contended host, so none is an idle-machine baseline. The
        working environment depends on a system runtime library and session-level device access. The CPU numerical
        defects were isolated to the tested builds and shapes; their upstream cause remains unresolved.
      </p>
      <AgentContext summary="Environment setup, safe defaults and the remeasurement boundary">
        <p>
          Read <code>approaches/lifetime-objective/gpu/activate.sh</code> before running the harness. Keep group
          normalization as the default and pin the affected CPU matrix library to the recorded safe thread count.
          Begin with <code>bench.py --probe</code> and <code>bench.py --correctness</code> before any throughput stage.
        </p>
        <p>
          A publishable timing result needs a fresh registered run, an exclusive resource lease, the retained machine
          profile and repeated idle-host measurements. Do not reuse the exploratory throughput figures as a clean
          baseline, and do not describe this package as a game engine.
        </p>
      </AgentContext>
    </>
  );
}

const BODIES: Partial<Record<EngineSlug, () => ReactNode>> = {
  typescript: TypeScriptBody,
  browser: BrowserBody,
  native: NativeBody,
  fast: FastBody,
  scenario: ScenarioBody,
  "rust-classic": RustClassicBody,
  gpu: GpuBody,
};

export function hasWrittenEngineBody(slug: EngineSlug): boolean {
  return BODIES[slug] !== undefined;
}

export function EngineBody({ slug }: { slug: EngineSlug }) {
  const Body = BODIES[slug];
  return Body ? <Body /> : null;
}
