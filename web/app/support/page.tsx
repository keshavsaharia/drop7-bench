import "../app.css";
import { AppSupportContent } from "@/components/AppSupportContent";
import { ArticleLayout } from "@/components/ArticleLayout";
import { PageHeader } from "@/components/PageHeader";
import { pageMetadata } from "@/lib/metadata";

export const metadata = pageMetadata({
  title: "App support",
  description: "Get help with Drop7 Research, report a problem, or find answers about games, replays, competitions, and privacy.",
  path: "/support",
});

export default function SupportPage() {
  return (
    <>
      <PageHeader
        title="Drop7 Research support"
        lead="Help with the app, saved games, and competitions."
      />
      <ArticleLayout>
        <article className="prose-drop7">
          <AppSupportContent />
        </article>
      </ArticleLayout>
    </>
  );
}
