/** Qualitative lesson diagrams: one mechanism per card, no research measurements. */
import type { ReactNode } from "react";
import type { ArtProps } from "./registry";
import { ART_MONO, artSvgProps } from "./FallbackArt";
import "./art.css";
import "./lesson.css";

function Label({ x, y, children }: { x: number; y: number; children: ReactNode }) {
  return <text x={x} y={y} textAnchor="middle" fontFamily={ART_MONO} fontSize={10} fill="var(--color-ink-2)">{children}</text>;
}
function Dot({ x, y, value, covered = false }: { x: number; y: number; value?: number; covered?: boolean }) {
  return <g><circle cx={x} cy={y} r={15} fill={value ? `var(--color-disc-${value})` : "var(--color-raised)"} stroke={covered ? "var(--color-ink-3)" : "var(--color-rule-strong)"} strokeWidth={1.5} />{value && <text x={x} y={y} textAnchor="middle" dominantBaseline="central" fontFamily={ART_MONO} fontWeight={600} fontSize={14} fill={`var(--color-disc-${value}-fg)`}>{value}</text>}</g>;
}
function Box({ x, y, children }: { x: number; y: number; children: ReactNode }) {
  return <g><rect x={x - 34} y={y - 19} width={68} height={38} rx={7} fill="var(--color-raised)" stroke="var(--color-rule-strong)" /><Label x={x} y={y + 4}>{children}</Label></g>;
}
const Line = ({ d }: { d: string }) => <path d={d} fill="none" stroke="var(--color-rule-strong)" strokeWidth={1.5} />;
const Accent = ({ d }: { d: string }) => <path d={d} fill="none" stroke="var(--color-accent)" strokeWidth={2} />;

