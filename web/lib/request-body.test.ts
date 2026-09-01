import assert from "node:assert/strict";
import test from "node:test";
import { readLimitedJson } from "./request-body.ts";

test("limited JSON accepts a valid body within the byte limit", async () => {
  const result = await readLimitedJson(
    new Request("https://drop7.dev/api/test", { method: "POST", body: '{"ok":true}' }),
    32,
  );
  assert.deepEqual(result, { ok: true, value: { ok: true } });
});

test("limited JSON rejects actual bytes beyond a missing or false length", async () => {
  const request = new Request("https://drop7.dev/api/test", {
    method: "POST",
    headers: { "content-length": "1" },
    body: JSON.stringify({ value: "large" }),
  });
  assert.deepEqual(await readLimitedJson(request, 8), {
    ok: false,
    error: "request-too-large",
    status: 413,
  });
});

test("limited JSON counts UTF-8 bytes and rejects malformed JSON", async () => {
  assert.deepEqual(
    await readLimitedJson(
      new Request("https://drop7.dev/api/test", { method: "POST", body: '"💥"' }),
      5,
    ),
    { ok: false, error: "request-too-large", status: 413 },
  );
  assert.deepEqual(
    await readLimitedJson(
      new Request("https://drop7.dev/api/test", { method: "POST", body: "{" }),
      8,
    ),
    { ok: false, error: "invalid-json", status: 400 },
  );
});
