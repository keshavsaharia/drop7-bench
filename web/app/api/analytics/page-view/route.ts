import { trackPageView } from "@/lib/analytics/ingest";

export const runtime = "nodejs";
export const maxDuration = 10;

interface PageViewBody {
  path?: unknown;
}

export async function POST(request: Request) {
  if (!hasExpectedOrigin(request)) {
    return Response.json({ error: "invalid-origin" }, { status: 403 });
  }

  const contentLength = Number(request.headers.get("content-length") ?? 0);
  if (contentLength > 2_048) {
    return Response.json({ error: "request-too-large" }, { status: 413 });
  }

  let body: PageViewBody;
  try {
    body = (await request.json()) as PageViewBody;
  } catch {
    return Response.json({ error: "invalid-json" }, { status: 400 });
  }

  if (!isPublicPath(body.path)) {
    return Response.json({ error: "invalid-path" }, { status: 400 });
  }

  await trackPageView(request, body.path);
  return new Response(null, { status: 202 });
}

function hasExpectedOrigin(request: Request): boolean {
  const origin = request.headers.get("origin");
  const configuredSite = process.env.DROP7_SITE_URL;
  if (!origin || !configuredSite) return false;

  try {
    return origin === new URL(configuredSite).origin;
  } catch {
    return false;
  }
}

function isPublicPath(path: unknown): path is string {
  return (
    typeof path === "string" &&
    path.length >= 1 &&
    path.length <= 1_024 &&
    path.startsWith("/") &&
    !path.startsWith("//") &&
    !path.includes("?") &&
    !path.includes("#") &&
    path !== "/analytics" &&
    !path.startsWith("/analytics/")
  );
}
