import Link from "next/link";
import { DiscFace } from "@/components/discs";

const FOOTER_GROUPS = [
  {
    title: "Play and learn",
    links: [
      { href: "/play", label: "Play Drop7" },
      { href: "/learn/rules", label: "How Drop7 works" },
      { href: "/learn/concepts", label: "Strategy concepts" },
      { href: "/compete", label: "Human competition" },
      { href: "/leaderboard", label: "Leaderboards" },
    ],
  },
  {
    title: "Research",
    links: [
      { href: "/research", label: "Research overview" },
      { href: "/approach", label: "Approaches" },
      { href: "/engine", label: "Engines" },
      { href: "/learn/techniques", label: "Techniques" },
      { href: "/theories", label: "Theories" },
      { href: "/experiments", label: "Experiments" },
      { href: "/results", label: "Results" },
      { href: "/diagnostics", label: "Diagnostics" },
      { href: "/log", label: "Research log" },
      { href: "/docs", label: "Documents" },
    ],
  },
  {
    title: "Project",
    links: [
      { href: "/support", label: "App support" },
      { href: "/src", label: "Browse the source" },
      { href: "/docs", label: "Documentation" },
      {
        href: "https://github.com/keshavsaharia/drop7-bench",
        label: "GitHub repository",
        external: true,
      },
      {
        href: "https://github.com/keshavsaharia/drop7-bench/issues",
        label: "Issues and discussion",
        external: true,
      },
    ],
  },
] as const;

export function Footer() {
  return (
    <footer className="border-t border-rule bg-surface">
      <div className="mx-auto grid max-w-page gap-10 px-4 py-10 sm:grid-cols-2 lg:grid-cols-[1.5fr_repeat(3,minmax(0,1fr))]">
        <section className="sm:col-span-2 lg:col-span-1">
          <Link href="/" className="site-brand">
            <span className="site-brand-disc" aria-hidden="true">
              <DiscFace cell={7} />
            </span>
            Drop7 Research
          </Link>
          <p className="mt-3 max-w-sm text-small text-ink-2">
            An open, reproducible research project working toward a public-information
            Drop7 policy that averages more than one million points.
          </p>
          <Link href="/privacy" className="mt-4 inline-block text-small text-ink-2 hover:text-ink">
            Privacy-first analytics
          </Link>
        </section>

        {FOOTER_GROUPS.map((group) => (
          <nav key={group.title} aria-label={`${group.title} links`}>
            <h2 className="label">{group.title}</h2>
            <ul className="mt-3 space-y-2 text-small">
              {group.links.map((item) => (
                <li key={item.href}>
                  {"external" in item && item.external ? (
                    <a href={item.href} rel="noopener noreferrer" className="text-ink-2 hover:text-ink">
                      {item.label} <span aria-hidden="true">↗</span>
                    </a>
                  ) : (
                    <Link href={item.href} className="text-ink-2 hover:text-ink">
                      {item.label}
                    </Link>
                  )}
                </li>
              ))}
            </ul>
          </nav>
        ))}
      </div>

      <div className="border-t border-rule">
        <div className="mx-auto flex max-w-page flex-col gap-3 px-4 py-5 text-caption text-ink-3 sm:flex-row sm:items-center sm:justify-between">
          <p>
            Independent research project. Not affiliated with the creators or owners of
            Drop7.
          </p>
          <nav aria-label="Legal" className="flex gap-4">
            <Link href="/privacy" className="hover:text-ink">
              Privacy policy
            </Link>
            <Link href="/terms" className="hover:text-ink">
              Terms of service
            </Link>
          </nav>
        </div>
      </div>
    </footer>
  );
}
