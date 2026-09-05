# Choice and chance lesson

The lesson follows one retained teaching position through its first drop,
all seven first moves, and the best replies to each possible next disc.
`ChoiceLesson` is the server adapter registered in MDX. Each `view` sends
only its required presentation data to the interactive figure.

## Editorial and mechanics review

- Introduce falling, matching, clears, and gravity before naming expectimax.
  The opening animation shows the mechanism next to short explanations.
- The old small-board renderer had no grid lines and showed only final
  positions. All instructional boards now use `Drop7Board`, explicit grid
  rules, numbered columns, a marked destination, and engine snapshots.
- The starting board sits above a tree of seven alternatives. Wrapped rows
  connect back to the same root, not to the preceding moves. The generic
  `ChoiceTree` component can hold other board states or diagrams.
- The waiting disc uses exactly the same cell geometry and number size as
  the board. Incoming travel includes the board frame's spacing; within-board
  gravity uses the grid pitch. No lesson-specific pixel corrections are used.
- Column 6's 1 and 4 match on the same pre-clear board. Both clear in wave 1.
  The 2 clears after gravity in wave 2. The animation groups the engine's
  presentation burst snapshots by wave instead of implying separate waves.
- The board is a constructed legal teaching position. It is not a sampled
  research game. The page states its origin and keeps commands in the
  provenance disclosure.
- The first move and the next reply are two moves. The lesson now defines
  depth by the number of player moves examined, avoiding the earlier switch
  between “one move ahead” and a two-move value.
- Equal weighting is tied to the seven next numbered discs in Hardcore.
  The next-disc selector explores possibilities and never suggests that a
  reader can influence the deal. There are no hidden reveals in these clips.
- Immediate points, the average best reply, and their sum have distinct
  labels. Ties are explicit. A fractional expected score is described as an
  average, and no claim promises a higher score on every play.
- Removed broad conclusions about optimistic/pessimistic policies and the
  reference policy's performance from this introductory lesson. The lesson
  makes claims only about its exact two-move example, and links to the
  expectimax primer for further reading.
- Replaced the two large research-oriented tree explorers with a bounded
  choice/chance sequence and a short comprehension question. Interactive
  controls use native buttons with selection states and keyboard focus.
- The next-lesson link now matches the actual next page in reading order,
  `evaluating-a-board`, rather than the later sibling-ranking lesson.

## Verification

The generator's `--check` mode compares all seven first drops and 49 retained
best replies to the engine and original scenario. The animation tests check
simultaneous matching, full point accounting, stable final boards, duplicate
disc identity during gravity, and all four move rankings.

Browser QA covers replay, pause, stepping, offscreen suspension, all next-disc
selections, ranking controls, depth selection, quiz feedback, and responsive
layout. The shared kit provides reduced-motion and server-rendered stills.
Geometry checks compare each waiting disc with its landed version, including
width, height, font size, and horizontal alignment at desktop and phone widths.
See `../board-animation/README.md` for the component contracts.
