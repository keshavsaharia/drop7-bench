#!/usr/bin/env node
/**
 * Card-art contract check.
 *
 * The contract is written out in
 * `.agents/skills/drop7-web-console/references/card-art.md`. The parts of it
 * a machine can hold are checked here, so an art that would render unpaused,
 * collide with another art's keyframes, or leave nothing on screen under
 * reduced motion fails before it ships.
 *
 * For every art component under components/technique-art/:
 *   1. it calls `artSvgProps` (which supplies the class, viewBox, role and
 *      the data-mode the pause contract reads);
 *   2. every `data-anim` name it uses has an `animation-name` rule in a
 *      stylesheet;
 *   3. every keyframe the art's own stylesheet declares is prefixed
 *      `tart-<name>-`, so two arts cannot collide;
 *   4. the stylesheet sets `animation-name`, never the `animation` shorthand,
 *      which would reset the paused play-state;
 *   5. the art has a resting frame: either a `.tart-final` group or markup
 *      outside the animated groups;
 *   6. an art whose resting frame puts up a label buys the time to read it,
 *      by declaring `--tart-read`, and one that has bought that time keeps
 *      its keyframes inside the motion share so the extra time is spent
 *      standing still rather than slowing the drawing down.
 *
 *   node scripts/check-art.mjs
 */
import { readdirSync, readFileSync, statSync } from "node:fs";
import { dirname, join, relative, resolve } from "node:path";
import { fileURLToPath } from "node:url";

const WEB = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const ART_DIR = join(WEB, "components", "technique-art");

/** Files that are the kit rather than an art. */
const NOT_ART = new Set(["registry.ts", "TechniqueArt.tsx", "board.tsx", "FallbackArt.tsx"]);

function* walk(dir) {
  for (const entry of readdirSync(dir)) {
    const path = join(dir, entry);
    if (statSync(path).isDirectory()) yield* walk(path);
    else yield path;
  }
}

const files = [...walk(ART_DIR)];
const css = files.filter((path) => path.endsWith(".css"));
const cssText = new Map(css.map((path) => [path, readFileSync(path, "utf8")]));

const problems = [];
let checked = 0;

