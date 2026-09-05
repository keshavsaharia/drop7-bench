"use client";

/**
 * Interactive figures for the determinized-planning primer.
 *
 * The road-trip figure makes strategy fusion inspectable one step at a time.
 * The Drop7 figure uses the public board from oracle-scenario.json, generated
 * by web/scripts/generate-oracle-scenario.ts through the TypeScript engine.
 * Its sampled tapes and branch labels are schematic teaching marks, never
 * gameplay evidence or solver output.
 */
import {
  useEffect,
  useId,
  useRef,
  useState,
  useSyncExternalStore,
  type ReactNode,
} from "react";
import scenario from "../../content/learn/oracle-scenario.json";
import { DiscFace } from "../discs";
import { Drop7Board } from "../Drop7Board";
import "./determinization.css";

const STEP_MS = 5_200;

interface LessonStep {
  label: string;
  title: string;
  body: string;
}

interface LessonFigureProps {
  caption?: ReactNode;
}

export function DeterminizationRoadMap({ caption }: LessonFigureProps) {
  const titleId = useId();
  const descriptionId = useId();

  return (
    <figure className="fig fig--wide dz-road-map">
      <div className="fig-frame dz-road-map-frame">
        <div className="dz-road-map-heading">
          <div>
            <span className="dz-eyebrow">Two-day drive</span>
            <strong>Two roads, one destination</strong>
          </div>
          <div className="dz-road-map-legend" aria-hidden="true">
            <span><i data-leg="first" />Day 1</span>
            <span><i data-leg="second" />Day 2</span>
          </div>
        </div>
        <div className="dz-road-map-scroll">
          <svg
            className="dz-road-map-svg"
            viewBox="0 0 960 500"
            role="img"
            aria-labelledby={`${titleId} ${descriptionId}`}
          >
            <title id={titleId}>Bird&apos;s-eye map of the two-day drive</title>
            <desc id={descriptionId}>
              Both first-day roads take three hours. From the mountain stop, the pass takes two hours and the detour takes five. From the coast stop, the second day takes three and a half hours.
            </desc>

            <path className="dz-map-land" d="M0 0H960V500H0Z" />
            <path className="dz-map-water" d="M0 392C132 342 226 424 348 385C482 342 569 428 704 377C809 338 877 351 960 317V500H0Z" />
            <g className="dz-map-contours" aria-hidden="true">
              <path d="M188 46C268 8 376 30 416 91C449 141 401 185 319 178C248 172 184 122 188 46Z" />
              <path d="M238 63C293 39 357 51 382 91C401 123 370 151 318 144C270 138 235 111 238 63Z" />
              <path d="M554 61C648 13 764 36 807 105C838 154 788 200 707 190C625 180 553 135 554 61Z" />
              <path d="M610 78C670 50 745 64 771 105C792 138 757 165 706 157C657 150 611 124 610 78Z" />
              <path d="M107 346C169 317 248 327 282 367C312 401 279 431 220 426C161 421 111 395 107 346Z" />
            </g>
            <g className="dz-map-peaks" aria-hidden="true">
              <path d="M244 131L301 45L358 131Z" />
              <path d="M319 137L374 70L429 137Z" />
              <path d="M627 137L692 37L757 137Z" />
              <path d="M713 153L766 78L819 153Z" />
            </g>
            <g className="dz-map-waves" aria-hidden="true">
              <path d="M39 430q18-15 36 0t36 0t36 0" />
              <path d="M170 460q18-15 36 0t36 0t36 0" />
              <path d="M686 435q18-15 36 0t36 0t36 0" />
              <path d="M806 467q18-15 36 0t36 0t36 0" />
            </g>

            <g className="dz-map-route-bases" aria-hidden="true">
              <path d="M105 251C191 199 273 150 370 139" />
              <path d="M105 269C196 323 278 369 374 385" />
              <path d="M422 132C559 66 715 91 852 235" />
              <path d="M422 151C547 212 641 318 850 255" />
              <path d="M426 390C595 430 714 360 853 272" />
            </g>
            <g className="dz-map-route-traces" aria-hidden="true">
              <path className="dz-map-trace dz-map-trace--first" pathLength="100" d="M105 251C191 199 273 150 370 139" />
              <path className="dz-map-trace dz-map-trace--first" pathLength="100" d="M105 269C196 323 278 369 374 385" />
              <path className="dz-map-trace dz-map-trace--pass" pathLength="100" d="M422 132C559 66 715 91 852 235" />
              <path className="dz-map-trace dz-map-trace--detour" pathLength="100" d="M422 151C547 212 641 318 850 255" />
              <path className="dz-map-trace dz-map-trace--coast" pathLength="100" d="M426 390C595 430 714 360 853 272" />
            </g>

            <g className="dz-map-node dz-map-node--start" transform="translate(76 260)">
              <circle r="25" />
              <text className="dz-map-node-kicker" textAnchor="middle" y="-34">Start</text>
              <text className="dz-map-node-label" textAnchor="middle" y="5">Day 1</text>
            </g>
            <g className="dz-map-node dz-map-node--mountain" transform="translate(397 139)">
              <circle r="25" />
              <text className="dz-map-node-kicker" textAnchor="middle" y="-36">Night 1</text>
              <text className="dz-map-node-label" textAnchor="middle" y="6">Mountain</text>
            </g>
            <g className="dz-map-node dz-map-node--coast" transform="translate(400 389)">
              <circle r="25" />
              <text className="dz-map-node-kicker" textAnchor="middle" y="-36">Night 1</text>
              <text className="dz-map-node-label" textAnchor="middle" y="6">Coast</text>
            </g>
            <g className="dz-map-node dz-map-node--finish" transform="translate(881 257)">
              <circle r="27" />
              <text className="dz-map-node-kicker" textAnchor="middle" y="-38">Finish</text>
              <text className="dz-map-node-label" textAnchor="middle" y="6">Day 2</text>
            </g>

            <g className="dz-map-route-label" transform="translate(204 173)">
              <rect width="112" height="43" rx="8" />
              <text x="56" y="17" textAnchor="middle">Mountain</text>
              <text className="dz-map-time" x="56" y="34" textAnchor="middle">Day 1 · 3 h</text>
            </g>
            <g className="dz-map-route-label" transform="translate(211 319)">
              <rect width="102" height="43" rx="8" />
              <text x="51" y="17" textAnchor="middle">Coast</text>
              <text className="dz-map-time" x="51" y="34" textAnchor="middle">Day 1 · 3 h</text>
            </g>
            <g className="dz-map-route-label dz-map-route-label--pass" transform="translate(558 73)">
              <rect width="126" height="48" rx="8" />
              <text x="63" y="18" textAnchor="middle">Open pass · 2 h</text>
              <text className="dz-map-total" x="63" y="37" textAnchor="middle">5 h total</text>
            </g>
            <g className="dz-map-route-label dz-map-route-label--detour" transform="translate(574 251)">
              <rect width="128" height="48" rx="8" />
              <text x="64" y="18" textAnchor="middle">Detour · 5 h</text>
              <text className="dz-map-total" x="64" y="37" textAnchor="middle">8 h total</text>
            </g>
            <g className="dz-map-route-label dz-map-route-label--coast" transform="translate(628 367)">
              <rect width="137" height="48" rx="8" />
              <text x="68.5" y="18" textAnchor="middle">Coast · 3.5 h</text>
              <text className="dz-map-total" x="68.5" y="37" textAnchor="middle">6.5 h total</text>
            </g>
          </svg>
        </div>
      </div>
      <figcaption>{caption ?? "This schematic follows both three-hour first-day roads, then labels each second-day route and its total travel time."}</figcaption>
    </figure>
  );
}

