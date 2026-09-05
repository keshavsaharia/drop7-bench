/**
 * Colour math for the chart palette tests: sRGB -> linear, WCAG relative
 * luminance and contrast, and sRGB -> OKLab / OKLCH. Ported from the dataviz
 * palette validator so the repository's tests agree with it; no colour-vision
 * simulation here (the validator script does that). Pure; no DOM, no React.
 */

export type Rgb = [number, number, number];

const HEX = /^#?[0-9a-fA-F]{6}$/;

export function isHexColor(value: string): boolean {
  return HEX.test(value.trim());
}

/** "#3987e5" -> [r, g, b] in 0..1 (sRGB, gamma-encoded). */
export function hexToRgb(hex: string): Rgb {
  const h = hex.trim().replace(/^#/, "");
  if (!/^[0-9a-fA-F]{6}$/.test(h)) throw new Error(`not a 6-digit hex colour: ${hex}`);
  return [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16) / 255) as Rgb;
}

/** sRGB channel -> linear light. */
export function linearize(c: number): number {
  return c <= 0.04045 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4;
}

export function linearRgb(hex: string): Rgb {
  return hexToRgb(hex).map(linearize) as Rgb;
}

/** WCAG 2 relative luminance. */
export function relativeLuminance(hex: string): number {
  const [r, g, b] = linearRgb(hex);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

/** WCAG 2 contrast ratio, order-independent. */
export function contrastRatio(a: string, b: string): number {
  const [hi, lo] = [relativeLuminance(a), relativeLuminance(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
}

/** OKLab [L, a, b] from a hex colour (Björn Ottosson's matrices, as in the validator). */
export function oklab(hex: string): [number, number, number] {
  const [r, g, b] = linearRgb(hex);
  const l = Math.cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b);
  const m = Math.cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b);
  const s = Math.cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b);
  return [
    0.2104542553 * l + 0.793617785 * m - 0.0040720468 * s,
    1.9779984951 * l - 2.428592205 * m + 0.4505937099 * s,
    0.0259040371 * l + 0.7827717662 * m - 0.808675766 * s,
  ];
}

/** OKLCH: lightness 0..1, chroma, hue in degrees 0..360. */
export function oklch(hex: string): { l: number; c: number; h: number } {
  const [l, a, b] = oklab(hex);
  const h = ((Math.atan2(b, a) * 180) / Math.PI + 360) % 360;
  return { l, c: Math.hypot(a, b), h };
}

/** Euclidean OKLab distance x100, the validator's Delta E unit (normal vision). */
export function deltaE(a: string, b: string): number {
  const [l1, a1, b1] = oklab(a);
  const [l2, a2, b2] = oklab(b);
  return Math.hypot(l1 - l2, a1 - a2, b1 - b2) * 100;
}
