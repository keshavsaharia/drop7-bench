import { existsSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { LEARN_DIR } from "./learn";
import type { ChoiceLessonData } from "./board-animation";

/** Learn content is bundled at content/learn in both local and packaged builds. */
export function loadChoiceLesson(): ChoiceLessonData | null {
  const path = join(LEARN_DIR, "choice-lesson.json");
  if (!existsSync(path)) return null;
  return JSON.parse(readFileSync(path, "utf8")) as ChoiceLessonData;
}
