export type AnalyticsRange = "24h" | "7d" | "30d" | "90d";
export type AnalyticsAudience = "humans" | "all" | "bots";

export interface AthenaQueryResult {
  columns: string[];
  rows: Record<string, string | null>[];
  queryExecutionId: string;
  dataScannedBytes: number;
  engineExecutionMs: number;
}

export interface DashboardPoint {
  label: string;
  bucket: string;
  views: number;
  visitors: number;
  paths: number;
}

export interface DashboardData {
  summary: DashboardPoint;
  timeSeries: DashboardPoint[];
  breakdowns: Record<string, DashboardPoint[]>;
  query: {
    queryExecutionId: string;
    dataScannedBytes: number;
    engineExecutionMs: number;
  };
}
