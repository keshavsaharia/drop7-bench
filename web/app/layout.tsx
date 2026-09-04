import type { Metadata } from "next";
import { Footer } from "@/components/Footer";
import { Header } from "@/components/Header";
import { PageViewTracker } from "@/components/PageViewTracker";
import { display, mono, sans } from "./fonts";
import "./globals.css";

const SITE_URL = process.env.DROP7_SITE_URL ?? "https://drop7.dev";
const SITE_TITLE = "Drop7 Research";
const SITE_DESCRIPTION =
  "Play Drop7 in your browser and follow the open-source search for a million-point strategy.";

export const metadata: Metadata = {
  metadataBase: new URL(SITE_URL),
  title: {
    default: SITE_TITLE,
    template: `%s · ${SITE_TITLE}`,
  },
  description: SITE_DESCRIPTION,
  openGraph: {
    type: "website",
    siteName: SITE_TITLE,
    title: SITE_TITLE,
    description: SITE_DESCRIPTION,
  },
  twitter: {
    card: "summary_large_image",
    title: SITE_TITLE,
    description: SITE_DESCRIPTION,
  },
};

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en" className={`${display.variable} ${sans.variable} ${mono.variable}`}>
      <body className="min-h-screen">
        <PageViewTracker />
        <Header />
        <main className="mx-auto w-full max-w-page px-4 py-8">{children}</main>
        <Footer />
      </body>
    </html>
  );
}
