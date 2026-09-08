import type { AnalyticsRange } from "./types.ts";

/** Fill empty UTC buckets so the chart cannot draw traffic across a silent day. */
export function fillTimeBuckets<T extends { bucket: string }>(
  points: T[],
  range: AnalyticsRange,
  now: Date,
  empty: (bucket: string) => T,
): T[] {
  const day = 86_400_000;
  const duration = {
    "24h": day,
    "7d": 7 * day,
    "30d": 30 * day,
    "90d": 90 * day,
  }[range];
  const step = range === "24h" ? 3_600_000 : range === "90d" ? 7 * day : day;
  const start = new Date(now.getTime() - duration);
  if (range === "24h") start.setUTCMinutes(0, 0, 0);
  else {
    start.setUTCHours(0, 0, 0, 0);
    if (range === "90d")
      start.setUTCDate(start.getUTCDate() - ((start.getUTCDay() + 6) % 7));
  }
  const byTime = new Map(
    points.map((point) => [
      Date.parse(point.bucket.replace(/(?:Z| UTC)$/, "") + "Z"),
      point,
    ]),
  );
  const filled: T[] = [];
  for (let time = start.getTime(); time < now.getTime(); time += step) {
    const bucket = new Date(time).toISOString();
    const point = byTime.get(time);
    filled.push(point ? { ...point, bucket } : empty(bucket));
  }
  return filled;
}
