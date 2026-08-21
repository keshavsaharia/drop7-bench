import path from "node:path";
import type { NextConfig } from "next";

/**
 * The console imports the repository's TypeScript engine directly
 * (`src/core/typescript/*`) for the in-browser game and its solver worker.
 * Local and production builds use webpack so those imports may stay outside
 * `web/`, while output tracing stays rooted here for OpenNext packaging.
 */
const webRoot =
  typeof __dirname !== "undefined"
    ? path.resolve(__dirname)
    : path.resolve(process.cwd());

const nextConfig: NextConfig = {
  outputFileTracingRoot: webRoot,
  outputFileTracingIncludes: {
    "/*": [
      "./build/repo/**/*",
      "./content/**/*",
      "./data/**/*",
    ],
  },
};

export default nextConfig;
