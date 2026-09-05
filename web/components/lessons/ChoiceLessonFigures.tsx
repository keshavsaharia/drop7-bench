"use client";

import { useState } from "react";
import { lessonNumber as fmt, type BoardClip, type ChoiceLessonData } from "@/lib/board-animation";
import { Drop7Board } from "../Drop7Board";
import { DiscFace } from "../discs";
import { AnimatedBoard } from "../board-animation/AnimatedBoard";
import { Playback, usePlayback } from "../board-animation/Playback";
import { ChoiceTree } from "../board-animation/ChoiceTree";

function DiscToken({ value }: { value: number }) {
  return <span className="choice-disc" role="img" aria-label={`disc ${value}`}><DiscFace cell={value} /></span>;
}

function OpeningExplanation({ clip }: { clip: BoardClip }) {
  const { index } = usePlayback();
  const frame = clip.frames[Math.min(index, clip.frames.length - 1)];
  const stage = frame.kind === "ready" || frame.kind === "drop" ? 0 : (frame.depth ?? 0) === 1 ? 1 : 2;
  return (
    <div className="choice-opening-copy">
      <span className="choice-eyebrow">One drop, a chain reaction</span>
      <div className="choice-big-number">+{clip.points}<span>points from this move</span></div>
      <ol className="choice-story">
        <li data-active={stage === 0}><span className="choice-step">1</span><div><strong>Choose a column.</strong><p>The green 1 falls until it reaches the stack in column 6.</p></div></li>
        <li data-active={stage === 1}><span className="choice-step">2</span><div><strong>Look for a match.</strong><p>The 1 is alone in its row. The 4 is in a column of four discs. Both clear in the first wave.</p><span className="choice-wave">2 discs × 7 points = 14</span></div></li>
        <li data-active={stage === 2}><span className="choice-step">3</span><div><strong>Let the board fall.</strong><p>The 2 drops beside the 3. That makes a row of two, so the 2 clears in a second wave.</p><span className="choice-wave">1 disc × 39 points = 39</span></div></li>
      </ol>
    </div>
  );
}

export function ChoiceOpening({ clip }: { clip: BoardClip }) {
  return (
    <figure className="choice-figure choice-opening">
      <Playback label="Watch a move" length={clip.frames.length} stepMs={1200}>
        <div className="choice-opening-layout">
          <AnimatedBoard clip={clip} />
          <OpeningExplanation clip={clip} />
        </div>
      </Playback>
    </figure>
  );
}

export function ChoiceBoards({ board, nextDisc, clips }: { board: string; nextDisc: number; clips: BoardClip[] }) {
  const best = Math.max(...clips.map((clip) => clip.points));
  return (
    <figure className="choice-figure">
      <Playback label="One position · seven choices" length={Math.max(...clips.map((clip) => clip.frames.length))}>
        <ChoiceTree
          label="Choose one of seven columns"
          root={<div className="choice-board-card choice-start-card">
            <div className="choice-card-heading"><span>Starting board</span><span className="choice-muted">{nextDisc} to drop</span></div>
            <Drop7Board cells={board} nextDisc={nextDisc} size="100%" showColumnLabels label="Starting position. A 7 above a 2 above a 4 in column 6, and a 3 at the bottom of column 5." />
            <p className="choice-start-note">One board. Seven possible moves.</p>
          </div>}
          branches={clips.map((clip) => ({
            id: String(clip.column),
            content: <div className="choice-board-card" data-best={clip.points === best}>
              <div className="choice-card-heading"><span>Column {clip.column + 1}</span><span className="choice-card-total">+{clip.points}</span></div>
              <AnimatedBoard clip={clip} />
              <div className="choice-card-foot">{clip.waves.length} {clip.waves.length === 1 ? "wave" : "waves"}{clip.points === best ? " · most points now" : ""}</div>
            </div>,
          }))}
        />
      </Playback>
      <figcaption>Follow any branch from the starting board to try that column. These are seven alternatives, not seven moves in a row. Each replay shows its drop, clears and falls. Column 6 earns the most points on this move.</figcaption>
    </figure>
  );
}

type LessonColumn = ChoiceLessonData["columns"][number];

