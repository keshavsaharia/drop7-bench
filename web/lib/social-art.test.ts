import assert from "node:assert/strict";
import test from "node:test";
import { createElement } from "react";
import { socialArtDataUrl, socialArtSvg } from "./social-art.ts";

function SampleArt() {
  return createElement(
    "svg",
    { className: "tart tart--sample", viewBox: "0 0 20 10", "data-mode": "static" },
    createElement(
      "g",
      { className: "tart-final", "data-anim": "finish" },
      createElement("circle", { cx: 5, cy: 5, r: 4, fill: "var(--color-disc-4)" }),
      createElement("text", { x: 10, y: 7, fontFamily: "var(--font-mono)", fill: "var(--color-ink)" }, "done"),
    ),
  );
}

test("serializes a component's final SVG frame with resolved design tokens", () => {
  const svg = socialArtSvg(createElement(SampleArt));
  assert.match(svg, /^<svg xmlns="http:\/\/www\.w3\.org\/2000\/svg" viewBox="0 0 20 10">/);
  assert.match(svg, /fill="#c4443e"/);
  assert.match(svg, /font-family="Courier New, monospace"/);
  assert.match(svg, />done<\/text>/);
  assert.doesNotMatch(svg, /class=|data-anim|data-mode|var\(--/);
});

test("encodes the final SVG as a next/og-compatible data URL", () => {
  const url = socialArtDataUrl(createElement(SampleArt));
  assert.match(url, /^data:image\/svg\+xml;base64,/);
  assert.match(Buffer.from(url.slice(url.indexOf(",") + 1), "base64").toString("utf8"), /<circle[^>]+fill="#c4443e"/);
});
