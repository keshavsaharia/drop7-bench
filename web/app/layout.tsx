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
          </div>
        </header>
        <main className="mx-auto max-w-6xl px-4 py-8">{children}</main>
        <footer className="border-t border-zinc-800 py-6 text-center text-xs text-zinc-600">
          Public-information Drop7 research. Leaderboard rounds are scripted and
          deterministic; they are a playground, not a research tier.
        </footer>
      </body>
    </html>
  );
}
