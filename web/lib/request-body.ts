export type JsonBodyResult =
  | { ok: true; value: unknown }
  | { ok: false; error: "invalid-json" | "request-too-large"; status: 400 | 413 };

/** Read JSON while enforcing the real UTF-8 payload size, even without Content-Length. */
export async function readLimitedJson(
  request: Request,
  maximumBytes: number,
): Promise<JsonBodyResult> {
  const contentLength = request.headers.get("content-length");
  const declaredLength = contentLength === null ? null : Number(contentLength);
  if (declaredLength !== null && Number.isFinite(declaredLength) && declaredLength > maximumBytes) {
    return { ok: false, error: "request-too-large", status: 413 };
  }

  const body = request.body;
  if (body === null) {
    return { ok: false, error: "invalid-json", status: 400 };
  }

  const reader = body.getReader();
  const decoder = new TextDecoder("utf-8", { fatal: true });
  let bytesRead = 0;
  let text = "";
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;

      bytesRead += value.byteLength;
      if (bytesRead > maximumBytes) {
        try {
          await reader.cancel("request-too-large");
        } catch {
          // The 413 response still takes precedence if the source rejects cancellation.
        }
        return { ok: false, error: "request-too-large", status: 413 };
      }
      text += decoder.decode(value, { stream: true });
    }
    text += decoder.decode();
  } catch {
    try {
      await reader.cancel("invalid-json");
    } catch {
      // Preserve the invalid-body response if the source also rejects cancellation.
    }
    return { ok: false, error: "invalid-json", status: 400 };
  } finally {
    reader.releaseLock();
  }

  try {
    return { ok: true, value: JSON.parse(text) };
  } catch {
    return { ok: false, error: "invalid-json", status: 400 };
  }
}
