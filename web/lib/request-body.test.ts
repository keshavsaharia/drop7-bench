import assert from "node:assert/strict";
import test from "node:test";
import { readLimitedJson } from "./request-body.ts";

const encoder = new TextEncoder();

function streamRequest(
  chunks: Uint8Array[],
  options: {
    headers?: HeadersInit;
    onCancel?: (reason: unknown) => void;
    onPull?: () => void;
  } = {},
): Request {
  let index = 0;
  const body = new ReadableStream<Uint8Array>(
    {
      pull(controller) {
        options.onPull?.();
        if (index === chunks.length) {
          controller.close();
          return;
        }
        controller.enqueue(chunks[index]);
        index += 1;
      },
      cancel(reason) {
        options.onCancel?.(reason);
      },
    },
    { highWaterMark: 0 },
  );
  return new Request("https://drop7.dev/api/test", {
    method: "POST",
    headers: options.headers,
    body,
    duplex: "half",
  } as RequestInit & { duplex: "half" });
}

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

test("limited JSON cancels a chunked body as soon as its byte limit is exceeded", async () => {
  let cancelledWith: unknown;
  let pulls = 0;
  const request = streamRequest(
    [encoder.encode('{"value":'), encoder.encode('"too large"}'), encoder.encode("unread")],
    {
      onCancel: (reason) => {
        cancelledWith = reason;
      },
      onPull: () => {
        pulls += 1;
      },
    },
  );

  assert.deepEqual(await readLimitedJson(request, 12), {
    ok: false,
    error: "request-too-large",
    status: 413,
  });
  assert.equal(cancelledWith, "request-too-large");
  assert.equal(pulls, 2);
});

test("limited JSON preserves multibyte characters split across chunks", async () => {
  const bytes = encoder.encode('{"value":"💥"}');
  const result = await readLimitedJson(
    streamRequest([bytes.slice(0, 11), bytes.slice(11, 13), bytes.slice(13)]),
    bytes.byteLength,
  );
  assert.deepEqual(result, { ok: true, value: { value: "💥" } });
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
