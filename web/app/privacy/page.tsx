import type { Metadata } from "next";

const GITHUB_REPO = "https://github.com/keshavsaharia/drop7-bench";

export const metadata: Metadata = {
  title: "Privacy Policy | Drop7 Research",
  description:
    "How Drop7 Research handles anonymous server metrics, local game data, GitHub sign-in, and competition submissions.",
};

export default function PrivacyPage() {
  return (
    <article className="prose-drop7 mx-auto">
      <p className="text-xs font-semibold uppercase tracking-[0.18em] text-emerald-400">
        Privacy policy
      </p>
      <h1 className="!mt-2">Privacy without a black box</h1>
      <p className="text-sm text-zinc-500">Effective August 23, 2026</p>

      <div className="my-6 rounded-xl border border-emerald-900/70 bg-emerald-950/20 p-5">
        <p className="!m-0 font-semibold text-emerald-200">The short version</p>
        <p className="!mb-0 text-zinc-300">
          Ordinary visits are measured on the server with first-party page-view analytics.
          There are no third-party analytics scripts, advertising pixels, cross-site trackers,
          or analytics cookies. Raw IP addresses, URL query strings, and signed-in identities
          are not added to analytics. If you choose to sign in with GitHub and submit a
          competition run, that optional feature necessarily handles the identity and game data
          described below.
        </p>
      </div>

      <h2>Ordinary browsing</h2>
      <p>
        Drop7 Research does not use third-party client-side analytics, advertising technology,
        tracking pixels, or cross-site tracking. A small first-party navigation hook reports only
        the new pathname after in-site navigation so pages served from the browser&apos;s Next.js
        prefetch cache are counted. The server constructs the analytics event from that pathname
        and the request headers. The project does not sell personal information or combine site
        analytics with advertising data.
      </p>
      <p>
        For each real page navigation, the server records the time, normalized page path and
        site host; referrer hostname and broad channel; a truncated user-agent string and
        accepted language; country, region, and city headers supplied by the hosting network
        when available; coarse device, browser, and operating-system families; and whether the
        request appears automated. It does not record URL query strings, referrer paths or
        queries, cookies, names, email addresses, GitHub IDs, or authentication state.
      </p>
      <p>
        The source network address and user agent are combined with a secret in a one-way HMAC
        to make a pseudonymous visitor ID for aggregate visitor counts. The raw network address
        is discarded before the event enters analytics. This identifier is first-party, is not
        placed in your browser, and is not shared with an advertising or analytics provider.
      </p>

      <h2>Open-source transparency</h2>
      <p>
        The website code and its deployment configuration are public in the{" "}
        <a href={GITHUB_REPO} rel="noopener noreferrer">
          project repository
        </a>
        . That includes the browser code, server-side event schema, authentication flow, admin
        access checks, and Firehose/Iceberg/Athena infrastructure definition. Any material change
        to that collection would be visible in the source and would require this policy to change.
      </p>

      <h2>Data kept in your browser</h2>
      <p>
        The playable game uses your browser&apos;s local storage to remember personal best scores.
        The competition game also uses local storage to preserve an unfinished move sequence and
        its local best. This information stays on your device and is not sent automatically. You
        can remove it at any time by clearing site data in your browser.
      </p>
      <p>
        If you choose GitHub sign-in, the site sets a first-party, encrypted session cookie so
        you can remain signed in. It is essential to authentication, is not an advertising or
        analytics cookie, and may remain valid for up to 30 days. Signing out ends that session.
      </p>

      <h2>GitHub sign-in</h2>
      <p>
        Sign-in is optional and is only needed to submit a human competition score. The site asks
        GitHub for read-only access to basic profile data. During sign-in it processes your GitHub
        numeric account ID, username or display name, and other basic profile fields returned by
        GitHub for the session. The project does not request repository access and does not store
        your GitHub OAuth access token.
      </p>
      <p>
        GitHub operates that authorization step under its own{" "}
        <a
          href="https://docs.github.com/en/site-policy/privacy-policies/github-general-privacy-statement"
          rel="noopener noreferrer"
        >
          privacy statement
        </a>
        . You can revoke the project&apos;s access from your GitHub application settings.
      </p>

      <h2>Competition submissions</h2>
      <p>
        Nothing is submitted merely because you play. After a run ends, you must explicitly
        choose to contribute it. A submission stores your GitHub provider and numeric account ID,
        GitHub username or display name, column choices, client and server-verified scores, move
        count, game/version identifiers, validation flags, and submission timestamps. The server
        may also write identity-linked validation or security events when it accepts or rejects a
        submission.
      </p>
      <p>
        Your display name, score, move count, and submission time may appear publicly on the
        leaderboard and replay pages. The underlying competition ledger is intended to be a
        durable research record. Research datasets derived from human move streams are published
        without GitHub account identifiers.
      </p>

      <h2>Service providers</h2>
      <p>
        The site uses GitHub for optional authentication and Amazon Web Services for hosting and
        storage. First-party analytics events pass through Amazon Data Firehose and are stored as
        Parquet data in an Apache Iceberg table in Amazon S3, with metadata in AWS Glue and
        aggregate queries run by Amazon Athena. No separate analytics vendor receives those
        events. Like any internet host, AWS and GitHub may also process technical request data as
        needed to deliver, secure, and operate their services under their own terms.
      </p>

      <h2>Retention and your choices</h2>
      <p>
        Analytics query-result files expire after 7 days and failed Firehose delivery records
        expire after 30 days. The underlying Iceberg page-view table is retained until the project
        operator deletes or applies a table-retention policy to it. Authentication data in the
        encrypted session cookie lasts until sign-out, deletion, or expiry. Competition submissions
        are retained with the public research record for as long as the project maintains that
        competition, subject to applicable law.
      </p>
      <p>
        You may browse and play without an account, clear local game data through your browser,
        sign out, or revoke GitHub authorization. For a privacy question or a reasonable request
        to access, correct, or remove competition information, use the contact options on the{" "}
        <a href="https://github.com/keshavsaharia" rel="noopener noreferrer">
          repository owner&apos;s GitHub profile
        </a>
        . Do not post sensitive information in a public issue.
      </p>

      <h2>Children</h2>
      <p>
        This research site is not directed to children under 13, and the project does not
        knowingly collect personal information from them.
      </p>

      <h2>Changes to this policy</h2>
      <p>
        Material changes will be published on this page and in the public repository, with a new
        effective date. The current policy will always be linked from the site footer.
      </p>
    </article>
  );
}
