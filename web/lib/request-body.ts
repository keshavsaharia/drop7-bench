export type JsonBodyResult =
  | { ok: true; value: unknown }
  | { ok: false; error: "invalid-json" | "request-too-large"; status: 400 | 413 };

/** Read JSON while enforcing the real UTF-8 payload size, even without Content-Length. */
export async function readLimitedJson(
  request: Request,
  maximumBytes: number,
): Promise<JsonBodyResult> {
  const declaredLength = Number(request.headers.get("content-length"));
  if (Number.isFinite(declaredLength) && declaredLength > maximumBytes) {
    return { ok: false, error: "request-too-large", status: 413 };
  }

  let text: string;
  try {
    text = await request.text();
  } catch {
    return { ok: false, error: "invalid-json", status: 400 };
  }
  if (new TextEncoder().encode(text).byteLength > maximumBytes) {
    return { ok: false, error: "request-too-large", status: 413 };
  }

  try {
    return { ok: true, value: JSON.parse(text) };
  } catch {
    return { ok: false, error: "invalid-json", status: 400 };
  }
}
