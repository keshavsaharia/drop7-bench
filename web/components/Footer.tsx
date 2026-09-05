import Link from "next/link";
import { DiscFace } from "@/components/discs";

const FOOTER_GROUPS = [
  {
    title: "Play & learn",
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
      { href: "/approaches", label: "Approaches" },
      { href: "/theories", label: "Theories" },
      { href: "/experiments", label: "Experiments" },
      { href: "/log", label: "Research log" },
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
        label: "Issues & discussion",
        external: true,
      },
    ],
  },
] as const;

export function Footer() {
  return (
    <footer className="border-t border-zinc-800 bg-zinc-950/70">
      <div className="mx-auto grid max-w-6xl gap-10 px-4 py-10 sm:grid-cols-2 lg:grid-cols-[1.5fr_repeat(3,minmax(0,1fr))]">
        <section className="sm:col-span-2 lg:col-span-1">
          <Link
            href="/"
            className="inline-flex items-center gap-2 font-bold text-zinc-100 hover:text-white"
          >
            <span className="inline-flex size-6 text-[0.7rem]" aria-hidden="true">
              <DiscFace cell={7} />
            </span>
            Drop7 Research
          </Link>
          <p className="mt-3 max-w-sm text-sm leading-relaxed text-zinc-500">
            An open, reproducible research project working toward a public-information
            Drop7 policy that averages more than one million points.
          </p>
          <Link
            href="/privacy"
            className="mt-4 inline-flex rounded-full border border-emerald-900/80 bg-emerald-950/30 px-3 py-1 text-xs font-medium text-emerald-300 hover:border-emerald-700 hover:text-emerald-200"
          >
            Privacy-first analytics
          </Link>
        </section>

        {FOOTER_GROUPS.map((group) => (
          <nav key={group.title} aria-label={`${group.title} links`}>
            <h2 className="text-xs font-semibold uppercase tracking-[0.16em] text-zinc-500">
              {group.title}
            </h2>
            <ul className="mt-3 space-y-2 text-sm">
              {group.links.map((item) => (
                <li key={item.href}>
                  {"external" in item && item.external ? (
                    <a
                      href={item.href}
                      rel="noopener noreferrer"
                      className="text-zinc-400 hover:text-zinc-100"
                    >
                      {item.label} <span aria-hidden="true">↗</span>
                    </a>
                  ) : (
                    <Link href={item.href} className="text-zinc-400 hover:text-zinc-100">
                      {item.label}
                    </Link>
                  )}
                </li>
              ))}
            </ul>
          </nav>
        ))}
      </div>

      <div className="border-t border-zinc-900">
        <div className="mx-auto flex max-w-6xl flex-col gap-3 px-4 py-5 text-xs text-zinc-600 sm:flex-row sm:items-center sm:justify-between">
          <p>
            Independent research project. Not affiliated with the creators or owners of
            Drop7.
          </p>
          <nav aria-label="Legal" className="flex gap-4">
            <Link href="/privacy" className="hover:text-zinc-300">
              Privacy policy
            </Link>
            <Link href="/terms" className="hover:text-zinc-300">
              Terms of service
            </Link>
          </nav>
        </div>
      </div>
    </footer>
  );
}
