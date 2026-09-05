import "../app.css";
import { ArticleLayout } from "@/components/ArticleLayout";
import { Callout } from "@/components/Board";
import { PageHeader } from "@/components/PageHeader";
import { pageMetadata } from "@/lib/metadata";

const GITHUB_REPO = "https://github.com/keshavsaharia/drop7-bench";

export const metadata = pageMetadata({
  title: "Privacy policy",
  description:
    "How Drop7 Research handles anonymous server metrics, local game data, GitHub sign-in, and competition submissions.",
  path: "/privacy",
});

export default function PrivacyPage() {
  return (
    <>
      <PageHeader title="Privacy without a black box">
        <span className="label">Privacy policy</span>
        <span>Effective September 4, 2026</span>
      </PageHeader>
      <ArticleLayout>
        <article className="prose-drop7">
          <Callout title="Summary" tone="success">
            <p>
              Ordinary visits are measured on the server with first-party page-view analytics.
              There are no third-party analytics scripts, advertising pixels, cross-site trackers,
              or analytics cookies. Raw IP addresses, URL query strings, and signed-in identities
              are not added to analytics. The mobile app contributes anonymous interaction events
              and completed game tapes without an account or device identifier. Competition
              leaderboard entries handle the display name and game data described below.
            </p>
          </Callout>

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

          <h2>Mobile app interactions</h2>
          <p>
            The mobile app records basic product interactions, including screens opened, mode and
            menu choices, legal game moves and their outcomes, level rises, board clears, completed
            and restarted games, replay and tutorial controls, haptic-setting changes, and
            competition submission outcomes. Each event includes its time, broad platform, app
            version, normalized screen name, and the small set of values needed to count that action.
          </p>
          <p>
            These events wait in a bounded queue on the device and are sent in batches. The server
            may add a truncated user agent, accepted language, and coarse country, region, or city
            supplied by the hosting network. It does not store the source network address or create a
            visitor ID for mobile interaction events. The events exclude display names, account IDs,
            device or advertising IDs, game and replay IDs, contacts, and precise location.
          </p>

          <h2>Completed games from the mobile app</h2>
          <p>
            When a game finishes in the mobile app, the app automatically and silently submits the
            exact disc tape, hidden covered-disc values, covered rows, column choices, ruleset,
            timestamps, claimed result, app version, and broad platform. The server independently
            replays the tape and stores only games whose moves and claimed result validate. It does
            not receive an account, advertising identifier, device identifier, contacts, or precise
            location with this record.
          </p>
          <p>
            The app stores each game and whether delivery succeeded on the device. Failed deliveries
            remain pending and are retried without interrupting play. Validated tapes are retained in
            an hour-partitioned Amazon S3 research archive and may be analyzed or released as research
            data under these terms.
          </p>
          <p>
            If you choose GitHub sign-in, the site sets a first-party, encrypted session cookie so
            you can remain signed in. It is essential to authentication, is not an advertising or
            analytics cookie, and may remain valid for up to 30 days. Signing out ends that session.
          </p>

          <h2>GitHub sign-in</h2>
          <p>
            Sign-in is optional and is only needed to submit a human competition score from the website. The site asks
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
            Website competition runs require an explicit submit action and GitHub sign-in. The mobile
            app instead lets you submit a completed competition run under any display name without an
            account. A leaderboard submission stores its source, display name, column choices, client
            and server-verified scores, move count, game/version identifiers, validation flags, and
            submission timestamps. Website submissions also store the GitHub provider and numeric
            account ID. The server may write validation or security events when it accepts or rejects
            a submission.
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
            storage. First-party website and mobile interaction analytics pass through Amazon Data
            Firehose and are stored as Parquet data in an Apache Iceberg table in Amazon S3, with
            metadata in AWS Glue and aggregate queries run by Amazon Athena. Validated mobile game
            submissions pass through a separate Amazon Data Firehose delivery stream into an
            hour-partitioned Amazon S3 archive. No separate analytics vendor receives those events.
            Like any internet host, AWS and GitHub may also process technical request data as needed
            to deliver, secure, and operate their services under their own terms.
          </p>

          <h2>Retention and your choices</h2>
          <p>
            Analytics query-result files expire after 7 days and failed Firehose delivery records
            expire after 30 days. The underlying Iceberg analytics event table is retained until the
            project operator deletes or applies a table-retention policy to it. Authentication data
            in the encrypted session cookie lasts until sign-out, deletion, or expiry. Validated
            mobile game tapes and competition submissions are retained as research records until the
            project operator deletes them or applies a retention policy, subject to applicable law.
          </p>
          <p>
            You may browse and play without an account, clear local game data through your browser or
            app settings, sign out, or revoke GitHub authorization. For a privacy question or a
            reasonable request to access, correct, or remove competition information, use the contact
            options on the{" "}
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
      </ArticleLayout>
    </>
  );
}
