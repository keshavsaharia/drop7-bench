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

/**
 * Next.js dev blocks /_next resources for any origin but localhost, which also
 * stops hydration when the console is opened from another machine on the LAN.
 * DROP7_DEV_ORIGINS="10.0.0.104,drop7.local" allows those hosts; unset, nothing
 * changes. Development only; production ignores it.
 */
const allowedDevOrigins = (process.env.DROP7_DEV_ORIGINS ?? "")
  .split(",")
  .map((origin) => origin.trim())
  .filter(Boolean);

const nextConfig: NextConfig = {
  ...(allowedDevOrigins.length ? { allowedDevOrigins } : {}),
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
