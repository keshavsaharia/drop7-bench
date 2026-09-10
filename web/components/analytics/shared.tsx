"use client";

import { useEffect, useState } from "react";
import type { ReactNode } from "react";
import type { DashboardData } from "@/lib/analytics/types";

export async function analyticsQuery<T>(
  body: unknown,
  signal?: AbortSignal,
): Promise<T> {
  const response = await fetch("/api/analytics/query", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify(body),
    signal,
  });
  if (response.status === 401)
    throw new Error("Your session expired. Sign in again to view analytics.");
  if (response.status === 404)
    throw new Error(
      "Analytics access is unavailable for this account. Sign in with the configured admin account.",
    );
  const payload = (await response.json().catch(() => ({}))) as {
    data?: T;
    error?: string;
  };
  if (!response.ok || !payload.data)
    throw new Error(
      payload.error ?? "Analytics could not load. Try refreshing in a moment.",
    );
  return payload.data;
}

/** Abort obsolete requests on filter changes; a slow response must not replace a newer filter. */
export function useAnalyticsQuery<T>(body: Record<string, unknown>) {
  const request = JSON.stringify(body);
  const [state, setState] = useState<{ data: T | null; error: string | null }>({
    data: null,
    error: null,
  });
  useEffect(() => {
    const controller = new AbortController();
    const timer = window.setTimeout(() => {
      void analyticsQuery<T>(JSON.parse(request), controller.signal).then(
        (data) => {
          if (!controller.signal.aborted) setState({ data, error: null });
        },
        (error: unknown) => {
          if (!controller.signal.aborted)
            setState({
              data: null,
              error:
                error instanceof Error
                  ? error.message
                  : "Analytics could not load.",
            });
        },
      );
    }, 0);
    return () => {
      window.clearTimeout(timer);
      controller.abort();
    };
  }, [request]);
  return state;
}

export function PanelHeading({
  title,
  description,
  children,
}: {
  title: string;
  description: string;
  children?: ReactNode;
}) {
  return (
    <div className="analytics-panel-heading">
      <div>
        <h2>{title}</h2>
        <p>{description}</p>
      </div>
      {children}
    </div>
  );
}

export function QueryFootnote({ query }: { query: DashboardData["query"] }) {
  return (
    <details className="analytics-details analytics-query-details">
      <summary>
        Query details{" "}
        <span>
          {(query.dataScannedBytes / 1024 / 1024).toFixed(2)} MiB scanned ·{" "}
          {(query.engineExecutionMs / 1000).toFixed(2)}s
        </span>
      </summary>
      <p>
        Query ID: <code>{query.queryExecutionId}</code>
      </p>
    </details>
  );
}
export function ErrorNotice({ message }: { message: string }) {
  // Auth.js serves a standalone HTML page; this must be a document navigation.
  /* eslint-disable @next/next/no-html-link-for-pages */
  return (
    <div role="alert" className="analytics-error">
      <strong>Analytics could not load</strong>
      <p>{message}</p>
      {/session|account/i.test(message) && (
        <a href="/api/auth/signin?callbackUrl=%2Fanalytics">
          Sign in with GitHub →
        </a>
      )}
      <span>Use Refresh to try again.</span>
    </div>
  );
}
export function EmptyState({ label }: { label: string }) {
  return <p className="analytics-empty">{label}</p>;
}
export function percentage(value: number, total: number) {
  return `${(total ? (value / total) * 100 : 0).toFixed(1)}%`;
}
