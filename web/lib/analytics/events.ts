import { createHmac, randomUUID } from "node:crypto";

export interface AnalyticsEventRow {
  event_id: string;
  event_name: string;
  schema_version: number;
  occurred_at: string;
  occurred_at_ms: number;
  received_at: string;
  received_at_ms: number;
  path: string;
  host: string;
  referrer_host: string;
  referrer_channel: string;
  user_agent: string;
  accept_language: string;
  country_code: string;
  region_code: string;
  city: string;
  device_type: string;
  browser_family: string;
  os_family: string;
  is_bot: boolean;
  visitor_id: string;
  stage: string;
  source_application: string;
  source_platform: string;
  app_version: string;
  screen: string;
  properties_json: string;
}

export interface PageViewEvent extends AnalyticsEventRow {
  event_name: "page_view";
}

const BOT_PATTERN =
  /bot|crawler|spider|slurp|bingpreview|facebookexternalhit|headlesschrome|lighthouse|pagespeed|uptimerobot|curl|wget/i;

export function buildPageViewEvent(
  request: Request,
  visitorHashKey: string,
  now = new Date(),
  pathOverride?: string,
): PageViewEvent {
  const url = new URL(request.url);
  const userAgent = header(request, "user-agent", 768);
  const referrerHost = parseReferrerHost(
    request.headers.get("referer") ?? request.headers.get("referrer"),
  );
  const isBot = BOT_PATTERN.test(userAgent);

  return {
    event_id: randomUUID(),
    event_name: "page_view",
    schema_version: 1,
    occurred_at: now.toISOString(),
    occurred_at_ms: now.getTime(),
    received_at: now.toISOString(),
    received_at_ms: now.getTime(),
    path: normalizePath(pathOverride ?? url.pathname),
    host: clean(url.hostname, 255),
    referrer_host: referrerHost,
    referrer_channel: classifyReferrer(referrerHost, url.hostname),
    user_agent: userAgent,
    accept_language: header(request, "accept-language", 256),
    country_code: header(request, "cloudfront-viewer-country", 8),
    region_code: header(request, "cloudfront-viewer-country-region", 32),
    city: decodeHeader(header(request, "cloudfront-viewer-city", 128)),
    device_type: classifyDevice(userAgent, isBot),
    browser_family: classifyBrowser(userAgent, isBot),
    os_family: classifyOs(userAgent),
    is_bot: isBot,
    visitor_id: pseudonymousVisitorId(request, visitorHashKey, userAgent),
    stage: clean(process.env.DROP7_STAGE ?? "local", 32),
    source_application: "drop7-web",
    source_platform: "web",
    app_version: "",
    screen: normalizePath(pathOverride ?? url.pathname),
    properties_json: "{}",
  };
}

function pseudonymousVisitorId(
  request: Request,
  visitorHashKey: string,
  userAgent: string,
): string {
  const address = clientAddress(request);
  return createHmac("sha256", visitorHashKey)
    .update(`${address}\n${userAgent}`)
    .digest("hex")
    .slice(0, 32);
}

function clientAddress(request: Request): string {
  const edgeAddress = request.headers.get("x-drop7-viewer-address");
  if (edgeAddress) return clean(edgeAddress, 128);

  const cloudfrontAddress = request.headers.get("cloudfront-viewer-address");
  if (cloudfrontAddress) return clean(cloudfrontAddress, 128);

  const forwardedFor = request.headers.get("x-forwarded-for");
  if (forwardedFor) return clean(forwardedFor.split(",", 1)[0]?.trim(), 128);

  return "unknown";
}

function parseReferrerHost(value: string | null): string {
  if (!value) return "";
  try {
    return clean(new URL(value).hostname.toLowerCase(), 255);
  } catch {
    return "";
  }
}

function classifyReferrer(referrerHost: string, siteHost: string): string {
  if (!referrerHost) return "direct";
  if (referrerHost === siteHost || referrerHost.endsWith(`.${siteHost}`)) {
    return "internal";
  }
  if (
    /(^|\.)(google|bing|duckduckgo|yahoo|yandex|baidu)\./.test(referrerHost)
  ) {
    return "search";
  }
  if (
    /(^|\.)(github|reddit|twitter|x|linkedin|facebook|instagram|threads|bsky)\./.test(
      referrerHost,
    )
  ) {
    return "social";
  }
  return "referral";
}

function classifyDevice(userAgent: string, isBot: boolean): string {
  if (isBot) return "bot";
  if (/ipad|tablet|kindle|silk/i.test(userAgent)) return "tablet";
  if (/mobile|iphone|ipod|android/i.test(userAgent)) return "mobile";
  return userAgent ? "desktop" : "unknown";
}

function classifyBrowser(userAgent: string, isBot: boolean): string {
  if (isBot) return "Bot";
  if (/Edg\//.test(userAgent)) return "Edge";
  if (/OPR\//.test(userAgent)) return "Opera";
  if (/Firefox\//.test(userAgent)) return "Firefox";
  if (/CriOS\//.test(userAgent)) return "Chrome iOS";
  if (/Chrome\//.test(userAgent)) return "Chrome";
  if (/FxiOS\//.test(userAgent)) return "Firefox iOS";
  if (/Safari\//.test(userAgent) && /Version\//.test(userAgent)) return "Safari";
  return userAgent ? "Other" : "Unknown";
}

function classifyOs(userAgent: string): string {
  if (/Windows NT/i.test(userAgent)) return "Windows";
  if (/Android/i.test(userAgent)) return "Android";
  if (/iPhone|iPad|iPod/i.test(userAgent)) return "iOS";
  if (/Mac OS X|Macintosh/i.test(userAgent)) return "macOS";
  if (/CrOS/i.test(userAgent)) return "ChromeOS";
  if (/Linux/i.test(userAgent)) return "Linux";
  return userAgent ? "Other" : "Unknown";
}

function normalizePath(path: string): string {
  const cleaned = clean(path, 1024) || "/";
  return cleaned.length > 1 ? cleaned.replace(/\/+$/, "") : cleaned;
}

function header(request: Request, name: string, maxLength: number): string {
  return clean(request.headers.get(name), maxLength);
}

function clean(value: string | null | undefined, maxLength: number): string {
  return (value ?? "").replace(/[\u0000-\u001f\u007f]/g, " ").slice(0, maxLength);
}

function decodeHeader(value: string): string {
  if (!value) return "";
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
}