function subscribeToReducedMotion(onChange: () => void) {
  const query = window.matchMedia("(prefers-reduced-motion: reduce)");
  query.addEventListener("change", onChange);
  return () => query.removeEventListener("change", onChange);
}

function prefersReducedMotion() {
  return window.matchMedia("(prefers-reduced-motion: reduce)").matches;
}

function serverPrefersReducedMotion() {
  return false;
}

function StepControls({
  steps,
  step,
  playing,
  reducedMotion,
  onStep,
  onToggle,
}: {
  steps: readonly LessonStep[];
  step: number;
  playing: boolean;
  reducedMotion: boolean;
  onStep: (next: number) => void;
  onToggle: () => void;
}) {
  return (
    <div className="dz-controls">
      <div className="dz-step-tabs" role="tablist" aria-label="Explanation steps">
        {steps.map((item, index) => (
          <button
            key={item.label}
            type="button"
            role="tab"
            aria-selected={step === index}
            className="dz-step-tab"
            data-active={step === index ? "true" : undefined}
            onClick={() => onStep(index)}
          >
            <span>{index + 1}</span>
            {item.label}
          </button>
        ))}
      </div>
      <button
        type="button"
        className="dz-play"
        onClick={onToggle}
        disabled={reducedMotion}
        aria-label={
          reducedMotion
            ? "Autoplay is off because reduced motion is enabled"
            : playing
              ? "Pause the explanation"
              : "Play the explanation"
        }
      >
        <span aria-hidden="true">{playing && !reducedMotion ? "Ⅱ" : "▶"}</span>
        {reducedMotion ? "Autoplay off" : playing ? "Pause" : "Play"}
      </button>
    </div>
  );
}

