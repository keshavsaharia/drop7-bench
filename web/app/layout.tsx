import type { Metadata } from "next";
import { Footer } from "@/components/Footer";
import { Header } from "@/components/Header";
import { PageViewTracker } from "@/components/PageViewTracker";
import "./globals.css";

export const metadata: Metadata = {
  title: "Drop7 Research",
  description:
    "Experiments, theories, deterministic scripted-round benchmarks, and strategy documentation for the Drop7 million-point research program.",
};

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body className="min-h-screen antialiased">
        <PageViewTracker />
        <Header />
        <main className="mx-auto max-w-6xl px-4 py-8">{children}</main>
        <Footer />
      </body>
    </html>
  );
}
