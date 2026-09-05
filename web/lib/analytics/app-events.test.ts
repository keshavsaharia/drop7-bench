import assert from "node:assert/strict";
import test from "node:test";
import {
  MAX_APP_ANALYTICS_BATCH_EVENTS,
  buildAppAnalyticsRows,
  validateAppAnalyticsBatch,
} from "./app-events.ts";

function validBatch() {
  return {
    schemaVersion: 1,
    source: {
      application: "drop7-mobile",
      platform: "ios",
      appVersion: "1.2.3",
    },
    events: [
      {
        id: "evt-20260904-0001",
        name: "mode_selected",
        occurredAt: "2026-09-04T12:34:56.000Z",
        screen: "home",
        properties: { mode: "hardcore", enabled: true, position: 2 },
      },
    ],
  };
}

test("app analytics batches are validated and converted to privacy-limited rows", () => {
  const validation = validateAppAnalyticsBatch(validBatch());
  assert.equal(validation.ok, true);
  if (!validation.ok) return;

  const rows = buildAppAnalyticsRows(
    new Request("https://drop7.dev/api/analytics/events?ignored=yes", {
      headers: {
        "user-agent": "Drop7/1.2.3",
        "cloudfront-viewer-country": "US",
        "x-forwarded-for": "203.0.113.42",
      },
    }),
    validation.batch,
    new Date("2026-09-04T12:35:00.000Z"),
  );

  assert.equal(rows.length, 1);
  assert.equal(rows[0]?.event_name, "mode_selected");
  assert.equal(rows[0]?.screen, "home");
  assert.equal(rows[0]?.source_platform, "ios");
  assert.equal(rows[0]?.received_at_ms, Date.parse("2026-09-04T12:35:00.000Z"));
  assert.deepEqual(JSON.parse(rows[0]?.properties_json ?? ""), {
    mode: "hardcore",
    enabled: true,
    position: 2,
  });
  assert.ok(!JSON.stringify(rows).includes("203.0.113.42"));
  assert.equal(rows[0]?.visitor_id, "");
});

test("app analytics rejects malformed, oversized, and duplicate batches", () => {
  assert.equal(validateAppAnalyticsBatch({ ...validBatch(), schemaVersion: 2 }).ok, false);
  assert.equal(
    validateAppAnalyticsBatch({
      ...validBatch(),
      events: Array.from({ length: MAX_APP_ANALYTICS_BATCH_EVENTS + 1 }, (_, index) => ({
        ...validBatch().events[0],
        id: `evt-${index.toString().padStart(8, "0")}`,
      })),
    }).ok,
    false,
  );
  const duplicate = validBatch().events[0];
  assert.equal(
    validateAppAnalyticsBatch({ ...validBatch(), events: [duplicate, duplicate] }).ok,
    false,
  );
  assert.equal(
    validateAppAnalyticsBatch({
      ...validBatch(),
      events: [{ ...duplicate, properties: { private_value: { nested: true } } }],
    }).ok,
    false,
  );
});