function LessonFigure({
  kind,
  eyebrow,
  steps,
  renderStage,
  caption,
}: {
  kind: string;
  eyebrow: string;
  steps: readonly LessonStep[];
  renderStage: (step: number) => ReactNode;
  caption: ReactNode;
}) {
  const [step, setStep] = useState(0);
  const [playing, setPlaying] = useState(true);
  const [visible, setVisible] = useState(false);
  const root = useRef<HTMLElement>(null);
  const reducedMotion = useSyncExternalStore(
    subscribeToReducedMotion,
    prefersReducedMotion,
    serverPrefersReducedMotion,
  );

  useEffect(() => {
    const node = root.current;
    if (!node || !("IntersectionObserver" in window)) return;
    const observer = new IntersectionObserver(
      ([entry]) => setVisible(entry.isIntersecting),
      { threshold: 0.25 },
    );
    observer.observe(node);
    return () => observer.disconnect();
  }, []);

  useEffect(() => {
    if (!playing || !visible || reducedMotion) return;
    const timer = window.setInterval(
      () => setStep((current) => (current + 1) % steps.length),
      STEP_MS,
    );
    return () => window.clearInterval(timer);
  }, [playing, reducedMotion, steps.length, visible]);

  const chooseStep = (next: number) => {
    setStep(next);
    setPlaying(false);
  };

  return (
    <figure ref={root} className={`fig fig--wide dz-lesson dz-lesson--${kind}`}>
      <div className="fig-frame dz-frame">
        <div className="dz-lesson-head">
          <div>
            <span className="dz-eyebrow">{eyebrow} · step {step + 1} of {steps.length}</span>
            <h3>{steps[step].title}</h3>
            <p>{steps[step].body}</p>
          </div>
          <StepControls
            steps={steps}
            step={step}
            playing={playing}
            reducedMotion={reducedMotion}
            onStep={chooseStep}
            onToggle={() => setPlaying((current) => !current)}
          />
        </div>
        <div
          key={step}
          className="dz-stage"
          aria-live={playing && !reducedMotion ? "off" : "polite"}
        >
          {renderStage(step)}
        </div>
        <div className="dz-progress" aria-hidden="true">
          <span
            key={`${step}-${playing}`}
            data-running={playing && visible && !reducedMotion ? "true" : undefined}
          />
        </div>
      </div>
      <figcaption>{caption}</figcaption>
    </figure>
  );
}

