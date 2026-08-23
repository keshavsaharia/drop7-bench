import type { Metadata } from "next";
import Link from "next/link";

const GITHUB_REPO = "https://github.com/keshavsaharia/drop7-bench";

export const metadata: Metadata = {
  title: "Terms of Service | Drop7 Research",
  description:
    "Terms for using the Drop7 Research site, browser game, research records, and human competition.",
};

export default function TermsPage() {
  return (
    <article className="prose-drop7 mx-auto">
      <p className="text-xs font-semibold uppercase tracking-[0.18em] text-sky-400">
        Terms of service
      </p>
      <h1 className="!mt-2">Terms for using Drop7 Research</h1>
      <p className="text-sm text-zinc-500">Effective August 23, 2026</p>

      <p>
        These terms apply when you use the Drop7 Research website, browser game, research
        console, source browser, or competition features (together, the “site”). By using the
        site, you agree to these terms. If you do not agree, do not use the site.
      </p>

      <h2>Research project, not a commercial game service</h2>
      <p>
        The site is an independent, open research project studying public-information strategies
        for Drop7. Research records may include preliminary, negative, incomplete, superseded, or
        playground results. Scripted-round leaderboards are demonstrations, not scientific
        evidence, promises of performance, or professional advice.
      </p>

      <h2>Permitted use</h2>
      <p>
        You may read the site, play the browser game, reproduce published research, inspect the
        source, and use the public interfaces for legitimate research, education, and personal
        experimentation. You must comply with applicable law and with any license or attribution
        terms attached to particular source files, data, or third-party materials.
      </p>
      <p>You may not:</p>
      <ul>
        <li>interfere with the site, its hosting, authentication, or other visitors;</li>
        <li>probe or exploit security vulnerabilities except through responsible disclosure;</li>
        <li>submit unlawful, deceptive, malicious, or impersonated competition records;</li>
        <li>evade technical limits in a way that burdens or disrupts the service; or</li>
        <li>misrepresent playground output as research-tier evidence.</li>
      </ul>

      <h2>Competition participation</h2>
      <p>
        You may play without signing in. Submitting a completed run requires GitHub sign-in and an
        explicit submit action. By submitting, you confirm that you control the account used, that
        the submitted choices came from the represented run, and that the project may validate,
        rank, display, archive, and study that submission as described in the{" "}
        <Link href="/privacy">privacy policy</Link>.
      </p>
      <p>
        The independently replayed server score controls the leaderboard. Runs may be rejected if
        they are illegal, incomplete, malformed, or inconsistent with the pinned game artifact.
        The project may correct or remove fraudulent, abusive, broken, or legally problematic
        entries. Unless a separate written announcement says otherwise, competitions have no
        prize, employment offer, or promise of continued availability.
      </p>

      <h2>Source code, research, and trademarks</h2>
      <p>
        The site&apos;s source is publicly available in the{" "}
        <a href={GITHUB_REPO} rel="noopener noreferrer">
          GitHub repository
        </a>
        . Public visibility does not replace a license: any license or notice accompanying a
        particular work governs your reuse of that work. You retain rights in material you own;
        by deliberately submitting competition moves, you grant the project a worldwide,
        non-exclusive, royalty-free license to validate, reproduce, analyze, publish, and include
        those moves and their resulting game data in the leaderboard and research archive.
      </p>
      <p>
        Drop7 and related names, artwork, and trademarks belong to their respective owners. This
        project is not affiliated with, endorsed by, or sponsored by those owners. Do not use the
        site or its materials in a way that implies otherwise.
      </p>

      <h2>Third-party services and links</h2>
      <p>
        Optional GitHub sign-in and links to outside sites are governed by those services&apos; own
        terms and privacy policies. The project does not control and is not responsible for
        third-party content, availability, or conduct.
      </p>

      <h2>Availability and changes</h2>
      <p>
        The site may change, pause, remove, or discontinue features or competitions at any time.
        Research records may be corrected through the project&apos;s documented evidence process.
        Material changes to these terms will be published here with a new effective date.
      </p>

      <h2>No warranties</h2>
      <p>
        To the fullest extent permitted by law, the site and its contents are provided “as is” and
        “as available,” without warranties of accuracy, completeness, fitness for a particular
        purpose, non-infringement, security, or uninterrupted availability. You use research code,
        strategies, data, and game features at your own risk.
      </p>

      <h2>Limitation of liability</h2>
      <p>
        To the fullest extent permitted by law, the project owner and contributors will not be
        liable for indirect, incidental, special, consequential, exemplary, or punitive damages,
        or for lost data, profits, opportunities, or goodwill, arising from or related to your use
        of the site. Nothing in these terms limits liability that cannot legally be limited.
      </p>

      <h2>Contact</h2>
      <p>
        Questions about these terms can be raised through the{" "}
        <a href={`${GITHUB_REPO}/issues`} rel="noopener noreferrer">
          project&apos;s GitHub issue tracker
        </a>
        . Do not include secrets or sensitive personal information in a public issue.
      </p>
    </article>
  );
}
