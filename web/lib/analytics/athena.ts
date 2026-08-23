import "server-only";

import {
  AthenaClient,
  GetQueryExecutionCommand,
  GetQueryResultsCommand,
  StartQueryExecutionCommand,
  StopQueryExecutionCommand,
} from "@aws-sdk/client-athena";
import type { AthenaQueryResult } from "@/lib/analytics/types";

const QUERY_TIMEOUT_MS = 14_000;
const MAX_RESULT_ROWS = 501;
let athenaClient: AthenaClient | undefined;

export async function runAthenaQuery(sql: string): Promise<AthenaQueryResult> {
  const database = requiredEnvironment("DROP7_ANALYTICS_DATABASE");
  const workgroup = requiredEnvironment("DROP7_ANALYTICS_ATHENA_WORKGROUP");
  athenaClient ??= new AthenaClient({});

  const started = await athenaClient.send(
    new StartQueryExecutionCommand({
      QueryString: sql,
      QueryExecutionContext: {
        Catalog: "AwsDataCatalog",
        Database: database,
      },
      WorkGroup: workgroup,
      ResultReuseConfiguration: {
        ResultReuseByAgeConfiguration: {
          Enabled: true,
          MaxAgeInMinutes: 5,
        },
      },
    }),
  );
  const queryExecutionId = started.QueryExecutionId;
  if (!queryExecutionId) throw new Error("Athena did not return a query execution ID.");

  const deadline = Date.now() + QUERY_TIMEOUT_MS;
  let dataScannedBytes = 0;
  let engineExecutionMs = 0;
  while (Date.now() < deadline) {
    const execution = await athenaClient.send(
      new GetQueryExecutionCommand({ QueryExecutionId: queryExecutionId }),
    );
    const state = execution.QueryExecution?.Status?.State;
    dataScannedBytes = execution.QueryExecution?.Statistics?.DataScannedInBytes ?? 0;
    engineExecutionMs =
      execution.QueryExecution?.Statistics?.EngineExecutionTimeInMillis ?? 0;

    if (state === "SUCCEEDED") {
      const result = await readRows(queryExecutionId);
      return {
        ...result,
        queryExecutionId,
        dataScannedBytes,
        engineExecutionMs,
      };
    }
    if (state === "FAILED" || state === "CANCELLED") {
      throw new Error(
        execution.QueryExecution?.Status?.StateChangeReason ??
          `Athena query ${state.toLowerCase()}.`,
      );
    }
    await delay(350);
  }

  await athenaClient.send(
    new StopQueryExecutionCommand({ QueryExecutionId: queryExecutionId }),
  );
  throw new Error("Athena query exceeded the 14-second dashboard timeout.");
}

async function readRows(
  queryExecutionId: string,
): Promise<Pick<AthenaQueryResult, "columns" | "rows">> {
  const columns: string[] = [];
  const rows: Record<string, string | null>[] = [];
  let nextToken: string | undefined;
  let firstPage = true;

  do {
    const page = await athenaClient!.send(
      new GetQueryResultsCommand({
        QueryExecutionId: queryExecutionId,
        MaxResults: MAX_RESULT_ROWS,
        NextToken: nextToken,
      }),
    );
    if (firstPage) {
      for (const column of page.ResultSet?.ResultSetMetadata?.ColumnInfo ?? []) {
        columns.push(column.Name ?? `column_${columns.length + 1}`);
      }
    }
    const pageRows = page.ResultSet?.Rows ?? [];
    for (const row of pageRows.slice(firstPage ? 1 : 0)) {
      if (rows.length >= 500) break;
      const record: Record<string, string | null> = {};
      columns.forEach((column, index) => {
        record[column] = row.Data?.[index]?.VarCharValue ?? null;
      });
      rows.push(record);
    }
    firstPage = false;
    nextToken = rows.length >= 500 ? undefined : page.NextToken;
  } while (nextToken);

  return { columns, rows };
}

function requiredEnvironment(name: string): string {
  const value = process.env[name]?.trim();
  if (!value) throw new Error(`${name} is not configured.`);
  return value;
}

function delay(milliseconds: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}