const ROAD_STEPS: readonly LessonStep[] = [
  {
    label: "Choices",
    title: "Choose a road before tomorrow's weather",
    body: "The coast has a fixed travel time. The mountain is faster only if tomorrow's pass can be used well.",
  },
  {
    label: "Futures",
    title: "Solve each forecast as a separate puzzle",
    body: "Inside a forecast, the planner is allowed to choose tomorrow's road with that forecast already known.",
  },
  {
    label: "Fusion",
    title: "Average three incompatible plans",
    body: "The mountain receives credit for taking the pass in two futures and the detour in the other one.",
  },
  {
    label: "Reality",
    title: "Require one plan a driver can follow",
    body: "At dawn the driver still cannot see the weather. Each mountain plan must make the same second-day choice in all three futures.",
  },
];

const FORECASTS = [
  { name: "Future A", weather: "pass open", second: "pass · 2 h", total: "5 h", closed: false },
  { name: "Future B", weather: "pass open", second: "pass · 2 h", total: "5 h", closed: false },
  { name: "Future C", weather: "pass closed", second: "detour · 5 h", total: "8 h", closed: true },
] as const;

function ForecastStrip() {
  return (
    <div className="dz-forecast-strip" aria-label="Three forecasts">
      {FORECASTS.map((forecast) => (
        <div key={forecast.name} data-closed={forecast.closed ? "true" : undefined}>
          <span>{forecast.name}</span>
          <strong>{forecast.weather}</strong>
        </div>
      ))}
    </div>
  );
}

function RoadChoices() {
  return (
    <div className="dz-road-setup">
      <div className="dz-start-node"><span>start</span><strong>today</strong></div>
      <div className="dz-road-arrow" aria-hidden="true">→</div>
      <div className="dz-road-options">
        <article>
          <span className="dz-card-label">Mountain</span>
          <strong>3 h today</strong>
          <div className="dz-road-fork"><span>pass tomorrow</span><span>detour tomorrow</span></div>
        </article>
        <article data-safe="true">
          <span className="dz-card-label">Coast</span>
          <strong>3 h today</strong>
          <div className="dz-road-fork"><span>3.5 h tomorrow</span></div>
        </article>
      </div>
      <ForecastStrip />
    </div>
  );
}

function SolvedForecasts() {
  return (
    <div className="dz-solved-grid">
      {FORECASTS.map((forecast) => (
        <article key={forecast.name} data-closed={forecast.closed ? "true" : undefined}>
          <header><span>{forecast.name}</span><strong>{forecast.weather}</strong></header>
          <div className="dz-route-line">
            <span>start</span><i aria-hidden="true" /><span>mountain · 3 h</span><i aria-hidden="true" /><span>{forecast.second}</span>
          </div>
          <div className="dz-route-total"><span>best route in this future</span><strong>{forecast.total}</strong></div>
        </article>
      ))}
    </div>
  );
}

function FusionAverage() {
  return (
    <div className="dz-fusion">
      <div className="dz-private-plans">
        {FORECASTS.map((forecast) => (
          <div key={forecast.name}>
            <span>{forecast.name}</span>
            <strong>{forecast.second}</strong>
            <em>{forecast.total}</em>
          </div>
        ))}
      </div>
      <div className="dz-fusion-arrow" aria-hidden="true"><i /><span>average</span><i /></div>
      <div className="dz-fused-result">
        <span>mountain&apos;s reported time</span>
        <strong>(5 + 5 + 8) ÷ 3 = 6 h</strong>
        <em>planner picks mountain over coast at 6.5 h</em>
      </div>
      <p className="dz-warning">The 6-hour plan changes its day-two action after consulting the private forecast.</p>
    </div>
  );
}

