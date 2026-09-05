import type { AnalyticsEventRow } from "./events.ts";

export const MAX_APP_ANALYTICS_BATCH_EVENTS = 50;
export const MAX_APP_ANALYTICS_REQUEST_BYTES = 65_536;

type AnalyticsProperty = string | number | boolean;

interface AppAnalyticsEvent {
  id: string;
  name: string;
  occurredAt: string;
  screen: string;
  properties: Record<string, AnalyticsProperty>;
}

export interface AppAnalyticsBatch {
  schemaVersion: 1;
  source: {
    application: "drop7-mobile";
    platform: "ios" | "android" | "web" | "unknown";
    appVersion: string;
  };
  events: AppAnalyticsEvent[];
}

export type AppAnalyticsValidation =
  | { ok: true; batch: AppAnalyticsBatch }
  | { ok: false; error: "invalid-analytics-batch"; status: 400 };

export function validateAppAnalyticsBatch(value: unknown): AppAnalyticsValidation {
  if (!isRecord(value) || value.schemaVersion !== 1 || !isRecord(value.source)) {
    return invalidBatch();
  }

  const source = value.source;
  if (
    source.application !== "drop7-mobile" ||
    !isPlatform(source.platform) ||
    !isCleanString(source.appVersion, 1, 64)
  ) {
    return invalidBatch();
  }

  if (
    !Array.isArray(value.events) ||
    value.events.length === 0 ||
    value.events.length > MAX_APP_ANALYTICS_BATCH_EVENTS
  ) {
    return invalidBatch();
  }

  const ids = new Set<string>();
  const events: AppAnalyticsEvent[] = [];
  for (const event of value.events) {
    if (!isRecord(event)) return invalidBatch();
    if (
      !isCleanString(event.id, 8, 96) ||
      !/^[A-Za-z0-9._:-]+$/.test(event.id) ||
      ids.has(event.id) ||
      !isCleanString(event.name, 1, 48) ||
      !/^[a-z][a-z0-9_]*$/.test(event.name) ||
      !isIsoTimestamp(event.occurredAt) ||
      !isCleanString(event.screen, 1, 64) ||
      !/^[a-z][a-z0-9_/-]*$/.test(event.screen)
    ) {
      return invalidBatch();
    }

    const properties = validateProperties(event.properties);
    if (properties === null) return invalidBatch();
    ids.add(event.id);
    events.push({
      id: event.id,
      name: event.name,
      occurredAt: event.occurredAt,
      screen: event.screen,
      properties,
    });
  }

  return {
    ok: true,
    batch: {
      schemaVersion: 1,
      source: {
        application: "drop7-mobile",
        platform: source.platform,
        appVersion: source.appVersion,
      },
      events,
    },
  };
}

export function buildAppAnalyticsRows(
  request: Request,
  batch: AppAnalyticsBatch,
  receivedAt = new Date(),
): AnalyticsEventRow[] {
  const url = new URL(request.url);
  const receivedAtIso = receivedAt.toISOString();
  const receivedAtMs = receivedAt.getTime();
  const userAgent = header(request, "user-agent", 768);
  const acceptLanguage = header(request, "accept-language", 256);
  const countryCode = header(request, "cloudfront-viewer-country", 8);
  const regionCode = header(request, "cloudfront-viewer-country-region", 32);
  const city = decodeHeader(header(request, "cloudfront-viewer-city", 128));
  const stage = clean(process.env.DROP7_STAGE ?? "local", 32);

  return batch.events.map((event) => {
    const occurredAtMs = Date.parse(event.occurredAt);
    return {
      event_id: event.id,
      event_name: event.name,
      schema_version: 1,
      occurred_at: event.occurredAt,
      occurred_at_ms: occurredAtMs,
      received_at: receivedAtIso,
      received_at_ms: receivedAtMs,
      path: "",
      host: clean(url.hostname, 255),
      referrer_host: "",
      referrer_channel: "app",
      user_agent: userAgent,
      accept_language: acceptLanguage,
      country_code: countryCode,
      region_code: regionCode,
      city,
      device_type:
        batch.source.platform === "ios" || batch.source.platform === "android"
          ? "mobile"
          : batch.source.platform,
      browser_family: "",
      os_family: platformOs(batch.source.platform),
      is_bot: false,
      visitor_id: "",
      stage,
      source_application: batch.source.application,
      source_platform: batch.source.platform,
      app_version: batch.source.appVersion,
      screen: event.screen,
      properties_json: JSON.stringify(event.properties),
    };
  });
}

function validateProperties(value: unknown): Record<string, AnalyticsProperty> | null {
  if (value === undefined) return {};
  if (!isRecord(value) || Object.keys(value).length > 16) return null;

  const result: Record<string, AnalyticsProperty> = {};
  for (const [key, property] of Object.entries(value)) {
    if (!/^[a-z][a-z0-9_]{0,39}$/.test(key)) return null;
    if (typeof property === "string") {
      if (!isCleanString(property, 0, 160)) return null;
      result[key] = property;
      continue;
    }
    if (typeof property === "boolean") {
      result[key] = property;
      continue;
    }
    if (typeof property === "number" && Number.isFinite(property)) {
      result[key] = property;
      continue;
    }
    return null;
  }
  return result;
}

function invalidBatch(): AppAnalyticsValidation {
  return { ok: false, error: "invalid-analytics-batch", status: 400 };
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === "object" && value !== null && !Array.isArray(value);
}

function isCleanString(value: unknown, minimum: number, maximum: number): value is string {
  return (
    typeof value === "string" &&
    value.length >= minimum &&
    value.length <= maximum &&
    !/[\u0000-\u001f\u007f]/.test(value)
  );
}

function isIsoTimestamp(value: unknown): value is string {
  if (typeof value !== "string") return false;
  const milliseconds = Date.parse(value);
  return Number.isFinite(milliseconds) && new Date(milliseconds).toISOString() === value;
}

function isPlatform(value: unknown): value is AppAnalyticsBatch["source"]["platform"] {
  return value === "ios" || value === "android" || value === "web" || value === "unknown";
}

function platformOs(platform: AppAnalyticsBatch["source"]["platform"]): string {
  if (platform === "ios") return "iOS";
  if (platform === "android") return "Android";
  if (platform === "web") return "Web";
  return "Unknown";
}

function header(request: Request, name: string, maximum: number): string {
  return clean(request.headers.get(name), maximum);
}

function clean(value: string | null | undefined, maximum: number): string {
  return (value ?? "").replace(/[\u0000-\u001f\u007f]/g, " ").slice(0, maximum);
}

function decodeHeader(value: string): string {
  if (!value) return "";
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
}
