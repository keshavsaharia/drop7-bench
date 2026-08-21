import type { Metadata } from "next";
import Link from "next/link";
import { Header } from "@/components/Header";
import "./globals.css";

export const metadata: Metadata = {
  title: "Drop7 Research Console",
  description:
    "Experiments, theories, deterministic scripted-round benchmarks, and strategy documentation for the Drop7 million-point research program.",
};

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body className="min-h-screen antialiased">
        <Header />
        <main className="mx-auto max-w-6xl px-4 py-8">{children}</main>
        <footer className="border-t border-zinc-800 py-6 text-center text-xs text-zinc-600">
          Public-information Drop7 research. Leaderboard rounds are scripted and
          deterministic; they are a playground, not a research tier.{" "}
          <a
            href="https://github.com/keshavsaharia/drop7-bench"
            rel="noopener noreferrer"
            className="text-zinc-500 underline underline-offset-2 hover:text-zinc-300"
          >
            Source on GitHub
          </a>
          {" · "}
          <Link href="/learn/rules" className="text-zinc-500 underline underline-offset-2 hover:text-zinc-300">
            How Drop7 works
          </Link>
        </footer>
      </body>
    </html>
  );
}