function ExecutablePlans() {
  return (
    <div className="dz-plan-table-wrap">
      <div className="dz-one-rule"><span>one driver</span><i aria-hidden="true">→</i><strong>one rule at dawn</strong></div>
      <table className="dz-plan-table">
        <thead>
          <tr><th>Plan fixed today</th><th>Open</th><th>Open</th><th>Closed</th><th>Average</th></tr>
        </thead>
        <tbody>
          <tr data-impossible="true"><td>Mountain, switch with forecast</td><td>5 h</td><td>5 h</td><td>8 h</td><td>6 h <span>cannot follow</span></td></tr>
          <tr><td>Mountain, always take pass</td><td>5 h</td><td>5 h</td><td>11 h</td><td>7 h</td></tr>
          <tr><td>Mountain, always take detour</td><td>8 h</td><td>8 h</td><td>8 h</td><td>8 h</td></tr>
          <tr data-best="true"><td>Coast</td><td>6.5 h</td><td>6.5 h</td><td>6.5 h</td><td>6.5 h <span>best real plan</span></td></tr>
        </tbody>
      </table>
    </div>
  );
}

function RoadStage({ step }: { step: number }) {
  if (step === 0) return <RoadChoices />;
  if (step === 1) return <SolvedForecasts />;
  if (step === 2) return <FusionAverage />;
  return <ExecutablePlans />;
}

export function DeterminizationRoadTrip({ caption }: LessonFigureProps) {
  return (
    <LessonFigure
      kind="road"
      eyebrow="Small example"
      steps={ROAD_STEPS}
      renderStage={(step) => <RoadStage step={step} />}
      caption={caption ?? "The road-trip example advances from the public choice to the three private plans, then compares the fused estimate with plans one driver can execute. Use the numbered controls to hold any step."}
    />
  );
}

const baseHidden = scenario.covered.slice(0, 3).map((index) => Number(scenario.oracle[index]));
const rotateDisc = (value: number, amount: number) => ((value - 1 + amount) % 7) + 1;
const SAMPLE_WORLDS = [
  { name: "Sample A", hidden: baseHidden, tape: [5, 2, 4], reply: "reply A" },
  { name: "Sample B", hidden: baseHidden.map((value) => rotateDisc(value, 2)), tape: [1, 7, 3], reply: "reply B" },
  { name: "Sample C", hidden: baseHidden.map((value) => rotateDisc(value, 4)), tape: [6, 1, 5], reply: "reply C" },
] as const;

const DROP7_STEPS: readonly LessonStep[] = [
  {
    label: "Public state",
    title: "Start from what the player can see",
    body: "The visible board, next disc and distance to the rise are known. Gray values, later discs and the next covered row are unknown.",
  },
  {
    label: "Samples",
    title: "Fill the unknowns with concrete guesses",
    body: "Each sample supplies a private answer key for gray discs and a private tape of future discs.",
  },
  {
    label: "Search",
    title: "Let every sample choose its own continuation",
    body: "A deep deterministic search can now react to its private answer key when it picks later columns.",
  },
  {
    label: "Public tree",
    title: "Merge branches that still look the same",
    body: "A fair planner must use one continuation while the public observation is unchanged. It may branch after a reveal, deal or rise becomes visible.",
  },
];

function DiscRow({ values }: { values: readonly number[] }) {
  return (
    <span className="dz-disc-row">
      {values.map((value, index) => (
        <span key={`${value}-${index}`} className="dz-disc-slot">
          <DiscFace cell={value} className="dz-disc" />
        </span>
      ))}
    </span>
  );
}

function PublicDrop7State() {
  return (
    <div className="dz-public-state">
      <div className="dz-drop7-board">
        <Drop7Board
          cells={scenario.board}
          nextDisc={scenario.nextDisc}
          size="min(100%, 19rem)"
          highlight={scenario.cracked}
          label="Engine-generated public Drop7 position with covered gray discs"
        />
      </div>
      <div className="dz-information-cards">
        <article data-kind="public">
          <span className="dz-card-label">Known now</span>
          <strong>next disc {scenario.nextDisc}</strong>
          <p>{scenario.movesRemaining} move until the next rise</p>
        </article>
        <article data-kind="hidden">
          <span className="dz-card-label">Still hidden</span>
          <strong>{scenario.covered.length} gray values</strong>
          <p>later discs and the next covered row</p>
        </article>
      </div>
    </div>
  );
}

