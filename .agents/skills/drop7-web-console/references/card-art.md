# Card art: drawing one idea as an animated SVG

Every approach, technique and engine card on the console carries a small
animated drawing above its title. The drawing is the reader's first idea of
what the page is about, so it should show the mechanism the page describes
and nothing else. This file is the contract every art follows.

An art is a **server component returning one inline SVG**. There is no
three.js, no canvas, no client JavaScript and no image file. Motion is CSS
keyframes on `transform` and `opacity`.

## Where the files go

| What | Where |
| --- | --- |
| Art shared by every page using a technique | `web/components/technique-art/<Name>Art.tsx` |
| Art for one approach directory | `web/components/technique-art/approach/<Name>Art.tsx` |
| The art's keyframes | the same directory, `<slug>.css` |
| Registration | `registry.ts` in the same directory |

An approach art is keyed `"<family>/<slug>"`, matching the directory under
`approaches/`. A page with no entry falls back to its technique's art, which
is a correct default and not a placeholder: write an approach art when the
page has a mechanism of its own to draw, not to fill a gap.

## The component

```tsx
/**
 * Card art for `<family>/<slug>`: one sentence saying what is drawn and what
 * the play shows.
 */
import type { ArtProps } from "../registry";
import { ART_MONO, artSvgProps } from "../FallbackArt";
import "./<slug>.css";

export function SomethingArt(props: ArtProps) {
  return (
    <svg {...artSvgProps("approach-<slug>", "A plain description of the drawing", props)}>
      ...
    </svg>
  );
}
```

`artSvgProps(name, label, props)` supplies the class (`tart tart--<name>`),
the `0 0 320 180` viewBox, `role="img"`, the `aria-label` and `data-mode`.
Technique arts pass their technique slug as the name; approach arts pass
`approach-<slug>`.

Rules for the markup:

- **The SVG's own attributes are the resting frame.** What a reader sees with
  animation off must be the end of the cycle, not the start. Elements that
  exist only at the end go inside `<g className="tart-final">`, which is
  hidden while animating and shown at rest.
- **Every animated element carries `data-anim="<name>"`.** The shared
  contract in `art.css` pauses each one on its first keyframe until the card
  is hovered, focused, or scrolled into an index grid.
- **Colours are tokens.** `var(--color-ink-2)`, `var(--color-accent)`,
  `var(--color-disc-4)`, and so on. A literal hex anywhere outside the
  `@theme static` block in `globals.css` fails the design check.
- **Text is `ART_MONO` and sparse.** At most three or four short labels, 9 to
  12px, in `--color-ink-2` or `--color-ink-3`. A label names a part of the
  mechanism ("teacher", "rise", "depth 4"); it is not a caption explaining
  the picture.
- **Numbers.** Game mechanics are welcome: disc values 1 to 7, column counts,
  wave scores from the engine's `scoreForWave`. A research measurement is
  not: never draw a score, a margin, a win rate or a generation count that
  would read as a recorded figure.

## The stylesheet

```css
/* <family>/<slug> card art: one line on what the play shows. */
.tart--approach-<slug> [data-anim="drop"] {
  animation-name: tart-approach-<slug>-drop;
}

@keyframes tart-approach-<slug>-drop {
  0%, 12% { transform: translateY(-40px); opacity: 0; }
  40%, 100% { transform: translateY(0); opacity: 1; }
}
```

- **Set `animation-name` only.** The `animation` shorthand resets play-state
  and fill-mode and would break the pause contract. Duration, easing,
  iteration count, fill mode and delay all come from `art.css`.
- **Prefix every keyframe** `tart-<name>-`; the stylesheets are global and a
  collision silently breaks another art.
- **Animate `transform` and `opacity`.** For a scaled bar, set
  `transform-box: fill-box` and a `transform-origin` on the element.
- To slow an art down, raise `--tart-motion` on its own root, as `dqn.css`
  does: `--tart-motion: calc(var(--duration-art, 2400ms) * 1.4);`. Never set
  `animation-duration` to anything but `var(--tart-duration)`, which is what
  the whole cycle is worth.
