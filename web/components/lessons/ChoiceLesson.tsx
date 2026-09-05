import { loadChoiceLesson } from "@/lib/choice-lesson";
import { ChoiceBoards, ChoiceChance, ChoiceComparison, ChoiceDepth, ChoiceOpening, ChoiceQuiz } from "./ChoiceLessonFigures";
import "./choice-lesson.css";

/** MDX adapter: load teaching artifacts on the server, send each island only what it needs. */
export function ChoiceLesson({ view }: { view: "opening" | "choices" | "chance" | "comparison" | "depth" | "quiz" }) {
  const data = loadChoiceLesson();
  if (!data) return <p>The illustrated position is unavailable in this copy of the site. You can still follow the explanation below.</p>;
  switch (view) {
    case "opening": return <ChoiceOpening clip={data.columns[5].move} />;
    case "choices": return <ChoiceBoards board={data.board} nextDisc={data.nextDisc} clips={data.columns.map((column) => column.move)} />;
    case "chance": return <ChoiceChance columns={data.columns.filter((column) => column.column === 0 || column.column === 5)} />;
    case "comparison": return <ChoiceComparison columns={data.columns.map(({ column, move, replyAverage, fair, optimistic, pessimistic }) => ({ column, points: move.points, replyAverage, fair, optimistic, pessimistic }))} />;
    case "depth": return <ChoiceDepth />;
    case "quiz": return <ChoiceQuiz />;
  }
}
