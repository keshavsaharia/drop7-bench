import assert from "node:assert/strict";
import test from "node:test";
import { shouldTrackPageView } from "./requests.ts";

test("real GET navigations are tracked", () => {
  assert.equal(
    shouldTrackPageView(new Request("https://drop7.dev/learn", { method: "GET" })),
    true,
  );
});

test("prefetches and non-GET requests are not tracked", () => {
  assert.equal(
    shouldTrackPageView(
      new Request("https://drop7.dev/learn", {
        headers: { "next-router-prefetch": "1" },
      }),
    ),
    false,
  );
  assert.equal(
    shouldTrackPageView(
      new Request("https://drop7.dev/learn", {
        headers: { purpose: "prefetch" },
      }),
    ),
    false,
  );
  assert.equal(
    shouldTrackPageView(new Request("https://drop7.dev/learn", { method: "POST" })),
    false,
  );
  assert.equal(
    shouldTrackPageView(
      new Request("https://drop7.dev/learn", { headers: { rsc: "1" } }),
    ),
    false,
  );
  assert.equal(
    shouldTrackPageView(
      new Request("https://drop7.dev/learn", {
        headers: { "sec-fetch-dest": "empty" },
      }),
    ),
    false,
  );
});