function SampledDrop7Worlds() {
  return (
    <div className="dz-sample-layout">
      <div className="dz-sample-source">
        <span>same public position</span>
        <DiscFace cell={scenario.nextDisc} className="dz-source-disc" />
      </div>
      <div className="dz-sample-arrow" aria-hidden="true">→</div>
      <div className="dz-world-grid">
        {SAMPLE_WORLDS.map((world) => (
          <article key={world.name}>
            <header><span>{world.name}</span><strong>private scratchpad</strong></header>
            <div><span>sampled gray values</span><DiscRow values={world.hidden} /></div>
            <div><span>sampled future tape</span><DiscRow values={world.tape} /></div>
          </article>
        ))}
      </div>
    </div>
  );
}

function PrivateSearches() {
  return (
    <div className="dz-private-search">
      <div className="dz-root-move"><span>candidate root move</span><strong>drop {scenario.nextDisc} in column 4</strong></div>
      <div className="dz-search-branches">
        {SAMPLE_WORLDS.map((world) => (
          <article key={world.name}>
            <span>{world.name}</span>
            <DiscRow values={[...world.hidden.slice(0, 1), ...world.tape.slice(0, 1)]} />
            <i aria-hidden="true" />
            <strong>{world.reply}</strong>
            <em>best later column with this sample known</em>
          </article>
        ))}
      </div>
      <div className="dz-merge-value"><span>average at the root</span><strong>three private best continuations fused into one value</strong></div>
    </div>
  );
}

function PlanningTrees() {
  return (
    <div className="dz-tree-comparison">
      <article data-kind="fused">
        <header><span>Hindsight tree</span><strong>branches on private samples</strong></header>
        <div className="dz-tree-root">same visible state</div>
        <svg
          className="dz-tree-lines"
          viewBox="0 0 300 42"
          preserveAspectRatio="none"
          aria-hidden="true"
        >
          <path pathLength="100" d="M150 0V8M150 8L50 42M150 8V42M150 8L250 42" />
        </svg>
        <div className="dz-tree-leaves">
          {SAMPLE_WORLDS.map((world) => <span key={world.name}>{world.name}<strong>{world.reply}</strong></span>)}
        </div>
        <p>Every sample spends information before the game reveals it.</p>
      </article>
      <article data-kind="public">
        <header><span>Observable-state tree</span><strong>branches on public events</strong></header>
        <div className="dz-tree-root">same visible state</div>
        <div className="dz-tree-trunk" aria-hidden="true"><i /></div>
        <div className="dz-public-reply">one reply while the observation matches</div>
        <div className="dz-public-branch">reveal or deal becomes visible <span>then branch</span></div>
        <p>The action changes only after the player has new information.</p>
      </article>
      <div className="dz-pitfall-strip">
        <span>Replanning cannot refund the root move.</span>
        <span>More samples reduce noise while the fusion bias remains.</span>
        <span>Common random numbers pair the comparison; they do not repair the tree.</span>
      </div>
    </div>
  );
}

function Drop7Stage({ step }: { step: number }) {
  if (step === 0) return <PublicDrop7State />;
  if (step === 1) return <SampledDrop7Worlds />;
  if (step === 2) return <PrivateSearches />;
  return <PlanningTrees />;
}

export function DeterminizationDrop7({ caption }: LessonFigureProps) {
  return (
    <LessonFigure
      kind="drop7"
      eyebrow="Drop7 mechanism"
      steps={DROP7_STEPS}
      renderStage={(step) => <Drop7Stage step={step} />}
      caption={caption ?? `The public board comes from an engine-generated playground game (${scenario.seed}). The sample tapes and branch labels are schematic: they show where information enters the plan, and they are not policy results.`}
    />
  );
}

