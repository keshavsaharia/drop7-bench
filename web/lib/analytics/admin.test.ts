import assert from "node:assert/strict";
import test from "node:test";
import { isAnalyticsAdmin } from "./admin.ts";

test("analytics access requires the configured GitHub handle", () => {
  assert.equal(
    isAnalyticsAdmin({ provider: "github", handle: "KeshavSaharia" }, "keshavsaharia"),
    true,
  );
  assert.equal(
    isAnalyticsAdmin({ provider: "github", handle: "someone-else" }, "keshavsaharia"),
    false,
  );
  assert.equal(
    isAnalyticsAdmin({ provider: "google", handle: "keshavsaharia" }, "keshavsaharia"),
    false,
  );
  assert.equal(isAnalyticsAdmin({ provider: "github", handle: "owner" }, ""), false);
});