- End every keyframe list at `100%` holding the resting frame, so the loop
  reads as one statement repeated rather than a jitter.

### The cycle, and buying time to read

A cycle is `--tart-motion` (the drawing) plus `--tart-read` (the still frame
that follows it). **An art whose last frame puts up a label the first frame
did not have must buy that reading time**, or the words are gone before anyone
has finished them:

```css
.tart.tart--approach-<slug> {
  --tart-read: var(--duration-art-read, 2000ms);
}
```

Keyframes are written against the whole cycle, so a held art has to fit its
drawing inside the motion share and carry its last stop out to `100%`. With
the default 2400ms of motion and 2000ms of reading, that share is 54.55%: a
beat the reader used to see at 70% is now written at 38.18%, and the two
seconds that follow are spent standing still. The drawing keeps exactly the
timing it was authored with; only the pause at the end grows.

The arithmetic is worth doing once and then leaving alone. `check-art.mjs`
holds both halves of it: an art whose `.tart-final` group carries text has to
declare `--tart-read`, and an art that declares it may not have a keyframe
stop past its motion share.

## The board kit

Import from `technique-art/board.tsx` when the idea happens on a Drop7 board.
Drawing the real board is better than drawing an abstraction of it: a reader
who has played the game recognises a chain reaction immediately.

```tsx
import { ArtBoard, ArtCells, ArtDisc, ArtGray, ArtRing, ArtScore, BOARD, cellCenter, columnX } from "../board";
```

| Export | What it draws |
| --- | --- |
| `BOARD` | the default 7x7 geometry, 18px cells at (16, 26) |
| `BOARD_RIGHT` | the same board on the right half, for a two-panel art |
| `ArtBoard` | the frame and grid lines; draw it first |
| `ArtCells` | a whole board from the engine encoding (`0` empty, `1`-`7` a disc, `8` solid gray, `9` cracked) |
| `ArtDisc`, `ArtGray` | one disc, numbered or gray |
| `ArtRing` | the highlight ring around a cell |
| `ArtScore` | the `+7` label the game floats over a clearing disc |
| `cellCenter`, `columnX`, `discRadius` | geometry for anything hand-drawn |

`ArtScore` takes a wave `depth` and prints `scoreForWave(depth)` from the
engine: **+7** for the first wave of a chain, **+39** for the second, **+109**
for the third. Never type those numbers by hand; pass the depth.

Every disc component forwards `data-anim` and `className`, so an art animates
a disc by naming it:

```tsx
<ArtDisc value={4} col={2} row={3} data-anim="lands" />
```

## Composing a play

A card is read in about two seconds, from a thumbnail, usually in a grid of
others. That sets the budget:

- **One idea per art.** If the drawing needs a second sentence to explain it,
  cut something.
- **Three or four beats.** Something arrives, something happens, the outcome
  holds. The last beat should still be on screen at `100%`, and if it is a
  label, the art buys the reading time above.
- **Motion where the meaning is.** A disc that lands, a bar that grows, a
  branch that lights. Decoration that moves competes with the part that
  matters.
- **Legible at 320x180 and at a third of that.** No stroke under 1px, no text
  under 9px, no more than about a dozen moving elements.

## Verifying

From `web/`:

```bash
npx tsc --noEmit                                   # types
npm run lint                                       # React Compiler rules
node scripts/check-tokens.mjs                      # no literal colour outside the token block
node scripts/check-art.mjs                         # the contract above, as far as a script can hold it
npm run build                                      # the page compiles
```

`check-art.mjs` catches the faults that are invisible until someone sits and
watches the card: an art with no stylesheet, fewer `animation-name` rules than
animated elements, a keyframe name that could collide with another art, an
`animation` shorthand that would unpause everything, a resting label with no
time to read it, and a held art still moving after its motion share is up.

Then look at it: `/approaches` renders every card, and a card's own page
shows the art in the technique strip. Check the resting frame too, with
`prefers-reduced-motion` on in the browser's rendering panel; an art whose
resting frame is empty or half-drawn is not finished.
