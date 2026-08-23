import "server-only";

import {
  FirehoseClient,
  PutRecordCommand,
} from "@aws-sdk/client-firehose";
import { loadGithubAuthCredentials } from "@/lib/auth/github-secret";
import { buildPageViewEvent } from "@/lib/analytics/events";

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
