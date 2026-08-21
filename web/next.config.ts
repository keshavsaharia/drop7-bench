import path from "node:path";
import type { NextConfig } from "next";

/**
 * The console imports the repository's TypeScript engine directly
 * (`src/core/typescript/*`) for the in-browser game and its solver worker.
 * Turbopack only resolves files under its root, which it would otherwise pin
 * to `web/` (the nearest lockfile), so the root is widened to the repository.
 */
const repoRoot =
  typeof __dirname !== "undefined"
    ? path.resolve(__dirname, "..")
    : path.resolve(process.cwd(), "..");

const nextConfig: NextConfig = {
  turbopack: {
    root: repoRoot,
  },
};

export default nextConfig;
