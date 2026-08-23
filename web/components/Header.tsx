"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useEffect, useState } from "react";
import { DiscFace } from "@/components/discs";

const NAV = [
  { href: "/compete", label: "Compete" },
  { href: "/leaderboard", label: "Leaderboard" },
  { href: "/research", label: "Research" },
  { href: "/learn", label: "Learn" },
  { href: "/src", label: "Source" },
];

function GitHubMark() {
  return (
    <svg viewBox="0 0 16 16" width="15" height="15" aria-hidden="true" fill="currentColor">
      <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.01 8.01 0 0 0 16 8c0-4.42-3.58-8-8-8z" />
    </svg>
  );
}

function MenuIcon({ open }: { open: boolean }) {
  return (
    <svg
      viewBox="0 0 24 24"
      width="24"
      height="24"
      aria-hidden="true"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
    >
      {open ? (
        <>
          <path d="M6 6l12 12" />
          <path d="M18 6L6 18" />
        </>
      ) : (
        <>
          <path d="M4 6h16" />
          <path d="M4 12h16" />
          <path d="M4 18h16" />
        </>
      )}
    </svg>
  );
}

export function Header() {
  const pathname = usePathname();
  const [openAtPathname, setOpenAtPathname] = useState<string | null>(null);
  const menuOpen = openAtPathname === pathname;

  useEffect(() => {
    if (!menuOpen) return;

    function closeOnEscape(event: KeyboardEvent) {
      if (event.key === "Escape") setOpenAtPathname(null);
    }

    window.addEventListener("keydown", closeOnEscape);
    return () => window.removeEventListener("keydown", closeOnEscape);
  }, [menuOpen]);

  return (
    <header className="sticky top-0 z-10 border-b border-zinc-800 bg-zinc-950/90 backdrop-blur">
      <div className="mx-auto flex max-w-6xl items-center gap-4 px-4 py-3">
        <Link href="/" className="flex shrink-0 items-center gap-2 font-bold text-zinc-50">
          <span className="inline-flex size-6 text-[0.7rem]" aria-hidden="true">
            <DiscFace cell={7} />
          </span>
          Drop7 Research
        </Link>
        <nav aria-label="Primary navigation" className="hidden items-center gap-1 text-sm md:flex">
          {NAV.map((item) => (
            <Link
              key={item.href}
              href={item.href}
              className="rounded-md px-2.5 py-1.5 text-zinc-400 hover:bg-zinc-800 hover:text-zinc-100"
            >
              {item.label}
            </Link>
          ))}
        </nav>
        <div className="ml-auto hidden shrink-0 items-center gap-2 md:flex">
          <a
            href="https://github.com/keshavsaharia/drop7-bench"
            rel="noopener noreferrer"
            className="inline-flex items-center gap-1.5 rounded-md px-2.5 py-1.5 text-sm text-zinc-400 hover:bg-zinc-800 hover:text-zinc-100"
          >
            <GitHubMark />
            GitHub
          </a>
          <Link href="/play" className="play-cta">
            <span className="play-cta-disc" aria-hidden="true">
              <DiscFace cell={7} />
            </span>
            Play
          </Link>
        </div>
        <button
          type="button"
          aria-expanded={menuOpen}
          aria-controls="mobile-navigation"
          aria-label={menuOpen ? "Close navigation menu" : "Open navigation menu"}
          className="ml-auto inline-flex size-10 items-center justify-center rounded-md text-zinc-300 hover:bg-zinc-800 hover:text-zinc-50 focus-visible:outline-2 focus-visible:outline-offset-2 focus-visible:outline-sky-400 md:hidden"
          onClick={() => setOpenAtPathname(menuOpen ? null : pathname)}
        >
          <MenuIcon open={menuOpen} />
        </button>
      </div>
      <nav
        id="mobile-navigation"
        aria-label="Mobile navigation"
        hidden={!menuOpen}
        className="border-t border-zinc-800 px-4 pb-4 pt-2 md:hidden"
      >
        <div className="mx-auto flex max-w-6xl flex-col gap-1 text-sm">
          {NAV.map((item) => (
            <Link
              key={item.href}
              href={item.href}
              className="rounded-md px-3 py-2.5 text-zinc-300 hover:bg-zinc-800 hover:text-zinc-50"
              onClick={() => setOpenAtPathname(null)}
            >
              {item.label}
            </Link>
          ))}
          <a
            href="https://github.com/keshavsaharia/drop7-bench"
            rel="noopener noreferrer"
            className="flex items-center gap-2 rounded-md px-3 py-2.5 text-zinc-300 hover:bg-zinc-800 hover:text-zinc-50"
          >
            <GitHubMark />
            GitHub
          </a>
          <Link href="/play" className="play-cta mt-2 self-start" onClick={() => setOpenAtPathname(null)}>
            <span className="play-cta-disc" aria-hidden="true">
              <DiscFace cell={7} />
            </span>
            Play
          </Link>
        </div>
      </nav>
    </header>
  );
}