function Drawing({ name }: { name: string }) {
  switch (name) {
    case "rules":
      return <>
        <Label x={160} y={35}>match the run length</Label>
        <Dot x={118} y={94} value={5} /><Dot x={160} y={94} value={6} />
        <g data-anim="clear" opacity={0}><Dot x={202} y={94} value={3} /></g>
        <g data-anim="ring"><circle cx={202} cy={94} r={20} fill="none" stroke="var(--color-disc-3)" strokeWidth={2} strokeDasharray="3 4" /></g>
        <path d="M98 126v7h124v-7" fill="none" stroke="var(--color-ink-3)" />
        <Label x={160} y={153}>three touching discs</Label>
      </>;
    case "play":
      return <>
        <Label x={160} y={32}>choose a column</Label>
        {[90, 160, 230].map((x) => <path key={x} d={`M${x-22} 58v78h44V58`} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />)}
        <g data-anim="drop"><Dot x={160} y={115} value={4} /></g>
        <g data-anim="select"><path d="M151 145l9-8 9 8" fill="none" stroke="var(--color-accent)" strokeWidth={2} /></g>
      </>;
    case "chance-vs-choice":
      return <>
        <Line d="M72 90L149 48M72 90L149 132M171 48L248 36M171 48L248 78" />
        <g data-anim="branch"><Accent d="M72 90L149 48" /></g>
        <Dot x={60} y={90} value={4} /><circle cx={160} cy={48} r={10} fill="var(--color-accent)" /><circle cx={160} cy={132} r={10} fill="var(--color-raised)" stroke="var(--color-rule-strong)" />
        <g data-anim="reveal"><Dot x={262} y={34} value={2} /><Dot x={262} y={86} value={6} /></g>
        <Label x={83} y={161}>choose</Label><Label x={246} y={161}>average possibilities</Label>
      </>;
    case "evaluating-a-board":
      return <>
        <Label x={74} y={34}>board features</Label>
        {[58, 91, 124].map((y, i) => <g key={y}><rect x={34} y={y} width={84} height={9} rx={4} fill="var(--color-raised)" /><rect x={34} y={y} width={[58, 32, 72][i]} height={9} rx={4} fill={`var(--color-disc-${i+3})`} data-anim="grow" /></g>)}
        <Line d="M127 62L181 91M127 95H181M127 128L181 91M211 91h29" />
        <circle cx={195} cy={91} r={16} fill="var(--color-raised)" stroke="var(--color-accent)" /><Label x={195} y={95}>Σ</Label>
        <g data-anim="reveal"><circle cx={263} cy={91} r={21} fill="var(--color-accent-soft)" stroke="var(--color-accent)" /><path d="M254 91l6 6 12-13" fill="none" stroke="var(--color-accent)" strokeWidth={2} /></g>
        <Label x={257} y={151}>estimate</Label>
      </>;
    case "survival-vs-score":
      return <>
        <Line d="M42 35v108h235" />
        <path d="M50 135h47v-28h47V79h47V51h47" fill="none" stroke="var(--color-accent)" strokeWidth={3} />
        <g data-anim="steps"><circle cx={238} cy={51} r={6} fill="var(--color-disc-2)" /></g>
        <Label x={78} y={28}>score</Label><Label x={191} y={164}>successive rises</Label>
      </>;
    case "heavy-tails":
      return <>
        <Line d="M35 132h253" />
        {[50, 63, 76, 89, 102, 115, 128, 151, 205, 272].map((x, i) => <g key={x} data-anim={i > 7 ? "reveal" : undefined}><circle cx={x} cy={121} r={5} fill={i > 7 ? "var(--color-accent)" : "var(--color-ink-3)"} />{i < 6 && <circle cx={x} cy={107} r={5} fill="var(--color-ink-3)" />}{i > 0 && i < 5 && <circle cx={x} cy={93} r={5} fill="var(--color-ink-3)" />}</g>)}
        <Label x={92} y={65}>many games</Label><Label x={249} y={86}>a few long games</Label>
        <Label x={164} y={160}>shorter → longer</Label>
      </>;
    case "ranking-siblings":
      return <>
        {[0, 1, 2, 3, 4, 5, 6].map((i) => <g key={i}><Line d={`M160 40L${46 + i*38} 101`} /><rect x={32 + i*38} y={106} width={28} height={31} rx={5} fill="var(--color-raised)" stroke="var(--color-rule-strong)" /><Label x={46+i*38} y={126}>{i+1}</Label></g>)}
        <circle cx={160} cy={36} r={10} fill="var(--color-accent)" />
        <g data-anim="select"><Accent d="M160 46L198 99" /><rect x={184} y={106} width={28} height={31} rx={5} fill="none" stroke="var(--color-accent)" strokeWidth={2} /></g>
        <Label x={160} y={164}>compare every legal column</Label>
      </>;
    case "oracles-and-teachers":
      return <>
        <Line d="M160 25v125M103 97h113" />
        <Dot x={70} y={45} covered /><Label x={230} y={45}>visible input</Label>
        <Box x={70} y={99}>teacher</Box><Box x={250} y={99}>student</Box>
        <g data-anim="transfer"><rect x={171} y={91} width={11} height={15} rx={2} fill="var(--color-accent)" /></g>
        <Label x={160} y={161}>examples cross the boundary</Label>
      </>;
    case "does-more-compute-help":
      return <>
        <Line d="M160 30L96 69M160 30L224 69" />
        <circle cx={160} cy={30} r={6} fill="var(--color-accent)" />
        {[96,224].map((x) => <circle key={x} cx={x} cy={69} r={5} fill="var(--color-ink-2)" />)}
        <g data-anim="expand">{[64,128,192,256].map((x,i) => <g key={x}><Line d={`M${i<2?96:224} 74L${x} 106M${x} 111l-17 26M${x} 111l17 26`} /><circle cx={x} cy={106} r={5} fill="var(--color-accent)" /><circle cx={x-17} cy={137} r={3} fill="var(--color-ink-3)" /><circle cx={x+17} cy={137} r={3} fill="var(--color-ink-3)" /></g>)}</g>
        <Label x={160} y={165}>more futures to examine</Label>
      </>;
    case "learning-from-play":
      return <>
        <Line d="M105 55h110M250 75v37l-53 25M122 137L70 112V75" />
        <Box x={70} y={55}>play</Box><Box x={250} y={55}>record</Box><Box x={160} y={137}>learn</Box>
        <g data-anim="loop"><circle cx={120} cy={55} r={5} fill="var(--color-accent)" /></g>
      </>;
    case "vocabulary-game":
      return <>
        <Dot x={67} y={86} covered /><Line d="M91 86h42M177 86h42" />
        <g><Dot x={155} y={86} covered /><path d="M160 72l-8 13 9 2-10 14" fill="none" stroke="var(--color-ink-2)" strokeWidth={2} /></g>
        <g data-anim="reveal"><Dot x={243} y={86} value={5} /></g>
        <Label x={67} y={129}>covered</Label><Label x={155} y={129}>cracked</Label><Label x={243} y={129}>revealed</Label>
      </>;
    case "vocabulary-search":
      return <><Line d="M75 54L151 101M75 54L151 43M169 101L243 128M169 101L243 77" /><circle cx={75} cy={54} r={12} fill="var(--color-accent)" /><circle cx={160} cy={43} r={8} fill="var(--color-ink-3)" /><circle cx={160} cy={101} r={8} fill="var(--color-ink-3)" /><g data-anim="branch"><Accent d="M75 54L151 101M169 101L243 77" /></g><Box x={260} y={77}>leaf</Box><Label x={95} y={159}>look ahead</Label></>;
    case "vocabulary-learning":
      return <><Box x={72} y={90}>examples</Box><Line d="M106 90h38M204 90h31" /><g data-anim="transfer"><circle cx={130} cy={90} r={5} fill="var(--color-disc-5)" /></g><circle cx={174} cy={90} r={25} fill="var(--color-raised)" stroke="var(--color-accent)" /><Label x={174} y={94}>model</Label><Dot x={259} y={90} value={4} /><Label x={259} y={146}>choice</Label></>;
    case "vocabulary-evidence":
      return <><Label x={97} y={32}>candidate</Label><Label x={223} y={32}>reference</Label>{[60,90,120].map((y) => <g key={y}><Line d={`M104 ${y}h112`} /><circle cx={95} cy={y} r={7} fill="var(--color-accent)" /><circle cx={225} cy={y} r={7} fill="var(--color-disc-6)" /></g>)}<g data-anim="select"><rect x={76} y={74} width={168} height={32} rx={16} fill="none" stroke="var(--color-ink-2)" strokeDasharray="3 4" /></g><Label x={160} y={159}>the same games</Label></>;
    case "benchmarking":
      return <><Label x={160} y={34}>a shared set of rounds</Label>{[70,130,190,250].map((x,i) => <g key={x}><rect x={x-21} y={60} width={42} height={60} rx={5} fill="var(--color-raised)" stroke="var(--color-rule-strong)" /><Label x={x} y={96}>{i+1}</Label></g>)}<g data-anim="scan"><path d="M50 135h42" stroke="var(--color-accent)" strokeWidth={3} /></g></>;
    case "protocol":
      return <><Box x={67} y={89}>game</Box><Box x={253} y={89}>policy</Box><Line d="M107 73h104M107 107h104" /><g data-anim="transfer"><path d="M177 68l8 5-8 5" fill="none" stroke="var(--color-accent)" strokeWidth={2} /></g><g data-anim="return"><path d="M139 102l-8 5 8 5" fill="none" stroke="var(--color-disc-6)" strokeWidth={2} /></g><Label x={160} y={49}>position</Label><Label x={160} y={142}>column</Label></>;
    default: return null;
  }
}

