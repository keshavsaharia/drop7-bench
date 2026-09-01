import { trackPageView } from "@/lib/analytics/ingest";
import { readLimitedJson } from "@/lib/request-body";

export const runtime = "nodejs";
export const maxDuration = 10;

interface PageViewBody {
  path?: unknown;
}

export async function POST(request: Request) {
  if (!hasExpectedOrigin(request)) {
    return Response.json({ error: "invalid-origin" }, { status: 403 });
  }

  const parsed = await readLimitedJson(request, 2_048);
  if (!parsed.ok) {
    return Response.json({ error: parsed.error }, { status: parsed.status });
  }
  const body = parsed.value as PageViewBody;

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
