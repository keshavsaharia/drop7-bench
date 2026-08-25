import type { Metadata } from "next";
import { notFound, redirect } from "next/navigation";
import { auth } from "@/auth";
import { AnalyticsDashboard } from "@/components/AnalyticsDashboard";
import { isAnalyticsAdmin } from "@/lib/analytics/admin";

export const dynamic = "force-dynamic";

export const metadata: Metadata = {
  title: "Analytics | Drop7 Research",
  robots: { index: false, follow: false },
};

export default async function AnalyticsPage() {
  const session = await auth();
  if (!session?.user) {
    redirect("/api/auth/signin?callbackUrl=%2Fanalytics");
  }
  if (!isAnalyticsAdmin(session.user)) notFound();

  return <AnalyticsDashboard />;
}