const DESCRIPTIONS: Record<string, string> = {
  rules: "A 3 clears when it belongs to a run of three touching discs.",
  play: "A player chooses a column and drops the waiting disc.",
  "chance-vs-choice": "Choose a branch, then consider possible next discs.",
  "evaluating-a-board": "Board features combine into one estimate.",
  "survival-vs-score": "Score steps upward as the board rises.",
  "heavy-tails": "Schematic of many shorter games and a few long games.",
  "ranking-siblings": "Seven columns branch from one position; one is selected.",
  "oracles-and-teachers": "Examples cross from a teacher to a public student.",
  "does-more-compute-help": "A search tree grows wider and deeper.",
  "learning-from-play": "A loop connects play, recorded examples, and learning.",
  "vocabulary-game": "A covered disc is cracked, then revealed.",
  "vocabulary-search": "Branches lead from a decision to a search leaf.",
  "vocabulary-learning": "Examples train a model that chooses a move.",
  "vocabulary-evidence": "Candidate and reference are paired on the same games.",
  benchmarking: "A policy works through a shared set of scripted rounds.",
  protocol: "The game sends a position; the policy returns a column.",
};

export function LessonArt({ name, ...props }: ArtProps & { name: string }) {
  if (!DESCRIPTIONS[name]) return null;
  return <svg {...artSvgProps("lesson", DESCRIPTIONS[name], props)} data-lesson={name}><g><Drawing name={name} /></g></svg>;
}
