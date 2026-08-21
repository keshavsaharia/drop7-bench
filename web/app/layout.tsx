import type { Metadata } from "next";
import Link from "next/link";
import "./globals.css";

export const metadata: Metadata = {
  title: "Drop7 Research Console",
  description:
    "Experiments, theories, deterministic scripted-round benchmarks, and strategy documentation for the Drop7 million-point research program.",
};

const NAV = [
  { href: "/", label: "Overview" },
  { href: "/leaderboard", label: "Leaderboard" },
  { href: "/approaches", label: "Approaches" },
  { href: "/theories", label: "Theories" },
  { href: "/experiments", label: "Experiments" },
  { href: "/learn/rules", label: "The game" },
  { href: "/learn/concepts", label: "Concepts" },
  { href: "/learn", label: "Learn" },
  { href: "/docs/research/status", label: "Docs" },
];

export default function RootLayout({
  children,
}: Readonly<{ children: React.ReactNode }>) {
  return (
    <html lang="en">
      <body className="min-h-screen antialiased">
        <header className="sticky top-0 z-10 border-b border-zinc-800 bg-zinc-950/90 backdrop-blur">
          <div className="mx-auto flex max-w-6xl items-center gap-6 px-4 py-3">
            <Link href="/" className="flex items-center gap-2 font-bold text-zinc-50">
              <span className="inline-flex h-6 w-6 items-center justify-center rounded-full bg-sky-500 text-xs font-black text-white">
                7
              </span>
              Drop7 Research
            </Link>
            <nav className="flex flex-wrap items-center gap-1 text-sm">
              {NAV.map((item) => (
                <Link
                  key={item.label}
                  href={item.href}
                  className="rounded-md px-2.5 py-1.5 text-zinc-400 hover:bg-zinc-800 hover:text-zinc-100"
                >
                  {item.label}
                </Link>
              ))}
            </nav>
            <a
              href="https://github.com/keshavsaharia/drop7-bench"
              rel="noopener noreferrer"
              className="ml-auto hidden items-center gap-1.5 rounded-md px-2.5 py-1.5 text-sm text-zinc-400 hover:bg-zinc-800 hover:text-zinc-100 sm:inline-flex"
            >
              <svg viewBox="0 0 16 16" width="15" height="15" aria-hidden="true" fill="currentColor">
                <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8z" />
              </svg>
              GitHub
            </a>
          </div>
        </header>
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
