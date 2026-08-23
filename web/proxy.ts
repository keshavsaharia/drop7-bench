import type { NextFetchEvent, NextRequest } from "next/server";
import { NextResponse } from "next/server";
import { trackPageView } from "@/lib/analytics/ingest";
import { shouldTrackPageView } from "@/lib/analytics/requests";

export function proxy(request: NextRequest, event: NextFetchEvent) {
  if (shouldTrackPageView(request)) {
    event.waitUntil(trackPageView(request));
  }
  return NextResponse.next();
}

export const config = {
  matcher: [
    "/((?!api|analytics(?:/|$)|_next/static|_next/image|favicon.ico|sitemap.xml|robots.txt|icon.png|apple-icon.png|.*\\.[^/]+$).*)",
  ],
};