export function ChoiceChance({ columns }: { columns: LessonColumn[] }) {
  const [disc, setDisc] = useState(2);
  const selected = columns.map((column) => ({ ...column, reply: column.replies.find((reply) => reply.disc === disc)! }));
  return (
    <figure className="choice-figure">
      <div className="choice-selector-title"><span className="choice-eyebrow">The next disc is unknown</span><p>Choose a possibility to see the best reply.</p></div>
      <div className="choice-disc-selector" role="group" aria-label="Possible next discs">
        {[1, 2, 3, 4, 5, 6, 7].map((value) => <button type="button" key={value} aria-pressed={disc === value} aria-label={`Next disc ${value}`} onClick={() => setDisc(value)}><DiscToken value={value} /><span>1/7</span></button>)}
      </div>
      <Playback key={disc} label={`If a ${disc} comes next`} length={Math.max(...selected.map((column) => column.reply.move.frames.length))}>
        <div className="choice-pair">
          {selected.map((column) => <div className="choice-reply-card" key={column.column}>
            <div className="choice-card-heading"><span>After column {column.column + 1}</span><span className="choice-muted">+{column.move.points} already</span></div>
            <AnimatedBoard clip={column.reply.move} />
            <div className="choice-reply-total"><span>Best reply: column {column.reply.move.column + 1}</span><strong>+{column.reply.move.points}</strong></div>
            <div className="choice-reply-strip" aria-label="Best reply points for next discs 1 through 7">{column.replies.map((reply) => <span key={reply.disc} data-selected={reply.disc === disc}><small>{reply.disc}</small>+{reply.move.points}</span>)}</div>
          </div>)}
        </div>
      </Playback>
      <figcaption>Each next disc is equally likely in Hardcore mode. These are the engine’s highest-scoring replies after the first move. Selecting a disc explores a possibility; it does not change the odds.</figcaption>
    </figure>
  );
}

interface ValueColumn { column: number; points: number; replyAverage: number; fair: number; optimistic: number; pessimistic: number }
const ATTITUDES = [
  { key: "points", title: "Now only", detail: "Count only the points from this drop." },
  { key: "optimistic", title: "Hope for the best", detail: "Add the best reply to the luckiest next disc." },
  { key: "pessimistic", title: "Prepare for the worst", detail: "Add the best reply to the least helpful next disc." },
  { key: "fair", title: "Average the possibilities", detail: "Add the average of the best replies to all seven next discs." },
] as const;

export function ChoiceComparison({ columns }: { columns: ValueColumn[] }) {
  const [method, setMethod] = useState<(typeof ATTITUDES)[number]["key"]>("fair");
  const best = Math.max(...columns.map((column) => column[method]));
  const pick = columns.find((column) => column[method] === best)!;
  const maximum = Math.max(...columns.map((column) => column.optimistic));
  const left = columns[0];
  const right = columns[5];
  return (
    <figure className="choice-figure">
      <div className="choice-equations">
        {[left, right].map((column) => <div key={column.column} className="choice-equation" data-best={column === left}>
          <span className="choice-eyebrow">Column {column.column + 1}</span>
          <div><span>{column.points}<small>now</small></span><i>+</i><span>{fmt(column.replyAverage)}<small>average reply</small></span><i>=</i><span>{fmt(column.fair)}<small>two-move value</small></span></div>
        </div>)}
      </div>
      <div className="choice-method-selector" role="group" aria-label="Ways to compare moves">
        {ATTITUDES.map((attitude) => <button type="button" key={attitude.key} aria-pressed={method === attitude.key} onClick={() => setMethod(attitude.key)}>{attitude.title}</button>)}
      </div>
      <p className="choice-method-description">{ATTITUDES.find((attitude) => attitude.key === method)!.detail}</p>
      <div className="choice-value-chart" role="img" aria-label={`${ATTITUDES.find((attitude) => attitude.key === method)!.title}. Column ${pick.column + 1} is the leftmost best move, with a value of ${fmt(best)}.`}>
        {columns.map((column) => <div className="choice-value-row" key={column.column} data-best={column[method] === best}>
          <span>Column {column.column + 1}</span><span className="choice-value-track"><span style={{ width: `${column[method] / maximum * 100}%` }} /></span><span>{fmt(column[method])}</span>
        </div>)}
      </div>
      <p className="choice-chart-result">{method === "fair" ? "Columns 1, 2 and 3 tie. Taking the leftmost tied column gives column 1." : `This comparison chooses column ${pick.column + 1}.`}</p>
      <details className="choice-data"><summary>All the values</summary><div className="choice-table-scroll"><table><thead><tr><th>Column</th>{ATTITUDES.map((attitude) => <th key={attitude.key}>{attitude.title}</th>)}</tr></thead><tbody>{columns.map((column) => <tr key={column.column}><td>{column.column + 1}</td>{ATTITUDES.map((attitude) => <td key={attitude.key}>{fmt(column[attitude.key])}</td>)}</tr>)}</tbody></table></div></details>
      <figcaption>The same engine-generated moves give different rankings when we change how the next disc is treated. Values include only the first move and its best immediate reply. Displayed averages are rounded to one decimal place.</figcaption>
    </figure>
  );
}

