import "server-only";

import { FirehoseClient, PutRecordCommand } from "@aws-sdk/client-firehose";
import type { ValidatedGameSubmission } from "./types.ts";

let firehoseClient: FirehoseClient | undefined;

export async function deliverGameSubmission(
  submission: ValidatedGameSubmission,
): Promise<void> {
  const streamName = process.env.DROP7_GAME_SUBMISSIONS_FIREHOSE_STREAM;
  if (!streamName) throw new Error("Game submission delivery is not configured");
  firehoseClient ??= new FirehoseClient({});
  await firehoseClient.send(
    new PutRecordCommand({
      DeliveryStreamName: streamName,
      Record: { Data: Buffer.from(`${JSON.stringify(submission)}\n`, "utf8") },
    }),
  );
}
