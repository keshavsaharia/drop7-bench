# Board animation components

`Drop7Board` owns the board, disc styling, incoming row, grid, column labels
and score overlays. `AnimatedBoard` renders a recorded `BoardClip` on that
board. `Playback` provides one shared clock for the boards and annotations
inside a figure, with pause, next-step and replay controls. It stops timers
outside the viewport and in hidden tabs, and respects reduced motion.

```tsx
<Playback label="Compare the moves" length={Math.max(...clips.map(c => c.frames.length))}>
  {clips.map(clip => <AnimatedBoard key={clip.column} clip={clip} />)}
</Playback>
```

Short clips hold their final state until the longest clip finishes. Components
can use `usePlayback()` to synchronize their own captions, paths, highlights,
or other diagrams. Each clock step lasts `stepMs` (default 1,000 ms); CSS
handles the movement between engine states. Next step pauses playback and
shows a complete frame. Reduced motion uses the same manual controls and
starts with a still board. Totals and explanatory copy remain available.

`BoardClip` / `BoardFrame` in `web/lib/board-animation.ts` are serializable
presentation contracts. Frames contain a board, changed cells, a label,
recorded scores, and signed vertical travel in cells keyed by destination.
Keep rules and score calculations in engine-backed generators under
`web/scripts/`. The viewer must receive recorded data; it must not resolve
gameplay, infer hidden values, or compute research results in the browser.

For a new lesson, use an engine-backed script to record these phases:

- `ready`: original board and incoming disc.
- `drop`: placed board, with travel from the incoming row to the landing cell.
- `match`: the pre-clear board, highlighting all discs matching in one wave.
- `burst`: that same board, wave points, and all bursting cells together.
- `impact`: cleared board and any covered discs that cracked or revealed.
- `settle`: settled board and exact per-disc gravity travel.
- `rise`: shifted board with positive travel of one cell.
- `done`: final board and total score, held long enough to read.

The existing lesson generator deliberately rejects bonuses and hidden reveals
because its source example excludes them. A generator for a broader lesson
must record the appropriate chance realization and bonus events explicitly.
`gravityTravel` matches discs bottom to top within each column, so repeated
numbers retain their identity. Never derive a falling distance from changed
cell indexes alone.

Optional recorded `runs` mark the full matching rows and columns.
`BoardRunGuides` draws those regions using the board's overlay slot, while
the disc highlights identify which members of the run actually clear.

`ChoiceLesson` is the server MDX adapter. It uses the learn-content loader, handles a
missing artifact, and sends only a figure's required clips to its client island.
Its lesson-specific layout and copy live in `components/lessons/`; the reusable
kit contains no reference to a particular column, board or teaching argument.

## Branching choices and board geometry

`ChoiceTree` places a `root` above an array of `{ id, content }` branches.
It accepts any board or diagram, a descriptive `label`, and optional desktop
and compact column counts. A resize observer routes connectors through the
actual gaps between cards. Wrapped rows share an outside trunk back to the
root, so a later row never looks like a continuation of an earlier move.
The underlying ordered list remains readable without JavaScript.

The waiting row and board grid use the same content width, column gaps,
cell padding, square disc face, and container-relative type size in
`Drop7Board.module.css`. Motion uses its `--d7-cell-pitch` rather than an
approximation based on a disc's radius. Entry frames also cross the incoming
row's gap and the frame chrome via `--d7-entry-offset`; gravity within the
board does not. Keep these geometry variables in the board primitive, not
in a lesson or its recorded engine frames.
