import assert from "node:assert/strict";
import test from "node:test";
import { buildNavigationTree, landingPages } from "./navigation.ts";
import {
  buildNavigationSql,
  parseNavigationResult,
} from "./navigation-query.ts";
import { isAnalyticsRange } from "./queries.ts";
import type { AthenaQueryResult } from "./types.ts";

const paths = [
  { pages: ["/", "/learn", "/play"], sessions: 24, endedSessions: 24 },
  { pages: ["/", "/learn", "/research"], sessions: 16, endedSessions: 16 },
  { pages: ["/", "/play"], sessions: 60, endedSessions: 60 },
  { pages: ["/research", "/learn", "/play"], sessions: 50, endedSessions: 50 },
];

test("conditional percentages preserve the entire preceding route", () => {
  const { nodes, edges } = buildNavigationTree(paths, "/", 4, 8);
  const learn = nodes.find(
    (p) => p.pages.join(",") === "/,/learn" && p.kind === "page",
  )!;
  const play = nodes.find(
    (p) => p.pages.join(",") === "/,/learn,/play" && p.kind === "page",
  )!;
  assert.equal(learn.sessions, 40);
  assert.equal(edges.find((e) => e.target === learn.id)!.percentage, 40);
  assert.equal(edges.find((e) => e.target === play.id)!.percentage, 60);
  assert.equal(nodes[0].sessions, 100);
  const all = buildNavigationTree(paths, "", 4, 8);
  assert.equal(all.nodes.filter((p) => p.label === "/learn").length, 2);
});

test("collapsed branches retain the denominator and conserve sessions", () => {
  const { nodes, edges } = buildNavigationTree(paths, "/", 3, 1);
  const others = nodes.find((p) => p.kind === "other")!;
  assert.equal(others.sessions, 40);
  assert.equal(
    edges
      .filter((e) => e.source === nodes[0].id)
      .reduce((sum, e) => sum + e.sessions, 0),
    100,
  );
  assert.equal(edges.find((e) => e.target === others.id)!.percentage, 40);
});

test("repeated pages are distinct steps and missing next pages are explicit", () => {
  const { nodes } = buildNavigationTree(
    [{ pages: ["/", "/", "/play"], sessions: 2, endedSessions: 2 }],
    "/",
    4,
    3,
  );
  assert.equal(nodes.filter((n) => n.label === "/").length, 2);
  assert.equal(nodes.find((n) => n.kind === "end")!.sessions, 2);
  assert.deepEqual(buildNavigationTree([], ""), { nodes: [], edges: [] });
  assert.deepEqual(landingPages(paths), [
    { page: "/", sessions: 100 },
    { page: "/research", sessions: 50 },
  ]);
});

test("page limits bound the rendered tree", () => {
  const graph = buildNavigationTree(paths, "/", 2, 3);
  assert.ok(graph.nodes.every((n) => n.pages.length <= 2));
  assert.equal(graph.nodes.filter((n) => n.kind === "end").length, 0);
});

test("navigation query is bounded, deduplicated, ordered and contains no exported identities", () => {
  process.env.DROP7_ANALYTICS_DATABASE = "drop7_test_analytics";
  process.env.DROP7_ANALYTICS_TABLE = "page_views";
  const sql = buildNavigationSql(
    "24h",
    "humans",
    new Date("2026-08-23T12:00:00Z"),
  );
  assert.match(
    sql,
    /occurred_at_ms >= 1787400000000 AND occurred_at_ms < 1787486400000/,
  );
  assert.match(sql, /PARTITION BY event_id ORDER BY occurred_at_ms, path/);
  assert.match(sql, /occurred_at_ms - previous_ms >= 1800000/);
  assert.match(sql, /ORDER BY occurred_at_ms, event_id/);
  assert.match(sql, /step_number <= 6/);
  assert.match(sql, /LIMIT 400/);
  assert.match(sql, /AND is_bot = false/);
  assert.match(buildNavigationSql("7d", "bots"), /AND is_bot = true/);
  assert.doesNotMatch(buildNavigationSql("7d", "all"), /AND is_bot/);
  assert.equal(isAnalyticsRange("toString"), false);
  assert.equal(isAnalyticsRange("__proto__"), false);
});

test("coverage uses the full session total and malformed counts fail visibly", () => {
  const result: AthenaQueryResult = {
    columns: [],
    queryExecutionId: "fixture",
    dataScannedBytes: 0,
    engineExecutionMs: 0,
    rows: [
      { section: "summary", sessions: "100" },
      {
        section: "path",
        path_json: '["/","/play"]',
        sessions: "60",
        ended_sessions: "60",
      },
    ],
  };
  const parsed = parseNavigationResult(result);
  assert.equal(parsed.totalSessions, 100);
  assert.equal(parsed.representedSessions, 60);
  assert.equal(JSON.stringify(parsed).includes("visitor_id"), false);
  assert.throws(
    () =>
      parseNavigationResult({
        ...result,
        rows: [{ ...result.rows[1], ended_sessions: "61" }],
      }),
    /Unexpected/,
  );
  assert.throws(
    () => parseNavigationResult({ ...result, rows: [result.rows[1]] }),
    /totals/,
  );
});