for (const path of files) {
  if (!path.endsWith(".tsx")) continue;
  const base = path.slice(path.lastIndexOf("/") + 1);
  if (NOT_ART.has(base)) continue;
  const name = relative(WEB, path).replaceAll("\\", "/");
  const source = readFileSync(path, "utf8");
  const fail = (message) => problems.push(`${name}: ${message}`);
  checked += 1;

  // 1. the shared root props, and the art name they register under.
  // An art may build its own <svg> instead, as long as it carries both the
  // `tart--<name>` class and the `data-mode` the pause contract reads.
  const call = /artSvgProps\(\s*["']([a-z0-9-]+)["']/.exec(source);
  const artName = call?.[1] ?? /tart--([a-z0-9-]+)/.exec(source)?.[1];
  if (!call && !(artName && /data-mode/.test(source))) {
    fail("no artSvgProps call, and no `tart--<name>` class with a data-mode; the pause contract cannot reach it");
    continue;
  }
  // 2. the art has a stylesheet, and it binds an animation for every animated
  // element. An art may bind by `[data-anim="x"]` or by the element's own
  // class, so this counts the bindings rather than matching them one by one.
  const anims = new Set([...source.matchAll(/data-anim=["']([a-z0-9-]+)["']/g)].map((m) => m[1]));
  if (anims.size === 0 && !/data-anim=\{/.test(source)) fail("nothing is animated: no data-anim element");
  const own = [...cssText.entries()].filter(([, text]) => text.includes(`.tart--${artName} `) || text.includes(`.tart--${artName}[`) || text.includes(`.tart--${artName})`));
  if (anims.size > 0 && own.length === 0) {
    fail(`no stylesheet targets .tart--${artName}, so nothing it marks data-anim will move`);
  } else {
    const bound = new Set(own.flatMap(([, text]) => [...text.matchAll(/animation-name:\s*([\w-]+)/g)].map((m) => m[1])));
    if (bound.size < anims.size) {
      fail(`${anims.size} animated elements but only ${bound.size} animation-name rule(s) in its stylesheet`);
    }
  }

  // 5. a resting frame
  if (!source.includes("tart-final") && anims.size > 0) {
    // Acceptable when the SVG's own markup already is the resting frame, which
    // is the case whenever something is drawn outside an animated group.
    const drawn = source.split(/data-anim=/)[0];
    if (!/<(rect|circle|path|line|g|text|Art)/.test(drawn)) {
      fail("no .tart-final group and nothing drawn outside the animated elements: reduced motion would show an empty card");
    }
  }
}

/** The share of a held art's cycle its drawing is allowed to occupy. */
function motionShare(text) {
  const multiple = /--tart-motion:\s*calc\(var\(--duration-art[^)]*\)\s*\*\s*([\d.]+)\)/.exec(text);
  const motion = 2400 * Number(multiple?.[1] ?? 1);
  return (motion / (motion + 2000)) * 100;
}

// 6a: an art that rests on a label it did not start with holds that frame.
for (const path of files) {
  if (!path.endsWith(".tsx")) continue;
  const base = path.slice(path.lastIndexOf("/") + 1);
  if (NOT_ART.has(base)) continue;
  const source = readFileSync(path, "utf8");
  const name = relative(WEB, path).replaceAll("\\", "/");
  const rest = source.slice(source.indexOf('className="tart-final"'));
  if (!source.includes('className="tart-final"')) continue;
  if (!/<text|<ArtScore|<ArtDisc|<ArtCells|<ArtGray/.test(rest)) continue;
  const sheet = /import\s+"\.\/([a-z0-9-]+\.css)"/.exec(source)?.[1];
  const text = sheet ? cssText.get(join(dirname(path), sheet)) : undefined;
  if (text !== undefined && !text.includes("--tart-read")) {
    problems.push(`${name}: the resting frame puts up a label, so ${sheet} should set --tart-read and fit its keyframes inside the motion share`);
  }
}

// 3, 4 and 6b: keyframe prefixes, the animation shorthand and the read hold
for (const [path, text] of cssText) {
  const name = relative(WEB, path).replaceAll("\\", "/");
  if (name.endsWith("technique-art/art.css")) continue;
  if (!text.includes("--tart-read")) continue;
  const share = motionShare(text);
  for (const match of text.matchAll(/@keyframes\s+([\w-]+)\s*\{([\s\S]*?)\n\}/g)) {
    const stops = [...match[2].matchAll(/([^{}]+)\{[^{}]*\}/g)]
      .flatMap((block) => block[1].split(",").map((stop) => Number.parseFloat(stop)))
      .filter((stop) => Number.isFinite(stop));
    const late = stops.filter((stop) => stop > share + 0.01 && stop < 100);
    if (late.length > 0) {
      problems.push(`${name}: @keyframes ${match[1]} still moves at ${late.join("%, ")}%, past the ${share.toFixed(2)}% the drawing has before the read hold begins`);
    }
  }
}

// 3 and 4: keyframe prefixes and the animation shorthand, per stylesheet
for (const [path, text] of cssText) {
  const name = relative(WEB, path).replaceAll("\\", "/");
  if (name.endsWith("technique-art/art.css")) continue;
  const artName = name.slice(name.lastIndexOf("/") + 1, -4);
  const prefix = name.includes("/approach/") ? `tart-approach-${artName}-` : `tart-${artName}-`;
  for (const match of text.matchAll(/@keyframes\s+([\w-]+)/g)) {
    if (!match[1].startsWith(prefix)) {
      problems.push(`${name}: @keyframes ${match[1]} should start with ${prefix} so two arts cannot collide`);
    }
  }
  for (const match of text.matchAll(/^\s*animation:\s*(.+)$/gm)) {
    problems.push(`${name}: \`animation: ${match[1].trim()}\` resets the paused play-state; set animation-name only`);
  }
}

if (problems.length > 0) {
  console.error(`check-art: ${problems.length} problem(s) in the card-art contract\n`);
  for (const problem of problems) console.error(`  ${problem}`);
  console.error("\nThe contract is .agents/skills/drop7-web-console/references/card-art.md");
  process.exit(1);
}

console.log(`check-art: ${checked} arts and ${css.length} stylesheets, contract clean`);