function DepthFlow({ depth }: { depth: number }) {
  const { index } = usePlayback();
  return <div className="choice-depth-flow">
    {Array.from({ length: depth }, (_, i) => <div className="choice-depth-ply" key={i}>
      <div className="choice-choice-node" data-active={index === i * 2}><span>Move {i + 1}: you choose</span><div className="choice-seven-slots">{[1, 2, 3, 4, 5, 6, 7].map((column) => <span key={column}>{column}</span>)}</div><small>compare columns</small></div>
      {i < depth - 1 ? <><span className="choice-flow-arrow" aria-hidden="true">↓</span><div className="choice-chance-node" data-active={index === i * 2 + 1}><span>The game deals</span><div className="choice-seven-discs">{[1, 2, 3, 4, 5, 6, 7].map((disc) => <DiscToken key={disc} value={disc} />)}</div><small>average the possibilities</small></div></> : <><span className="choice-flow-arrow" aria-hidden="true">↓</span><div className="choice-stop-node" data-active={index === i * 2 + 1}>Stop and judge the position.</div></>}
    </div>)}
  </div>;
}

export function ChoiceDepth() {
  const [depth, setDepth] = useState(2);
  return <figure className="choice-figure">
    <div className="choice-method-selector" role="group" aria-label="Moves to look ahead">{[1, 2, 3].map((value) => <button key={value} type="button" aria-pressed={depth === value} onClick={() => setDepth(value)}>{value} {value === 1 ? "move" : "moves"} ahead</button>)}</div>
    <Playback key={depth} label="Choice, then chance" length={depth * 2} stepMs={1400}><DepthFlow depth={depth} /></Playback>
    <figcaption>This is a map of the search process. At every choice, compare the legal columns. Between choices, average over the discs the game could deal. A deeper search repeats the process before it stops.</figcaption>
  </figure>;
}

export function ChoiceQuiz() {
  const [answer, setAnswer] = useState<number | null>(null);
  const options = ["Choose the move with the biggest score right now.", "Choose the move with the best average over the next discs.", "Choose the move that wins if the luckiest disc arrives."];
  return <div className="choice-quiz">
    <span className="choice-eyebrow">A quick check</span>
    <p>You can see the current disc, but the next one has not been dealt. How would you compare two moves while accounting for all seven possibilities?</p>
    <div role="group" aria-label="Check your understanding">{options.map((option, index) => <button type="button" key={option} aria-pressed={answer === index} onClick={() => setAnswer(index)}><span>{String.fromCharCode(65 + index)}</span>{option}</button>)}</div>
    <div className="choice-feedback" role="status">{answer === null ? "Choose an answer to check your understanding." : answer === 1 ? "Yes. Find the best reply for each possible next disc, then average those replies. You can choose your column; you cannot choose the next disc." : answer === 0 ? "That finds the biggest immediate score. Column 6 shows why looking at the next disc can change the choice." : "That treats the luckiest disc as certain. Each disc has the same chance, so give each possible reply the same weight."}</div>
  </div>;
}
