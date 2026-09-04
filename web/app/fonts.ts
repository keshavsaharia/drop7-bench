/**
 * The three faces of the console, vendored under web/app/fonts/ and served by
 * next/font/local so the site never fetches a font at build or run time.
 * Each loader only sets a CSS variable (`--font-*-src`); the usable stacks
 * (`--font-display`, `--font-sans`, `--font-mono`) are declared in
 * globals.css and fall through to the system faces when a variable is unset.
 *
 * Licences: OFL-*.txt beside the files.
 */
import localFont from "next/font/local";

export const display = localFont({
  src: "./fonts/SchibstedGrotesk-latin.woff2",
  weight: "400 900",
  variable: "--font-display-src",
  display: "swap",
  adjustFontFallback: "Arial",
});

export const sans = localFont({
  src: "./fonts/InterVariable-latin.woff2",
  weight: "100 900",
  variable: "--font-sans-src",
  display: "swap",
  adjustFontFallback: "Arial",
});

export const mono = localFont({
  src: "./fonts/JetBrainsMono-latin.woff2",
  weight: "100 800",
  variable: "--font-mono-src",
  display: "swap",
  adjustFontFallback: false,
});
