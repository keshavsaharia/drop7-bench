import assert from "node:assert/strict";
import test from "node:test";
import { buildPageViewEvent } from "./events.ts";

test("buildPageViewEvent keeps aggregate fields and omits raw addresses and queries", () => {
  const request = new Request("https://drop7.dev/learn/concepts/?secret=hidden", {
    headers: {
      "user-agent":
        "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 Version/18.0 Mobile/15E148 Safari/604.1",
      referer: "https://www.google.com/search?q=drop7+private",
      "x-forwarded-for": "203.0.113.10, 10.0.0.1",
      "accept-language": "en-US,en;q=0.9",
      "cloudfront-viewer-country": "US",
      "cloudfront-viewer-city": "San%20Francisco",
    },
  });

  const event = buildPageViewEvent(
    request,
    "test-only-key",
    new Date("2026-08-23T12:34:56.000Z"),
  );

  assert.equal(event.path, "/learn/concepts");
  assert.equal(event.referrer_host, "www.google.com");
  assert.equal(event.referrer_channel, "search");
  assert.equal(event.device_type, "mobile");
  assert.equal(event.browser_family, "Safari");
  assert.equal(event.os_family, "iOS");
  assert.equal(event.city, "San Francisco");
  assert.equal(event.occurred_at_ms, new Date("2026-08-23T12:34:56.000Z").getTime());
  assert.match(event.visitor_id, /^[a-f0-9]{32}$/);
  assert.ok(!JSON.stringify(event).includes("203.0.113.10"));
  assert.ok(!JSON.stringify(event).includes("secret=hidden"));
  assert.ok(!JSON.stringify(event).includes("drop7+private"));
});

test("visitor identifiers are stable for the same request and keyed", () => {
  const request = new Request("https://drop7.dev/", {
    headers: {
      "user-agent": "example-agent",
      "cloudfront-viewer-address": "[2001:db8::1]:443",
    },
  });
  const now = new Date("2026-08-23T00:00:00Z");

  const first = buildPageViewEvent(request, "key-one", now).visitor_id;
  const second = buildPageViewEvent(request, "key-one", now).visitor_id;
  const otherKey = buildPageViewEvent(request, "key-two", now).visitor_id;

  assert.equal(first, second);
  assert.notEqual(first, otherKey);
});

test("a server-validated client-navigation path can override the collector URL", () => {
  const request = new Request("https://drop7.dev/api/analytics/page-view", {
    headers: { "user-agent": "Mozilla/5.0", "x-forwarded-for": "192.0.2.1" },
  });
  const event = buildPageViewEvent(
    request,
    "visitor-secret",
    new Date("2026-08-23T12:00:00.000Z"),
    "/docs/methodology",
  );

  assert.equal(event.path, "/docs/methodology");
  assert.equal(event.host, "drop7.dev");
});
