/**
 * The link preview for one day of the research log: the entry's own title and
 * summary, with the day itself in the eyebrow. A date with no entry in this
 * checkout keeps the date as the title.
 */
import { formatLogDate, getLogEntryInfo } from "@/lib/log";
import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

export const dynamic = "force-dynamic";

export const alt = cardAlt({ title: "Research log entry" });
export const size = SOCIAL_SIZE;
export const contentType = SOCIAL_CONTENT_TYPE;

type Props = { params: Promise<{ date: string }> };

export default async function Image({ params }: Props) {
  const { date } = await params;
  const info = getLogEntryInfo(date);
  return renderPageCard({
    eyebrow: `Research log · ${formatLogDate(date)}`,
    title: info?.title || date,
    summary: info?.summary ?? undefined,
    path: `/log/${date}`,
  });
}
