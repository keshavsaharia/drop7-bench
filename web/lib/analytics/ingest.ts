import "server-only";

import {
  FirehoseClient,
  PutRecordBatchCommand,
  PutRecordCommand,
  type _Record as FirehoseRecord,
} from "@aws-sdk/client-firehose";
import { loadGithubAuthCredentials } from "@/lib/auth/github-secret";
import { buildPageViewEvent } from "@/lib/analytics/events";
import type { AnalyticsEventRow } from "@/lib/analytics/events";

let firehoseClient: FirehoseClient | undefined;

export async function trackPageView(
  request: Request,
  pathOverride?: string,
): Promise<void> {
  const streamName = process.env.DROP7_ANALYTICS_FIREHOSE_STREAM;
  if (!streamName) return;

  try {
    const { authSecret } = await loadGithubAuthCredentials();
    const event = buildPageViewEvent(request, authSecret, new Date(), pathOverride);
    firehoseClient ??= new FirehoseClient({});
    await firehoseClient.send(
      new PutRecordCommand({
        DeliveryStreamName: streamName,
        Record: {
          Data: Buffer.from(`${JSON.stringify(event)}\n`, "utf8"),
        },
      }),
    );
  } catch (error) {
    // Analytics is deliberately best-effort and may never delay or fail a page view.
    console.warn(
      JSON.stringify({
        event: "analytics_page_view_delivery_failed",
        message: error instanceof Error ? error.message : "unknown error",
      }),
    );
  }
}

/** Deliver one client batch in one Firehose request, retrying only rejected records once. */
export async function deliverAppAnalyticsRows(
  rows: readonly AnalyticsEventRow[],
): Promise<void> {
  const streamName = process.env.DROP7_ANALYTICS_FIREHOSE_STREAM;
  if (!streamName) throw new Error("App analytics delivery is not configured");
  if (rows.length === 0) return;

  firehoseClient ??= new FirehoseClient({});
  const records = rows.map(toFirehoseRecord);
  const first = await firehoseClient.send(
    new PutRecordBatchCommand({
      DeliveryStreamName: streamName,
      Records: records,
    }),
  );
  const failed = failedRecords(records, first.FailedPutCount, first.RequestResponses);
  if (failed.length === 0) return;

  const retry = await firehoseClient.send(
    new PutRecordBatchCommand({
      DeliveryStreamName: streamName,
      Records: failed,
    }),
  );
  if ((retry.FailedPutCount ?? 0) > 0) {
    throw new Error(`Firehose rejected ${retry.FailedPutCount} app analytics records`);
  }
}

function toFirehoseRecord(row: AnalyticsEventRow): FirehoseRecord {
  return { Data: Buffer.from(`${JSON.stringify(row)}\n`, "utf8") };
}

function failedRecords(
  records: FirehoseRecord[],
  failedCount: number | undefined,
  responses: readonly { ErrorCode?: string }[] | undefined,
): FirehoseRecord[] {
  if (!failedCount) return [];
  if (!responses || responses.length !== records.length) return records;
  return records.filter((_, index) => Boolean(responses[index]?.ErrorCode));
}
