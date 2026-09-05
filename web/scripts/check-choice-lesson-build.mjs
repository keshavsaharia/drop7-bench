/** Production smoke test in an isolated checkout with no optional research data.
 * Run after npm run build: node scripts/check-choice-lesson-build.mjs
 */
import assert from "node:assert/strict";
import { cpSync, existsSync, mkdirSync, mkdtempSync, readdirSync, readFileSync, renameSync, rmSync, symlinkSync } from "node:fs";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";
import next from "next";

const web = resolve(dirname(fileURLToPath(import.meta.url)), "..");
assert(existsSync(join(web, ".next", "BUILD_ID")), "Build the web app first");
const fixture = mkdtempSync(join(tmpdir(), "drop7-choice-lesson-"));
const site = join(fixture, "web");
const learn = join(site, "content", "learn");
let app;
let server;
const originalCwd = process.cwd();
const originalRoot = process.env.DROP7_REPO_ROOT;
try {
  mkdirSync(join(learn, "concepts"), { recursive: true });
  mkdirSync(join(fixture, "research", "results"), { recursive: true });
  cpSync(join(web, "content", "learn", "concepts", "chance-vs-choice.mdx"), join(learn, "concepts", "chance-vs-choice.mdx"));
  cpSync(join(web, "content", "learn", "choice-lesson.json"), join(learn, "choice-lesson.json"));
  // Competition manifests and their checked-in round artifacts are required
  // source content, separate from optional web/data replay output.
  const catalog = JSON.parse(readFileSync(join(web, "content", "competition", "catalog.json"), "utf8"));
  cpSync(join(web, "content", "competition"), join(site, "content", "competition"), { recursive: true });
  for (const entry of catalog.games) {
    const manifest = JSON.parse(readFileSync(join(web, "..", entry.manifestPath), "utf8"));
    const source = resolve(web, "..", manifest.artifactPath);
    assert(source.startsWith(`${resolve(web, "..")}\/`));
    const target = join(fixture, manifest.artifactPath);
    mkdirSync(dirname(target), { recursive: true });
    cpSync(source, target);
  }
  symlinkSync(join(web, ".next"), join(site, ".next"), "dir");
  symlinkSync(join(web, "node_modules"), join(site, "node_modules"), "dir");
  assert(!existsSync(join(site, "data")));
  assert(!existsSync(join(site, "content", "log")));
  assert.deepEqual(readdirSync(join(fixture, "research", "results")), []);
  process.chdir(site);
  process.env.DROP7_REPO_ROOT = fixture;
  app = next({ dev: false, dir: site });
  await app.prepare();
  server = createServer(app.getRequestHandler());
  await new Promise((resolve, reject) => { server.once("error", reject); server.listen(0, "127.0.0.1", resolve); });
  const base = `http://127.0.0.1:${server.address().port}`;
  const read = async (path) => {
    const response = await fetch(`${base}${path}`);
    assert.equal(response.status, 200, path);
    return response.text();
  };
  const html = await read("/learn/concepts/chance-vs-choice");
  assert(html.includes("d7-choice-tree"));
  assert(html.includes("data-reduced-motion=\"true\""), "Server output must start with a still frame");
  assert(html.includes("All the values"));
  assert(!html.includes("illustrated position is unavailable"));
  for (const route of ["/learn", "/leaderboard", "/research", "/results", "/log"]) await read(route);
  renameSync(join(learn, "choice-lesson.json"), join(learn, "choice-lesson.unavailable.json"));
  const missing = await read("/learn/concepts/chance-vs-choice");
  assert(missing.includes("illustrated position is unavailable"));
  assert(missing.includes("A better average does not promise"));
  console.log("Production lesson and 5 index routes render without optional data; missing lesson data has a readable fallback.");
} catch (error) {
  console.error(error);
  process.exitCode = 1;
} finally {
  if (server) { server.closeAllConnections(); await new Promise((resolve) => server.close(resolve)); }
  await app?.close();
  process.chdir(originalCwd);
  if (originalRoot === undefined) delete process.env.DROP7_REPO_ROOT;
  else process.env.DROP7_REPO_ROOT = originalRoot;
  // Only this test's newly created, isolated fixture is removed.
  rmSync(fixture, { recursive: true, force: true });
}
