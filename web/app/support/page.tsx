import type { Metadata } from "next";
import { AppSupportContent } from "@/components/AppSupportContent";

export const metadata: Metadata = {
  title: "App support",
  description: "Get help with Drop7 Research, report a problem, or find answers about games, replays, competitions, and privacy.",
  alternates: { canonical: "https://drop7.dev/support" },
  openGraph: {
    title: "Drop7 Research support",
    description: "Help with the app, saved games, replays, competitions, and privacy.",
    url: "https://drop7.dev/support",
  },
};

export default function SupportPage() {
  return (
    <article className="prose-drop7 mx-auto">
      <h1>Drop7 Research support</h1>
      <p>Help with the app, saved games, and competitions.</p>
      <AppSupportContent />
    </article>
  );
}