export function DeterminizationPitfalls({ caption }: LessonFigureProps) {
  return (
    <figure className="fig fig--wide dz-supplement dz-supplement--pitfalls">
      <div className="fig-frame dz-supplement-frame">
        <div className="dz-supplement-heading">
          <span className="dz-eyebrow">After the root search</span>
          <strong>What changes, and what stays</strong>
        </div>
        <div className="dz-pitfall-cards">
          <article>
            <span className="dz-card-label">Strategy fusion</span>
            <div className="dz-fusion-mini">
              <span className="dz-mini-node">same visible state</span>
              <svg viewBox="0 0 200 34" preserveAspectRatio="none" aria-hidden="true">
                <path pathLength="100" d="M100 0V7M100 7L50 34M100 7L150 34" />
              </svg>
              <div>
                <span>Sample A<strong>reply A</strong></span>
                <span>Sample B<strong>reply B</strong></span>
              </div>
            </div>
            <p>One root value keeps both private replies.</p>
          </article>

          <article>
            <span className="dz-card-label">Replanning</span>
            <div className="dz-lock-timeline" aria-label="The fused value is used before replanning begins">
              <span>fused value</span>
              <i aria-hidden="true" />
              <span data-locked="true">first disc lands</span>
              <i aria-hidden="true" />
              <span>search restarts</span>
            </div>
            <p>The original move has already used the fused value.</p>
          </article>

          <article>
            <span className="dz-card-label">More samples</span>
            <div className="dz-sample-growth" aria-label="More samples feed the same private continuation rule">
              <div>
                <span aria-hidden="true"><i /><i /><i /></span>
                <small>few samples</small>
              </div>
              <b aria-hidden="true">→</b>
              <div>
                <span aria-hidden="true"><i /><i /><i /><i /><i /><i /><i /><i /><i /></span>
                <small>more samples</small>
              </div>
              <b aria-hidden="true">→</b>
              <strong>same private rule</strong>
            </div>
            <p>The estimate steadies while private branching stays.</p>
          </article>
        </div>
      </div>
      <figcaption>{caption ?? "This schematic shows why separate sample replies, later replanning and a larger sample set leave the strategy-fusion error in place."}</figcaption>
    </figure>
  );
}

export function DeterminizationContinuationRules({ caption }: LessonFigureProps) {
  return (
    <figure className="fig fig--wide dz-supplement dz-supplement--rules">
      <div className="fig-frame dz-supplement-frame">
        <div className="dz-supplement-heading">
          <span className="dz-eyebrow">Continuation rules</span>
          <strong>The branch point changes the policy</strong>
        </div>
        <div className="dz-policy-flows">
          <article data-rule="hindsight">
            <header><strong>Hindsight</strong><span>branches before observation</span></header>
            <div className="dz-policy-track">
              <span className="dz-policy-node">same visible state</span>
              <i className="dz-policy-line" aria-hidden="true" />
              <span className="dz-policy-event">private sample</span>
              <i className="dz-policy-line" aria-hidden="true" />
              <span className="dz-policy-replies"><span>reply A</span><span>reply B</span></span>
            </div>
          </article>
          <article data-rule="open-loop">
            <header><strong>Open loop</strong><span>keeps one fixed sequence</span></header>
            <div className="dz-policy-track">
              <span className="dz-policy-node">same visible state</span>
              <i className="dz-policy-line" aria-hidden="true" />
              <span className="dz-policy-event">fixed sequence</span>
              <i className="dz-policy-line" aria-hidden="true" />
              <span className="dz-policy-replies"><span>same reply</span></span>
            </div>
          </article>
          <article data-rule="observable">
            <header><strong>Observable state</strong><span>branches after observation</span></header>
            <div className="dz-policy-track">
              <span className="dz-policy-node">same visible state</span>
              <i className="dz-policy-line" aria-hidden="true" />
              <span className="dz-policy-event">deal or reveal is visible</span>
              <i className="dz-policy-line" aria-hidden="true" />
              <span className="dz-policy-replies"><span>reply A</span><span>reply B</span></span>
            </div>
          </article>
        </div>
      </div>
      <figcaption>{caption ?? "The three timelines compare when each continuation rule allows later actions to separate."}</figcaption>
    </figure>
  );
}
