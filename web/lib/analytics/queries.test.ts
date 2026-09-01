import assert from "node:assert/strict";
import test from "node:test";
import {
  buildDashboardSql,
  buildIosDashboardSql,
  buildReadOnlyCustomSql,
} from "./queries.ts";

test("dashboard SQL is bounded and can exclude bots", () => {
  process.env.DROP7_ANALYTICS_DATABASE = "drop7_test_analytics";
  process.env.DROP7_ANALYTICS_TABLE = "page_views";
  const sql = buildDashboardSql(
    "24h",
    "humans",
    new Date("2026-08-23T12:00:00.000Z"),
  );

  assert.match(sql, /FROM "drop7_test_analytics"\."page_views"/);
  assert.match(sql, /AND is_bot = false/);
  assert.match(sql, /date_trunc\('hour'/);
  assert.match(sql, /occurred_at_ms >= 1787400000000/);
  assert.match(sql, /occurred_at_ms < 1787486400000/);
});

test("iOS dashboard SQL reads only validated iOS app games", () => {
  process.env.DROP7_ANALYTICS_DATABASE = "drop7_test_analytics";
  process.env.DROP7_GAME_SUBMISSIONS_TABLE = "game_submissions";
  const sql = buildIosDashboardSql(
    "7d",
    new Date("2026-08-23T12:00:00.000Z"),
  );
  assert.match(sql, /FROM "drop7_test_analytics"\."game_submissions"/);
  assert.match(sql, /source_platform = 'ios'/);
  assert.match(sql, /event_name = 'completed_game'/);
  assert.match(sql, /PARTITION BY event_id/);
  assert.match(sql, /WHERE delivery_rank = 1/);
  assert.match(sql, /date_trunc\('day'/);
});

test("custom SQL is wrapped with a row limit", () => {
  assert.equal(
    buildReadOnlyCustomSql("SELECT path, count(*) AS views FROM page_views GROUP BY 1"),
    "SELECT * FROM (SELECT path, count(*) AS views FROM page_views GROUP BY 1) AS admin_query LIMIT 500",
  );
});

test("custom SQL rejects mutations and multiple statements", () => {
  assert.throws(() => buildReadOnlyCustomSql("DELETE FROM page_views"), /Only SELECT/);
  assert.throws(
    () => buildReadOnlyCustomSql("SELECT * FROM page_views; DROP TABLE page_views"),
    /without a semicolon/,
  );
  assert.throws(
    () => buildReadOnlyCustomSql("WITH changed AS (UPDATE page_views SET path = '/') SELECT 1"),
    /read-only/,
  );
});
