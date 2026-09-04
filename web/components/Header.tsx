"use client";

import Link from "next/link";
import { usePathname } from "next/navigation";
import { useEffect, useState } from "react";
import { Button } from "@/components/Button";
import { DiscFace } from "@/components/discs";
import { SiteSearch } from "@/components/SiteSearch";

const REPO_URL = "https://github.com/keshavsaharia/drop7-bench";

/** Primary navigation. `match` lists the route prefixes an item is current for. */
const NAV = [
  { href: "/learn", label: "Learn", match: ["/learn"] },
  { href: "/approaches", label: "Approaches", match: ["/approaches"] },
  { href: "/engines", label: "Engines", match: ["/engines"] },
  { href: "/research", label: "Research", match: ["/research", "/theories", "/experiments", "/results", "/log", "/docs", "/diagnostics"] },
  { href: "/leaderboard", label: "Leaderboard", match: ["/leaderboard", "/compete"] },
] as const;

function isCurrent(pathname: string, prefixes: readonly string[]): boolean {
  return prefixes.some((prefix) => pathname === prefix || pathname.startsWith(`${prefix}/`));
}

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
    <header className="site-header">
      <div className="site-header-row mx-auto max-w-page px-4">
        <Link href="/" className="site-brand">
          <span className="site-brand-disc" aria-hidden="true">
            <DiscFace cell={7} />
          </span>
          Drop7 Research
        </Link>
        <nav aria-label="Primary navigation" className="site-nav">
          {NAV.map((item) => (
            <Link
              key={item.href}
              href={item.href}
              aria-current={isCurrent(pathname, item.match) ? "page" : undefined}
            >
              {item.label}
            </Link>
          ))}
        </nav>
        <SiteSearch />
        <div className="site-actions">
          <a href={REPO_URL} rel="noopener noreferrer" className="site-link">
            <GitHubMark />
            GitHub
          </a>
          <Button href="/play" icon={<DiscFace cell={7} />}>
            Play
          </Button>
        </div>
        <button
          type="button"
          aria-expanded={menuOpen}
          aria-controls="mobile-navigation"
          aria-label={menuOpen ? "Close navigation menu" : "Open navigation menu"}
          className="site-menu-button"
          onClick={() => setOpenAtPathname(menuOpen ? null : pathname)}
        >
          <MenuIcon open={menuOpen} />
        </button>
      </div>
      <nav
        id="mobile-navigation"
        aria-label="Mobile navigation"
        hidden={!menuOpen}
        className="site-mobile-nav"
      >
        <div className="site-mobile-nav-list mx-auto max-w-page">
          {NAV.map((item) => (
            <Link
              key={item.href}
              href={item.href}
              aria-current={isCurrent(pathname, item.match) ? "page" : undefined}
              onClick={() => setOpenAtPathname(null)}
            >
              {item.label}
            </Link>
          ))}
          <a href={REPO_URL} rel="noopener noreferrer">
            <GitHubMark />
            GitHub
          </a>
          <Button
            href="/play"
            icon={<DiscFace cell={7} />}
            className="mt-2 self-start"
            onClick={() => setOpenAtPathname(null)}
          >
            Play
          </Button>
        </div>
      </nav>
    </header>
  );
}
