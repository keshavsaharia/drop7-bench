# Web deployment

The research console is deployed with SST and OpenNext. It remains a server-rendered
Next.js application: CloudFront serves versioned assets from a private S3 origin and
routes dynamic requests to Lambda.

Each stage also owns a competition ledger and a private, versioned artifact bucket:

| SST stage | DynamoDB ledger | Versioned artifact bucket |
| --- | --- | --- |
| production | drop7-prod-competition-ledger | drop7-prod-competition-artifacts |
| dev | drop7-dev-competition-ledger | drop7-dev-competition-artifacts |

Production also configures the existing `drop7-bench-data` bucket in `us-east-2` as
the public research-artifact store. The bucket itself keeps S3 public access blocked;
CloudFront origin access control serves known objects at `https://data.drop7.dev/`.
SST enables versioning, installs the CloudFront-only bucket policy, creates the DNS
record and certificate, and creates an IAM user and managed policy both named
`drop7-research`.

The research policy is intentionally narrower than "all resources whose name starts
with drop7". It can read and write objects (but not delete them) in
`drop7-bench-data`, read the immutable production/development competition artifacts,
and perform only `DescribeTable`, `GetItem`, `PutItem`, and `Query` against the two
competition ledgers. It can also run bounded Athena queries in the production and dev
analytics workgroups, read the two analytics tables and their backing data, and write
only Athena's temporary query results. It cannot deploy the site, ingest or alter
analytics, read Secrets Manager, manage IAM/EC2, or access protected/final research
data. The same policy is also the user's permissions boundary, so attaching another
policy cannot silently broaden the machine's effective access. Use the checked-in
competition CLI for ledger writes; possession of IAM credentials is not permission to
bypass its replay validation.

Before each build, `web/scripts/stage-repo-content.mjs` copies the repository-backed
approach, documentation, research, and optional leaderboard data into `web/build/repo`.
OpenNext traces that directory into the Lambda bundle, and `web/lib/repo.ts` reads it at
request time. Dynamic routes therefore keep working without `force-static` or a separate
content service.

| Git branch | SST stage | Domain | Asset bucket |
| --- | --- | --- | --- |
| `main` | `production` | `drop7.dev` | `drop7-prod-public` |
| `dev` | `dev` | `dev.drop7.dev` | `drop7-dev-public` |

Despite the requested `-public` names, both buckets remain private and are read through
CloudFront. SST creates the ACM certificates and Route 53 alias/validation records in the
existing `drop7.dev` hosted zone. Each stage owns its own CloudFront distribution. SST
uploads changed assets with long-lived caching for versioned files and invalidates `/*`
after every deployment; production waits for the invalidation to finish.

## One-time AWS and GitHub setup

SST needs an AWS role that GitHub Actions can assume through OIDC. The included
CloudFormation template restricts the trust policy to pushes from this repository's
`main` and `dev` branches. It uses GitHub's immutable owner and repository IDs in the
OIDC subject (`keshavsaharia@563902/drop7-bench@1340390077`), so a future account or
repository rename cannot transfer this AWS trust to a recycled name.

First check whether the GitHub OIDC provider already exists in the AWS account:

```sh
aws iam list-open-id-connect-providers
```

Then create the deployment role. Use `true` when the provider is absent, or `false` when
the account already has `token.actions.githubusercontent.com` configured:

```sh
aws cloudformation deploy \
  --stack-name drop7-github-sst-deploy \
  --template-file infra/github-actions-role.yaml \
  --capabilities CAPABILITY_NAMED_IAM \
  --parameter-overrides \
    CreateGitHubOidcProvider=true \
    CreateGitHubOAuthSecret=true
```

Read the resulting role ARN:

```sh
aws cloudformation describe-stacks \
  --stack-name drop7-github-sst-deploy \
  --query 'Stacks[0].Outputs[?OutputKey==`RoleArn`].OutputValue' \
  --output text
```

In GitHub, create repository **Actions variables** named `AWS_ROLE_ARN` with that
value and `ADMIN_GITHUB_USERNAME` with the GitHub login that should be allowed to
open `/analytics`. The username comparison is case-insensitive, but use the canonical
GitHub spelling for clarity. No AWS access keys are stored in GitHub. The old
`CLOUDFRONT_DEV_DISTRIBUTION_ID` and `CLOUDFRONT_PROD_DISTRIBUTION_ID` variables are not
needed because SST owns the distributions and invalidations.

The role starts with `AdministratorAccess`, matching SST's documented bootstrap path.
Its trust is branch- and repository-scoped, but the permissions are intentionally broad.
After both stages have deployed, use AWS IAM Access Analyzer against the role's CloudTrail
activity to replace that managed policy with a generated least-privilege policy.

## GitHub sign-in secret

The bootstrap stack creates one retained AWS Secrets Manager secret named
drop7-prod-github. Its initial JSON contains placeholders for the production and
development GitHub OAuth clients and an automatically generated AUTH_SECRET.

Open the secret in the AWS console and edit its current value. Copy the values from the
gitignored web/.env.local into GITHUB_CLIENT_ID and GITHUB_CLIENT_SECRET, and preserve the
generated AUTH_SECRET exactly. Never commit or paste the secret values into SST config,
GitHub Actions, or this repository.

Use this callback URL in the production GitHub OAuth application:

    https://drop7.dev/api/auth/callback/github

GitHub OAuth Apps accept one exact callback URL. To exercise OAuth on
dev.drop7.dev, create a second OAuth App with callback
https://dev.drop7.dev/api/auth/callback/github, then fill GITHUB_DEV_CLIENT_ID
and GITHUB_DEV_CLIENT_SECRET in the same AWS secret. The development site
otherwise remains fully playable offline, but sign-in and score submission will report
that its placeholder OAuth values are not configured.

Auth.js uses encrypted JWT cookies, so no session table or OAuth access token is stored.
The OAuth provider and stable provider account ID are copied into a validated score only
when the player explicitly submits a completed run. Adding Google later is a provider
configuration plus two more fields in this secret; the ledger's provider-neutral identity
fields do not need a schema migration.

## Competition data

`web/content/competition/catalog.json` names the current game and retains every archived
game. Each referenced manifest pins the scripted-round format, round ID, ruleset, object key,
and SHA-256 digest. SST uploads every catalogued round to the stage's versioned artifact
bucket. The build fails if any checked-in round no longer matches its manifest digest. Each
object upload explicitly depends on the bucket component so its first write cannot race ahead
of S3 versioning, and the pinned digest is also retained as object metadata.

Use the competition CLI to preview and perform those lifecycle changes:

```sh
npm run competition -- list
npm run competition -- start \
  --version 2026-09-v1 \
  --round gauntlet-02 \
  --name "Global Game · September 2026"
npm run competition -- start \
  --version 2026-09-v1 \
  --round gauntlet-02 \
  --name "Global Game · September 2026" \
  --write
npm run competition -- activate --game-key 'global#2026-08-v1' --write
```

`start` creates an immutable manifest, archives the previous current entry, and advances the
catalog pointer. `activate` can reopen a catalogued game; `archive` marks a non-current game
with an archive timestamp. Mutations preview by default and require `--write`. Deploy after a
catalog change so the site and S3 artifact registry advance together. Never edit an old game
or its round in place. Archived leaderboards remain selectable in `/leaderboard` and their
packed submissions remain replayable through the catalog.

The DynamoDB table is an append-only validated-score ledger:

- primary key: SHA-256 submission ID;
- LeaderboardByScore GSI: game/version partition plus numeric verified score;
- attribution: OAuth provider, provider account ID, and public display handle;
- moves: dense drop7-columns-3bit-v1 binary, with a separate move count;
- audit fields: client score, replayed score, mismatch flag, artifact digest, and timestamps.

AI contenders use the same immutable ledger and packed-move replay path with
`recordType=policy-score`. Their records additionally retain the stable policy id, family,
public-information flag, trajectory checksum, Git revision/dirty disclosure, and an HTTPS URL
to the exact approach page. The leaderboard reads those stored fields, so an archived result
does not silently acquire a different attribution when the policy registry changes later.

Preview or seed the standard public-information contender set with:

```sh
npm run competition -- seed \
  --stage production \
  --profile personal-deploy
npm run competition -- seed \
  --stage production \
  --profile personal-deploy \
  --write
```

The default set includes the TypeScript D4/D3/D2 expectimax line and the registered public
rollout, MCTS, sparse, risk-sensitive, open-loop, and greedy policies. Use `--policies` for an
explicit comma-separated subset or `--game-key` to seed an archived game. The command runs
each policy on the immutable round, rejects illegal play, independently replays the packed
columns, and uses a conditional DynamoDB insert. Re-running an identical seed is idempotent;
a changed result under the same policy id is rejected and must use a versioned id. These
single scripted-game scores are playground demonstrations, never research-tier evidence.

A slow policy whose game was already played elsewhere (for example a multi-hour Rust
depth-6 game from a larger machine, saved by `just play-round` under `runs/BENCH-*/`) is
imported rather than re-run:

```sh
npm run competition -- seed \
  --stage production \
  --profile personal-deploy \
  --replay runs/<run-id>/replays/<policy>--<round>.json \
  --source-revision <sha> \
  --write
```

`--replay` reads the bench replay (the `drop7-leaderboard-v1` game record with its frames),
checks that it names the competition's round and a registered policy, replays the recorded
columns independently against the immutable round, and requires the replayed score, move
count, censor flag, every per-move board, and the trajectory checksum to match the recording
exactly. The policy itself never runs, so the import takes seconds. Because the game was not
produced by this checkout, `--source-revision` must state the commit the producing machine had
checked out (or the literal `unknown`), and the record omits the dirty-worktree flag rather
than claiming one. The same conditional insert applies: an occupied policy slot is left alone
when it already holds the identical result and rejected otherwise.

The API rejects illegal, incomplete, or trailing choices. It conditionally inserts each
validated run so the same user/game/move stream is idempotent. Client/server score
mismatches remain valid submissions, but are flagged in DynamoDB and structured CloudWatch
logs; only the independently replayed score is ranked.

Mobile clients can submit to the same competition endpoint without an account by sending a
display name, a stable per-run identifier, and `source=mobile-app`. The server still replays
the pinned round before inserting it. Leaderboard records retain `sourceApplication` and
`sourcePlatform`, which lets the public leaderboard distinguish mobile results from website
and policy submissions. `web/public/competition/catalog.json` is generated at build time from
the immutable registry so the app can switch among the current and archived games.

## Completed mobile-game archive

The unauthenticated `POST /api/submit/hardcore` and `POST /api/submit/classic` routes accept
the exact version-2 mobile tape schema. The route name, ruleset, tape shape, source metadata,
and claimed score/level/move count must agree. The Lambda independently replays every move
through the corresponding TypeScript engine and sends only a complete matching game to the
stage Firehose stream. Payloads contain no account or device identifier.

Each stage owns a separate private, versioned S3 archive and direct-PUT Firehose stream:

| SST stage | Firehose stream | S3 bucket | Glue table |
| --- | --- | --- | --- |
| `production` | `drop7-production-game-submissions` | `drop7-prod-game-submissions` | `game_submissions` |
| `dev` | `drop7-dev-game-submissions` | `drop7-dev-game-submissions` | `game_submissions` |

Firehose buffers for up to 15 minutes or 128 MiB and GZIP-compresses newline-delimited JSON
under `game-submissions/year=YYYY/month=MM/day=DD/hour=HH/`. Firehose's direct-S3 maximum
buffering interval is 15 minutes, so an hour partition can contain more than one object; the
hour prefix is the stable unit for downstream compaction and study. Failed records go under
`firehose-errors/` and expire after 30 days. The external Glue JSON table exposes validated
metadata plus the exact tape JSON for Athena queries.

## Deploy and inspect locally

Use an AWS profile that can create the resources SST needs:

```sh
npm ci
npm ci --prefix web
npm run infra:diff:dev
npm run infra:dev
npm run infra:diff:prod
npm run infra:prod
```

The deploy output includes the site URL, asset bucket, website distribution ID, and the
separate `ResearchDataDistributionId` for `data.drop7.dev`.
The checked-in SST config pins the existing `drop7.dev` hosted zone
`Z06342693O6N64NO6EU5M`, so certificate validation and alias records cannot drift to a
same-named zone in another account.

The `drop7-bench-data` bucket predates SST, so the stack references it rather than
importing the base bucket. This avoids making removal of the web stack capable of
deleting research artifacts; SST still owns the bucket's versioning, public-access
block, CORS, CloudFront policy, distribution, certificate, and DNS record.

The stack deliberately does not create an IAM access key. A Pulumi/SST-managed access
key would place the long-lived secret in infrastructure state. After the production
deploy, create one key from **IAM → Users → drop7-research → Security credentials**,
copy the secret once into the research machine's credential store, and configure a
dedicated profile:

```sh
aws configure --profile drop7-research
# default region: us-east-1 (the competition ledger); EC2 artifact tooling selects us-east-2
aws sts get-caller-identity --profile drop7-research
aws s3 cp local-artifact \
  s3://drop7-bench-data/runs/<run-id>/local-artifact \
  --profile drop7-research
```

Agents should normally use the checked-in publisher, which requires an explicit public
release acknowledgement, refuses a conflicting object key, stores and verifies the
SHA-256 metadata, and prints the reference to copy into a research record:

```sh
npm run artifact:publish -- \
  --run-id RUN-YYYYMMDDTHHMMSSZ-0123abcd \
  --file runs/RUN-YYYYMMDDTHHMMSSZ-0123abcd/per-game.jsonl \
  --public
```

Use the read-only Athena helper for website analytics and validated mobile-game
history. It defaults to production, rejects non-`SELECT`/`WITH` SQL, caps results at
500 rows, uses the deployment's 1 GiB scan limit, and defaults to the
`drop7-research` profile:

```sh
npm run analytics:query -- --query \
  'SELECT mode, count(*) AS games FROM game_submissions GROUP BY mode'
```

Do not commit the key, put it in shell history, or share one key among machines. Attach
the reusable `drop7-research` policy to a separate workload identity when another
machine needs access, and rotate or disable unused long-lived keys.

## Analytics

Drop7 uses a first-party analytics path for website page views and anonymous mobile-app
interactions. There is no analytics cookie or third-party analytics endpoint. Next.js `proxy.ts`
records initial document requests and excludes prefetches, API calls, static assets, and the
admin analytics route. A minimal first-party pathname hook reports client-side navigations that
Next.js can otherwise serve entirely from its prefetch cache. The mobile app keeps a durable,
bounded local queue and sends at most 50 events per request. It flushes once per minute, when
the queue fills, and when the app changes foreground state, so normal play does not invoke a
Lambda for every interaction.

The server submits page views with Firehose `PutRecord` and each app request with one
`PutRecordBatch`, retrying only records Firehose rejects. Page-view delivery remains best-effort:
a Firehose or Secrets Manager failure is logged but never fails the page request. The app event
endpoint returns a retryable error when a batch cannot be delivered, and the app retains that
batch locally for a later attempt.

Each stage owns the complete analytics path:

| SST stage | Firehose stream | Glue database | Iceberg table | Athena workgroup | S3 bucket |
| --- | --- | --- | --- | --- | --- |
| `production` | `drop7-production-page-views` | `drop7_production_analytics` | `page_views` | `drop7-production-analytics` | `drop7-prod-analytics` |
| `dev` | `drop7-dev-page-views` | `drop7_dev_analytics` | `page_views` | `drop7-dev-analytics` | `drop7-dev-analytics` |

Firehose uses direct PUT and an append-only Apache Iceberg destination. It buffers for 300
seconds or 64 MiB, whichever happens first, and writes Iceberg v2 data files in Parquet
format under `warehouse/page_views/`. Failed delivery records go under
`firehose-errors/` and expire after 30 days. Athena query results go under
`athena-results/` and expire after 7 days. The Iceberg event table is the durable source;
rows are retained until an operator applies a documented table-retention policy.
AWS Glue managed optimizers compact the five-minute files and run snapshot retention and
orphan-file cleanup daily. They retain at least 10 snapshots and seven days of snapshot
history; cleanup does not expire page-view rows that remain in the current table snapshot.

The shared event schema records event and receipt time, stage, source application/platform and
version, a normalized screen, and a bounded JSON object of primitive event properties. Website
rows also record normalized page path, referrer hostname and channel, truncated user agent and
accepted language, CloudFront country/region/city when present, coarse device/browser/OS
families, bot classification, and a pseudonymous visitor ID. It deliberately omits URL query
strings, referrer paths and query strings, raw IP addresses, cookies, GitHub identity, account
identity, device identity, replay/game IDs, and competition display names. Website visitor IDs
are an HMAC of request address and user agent using the existing Auth.js secret; the source
address is discarded before the event is sent. Mobile interaction rows do not have a visitor ID.

`/analytics` and `/api/analytics/query` both require a GitHub Auth.js session whose handle
matches `ADMIN_GITHUB_USERNAME`. Other signed-in users receive a 404. The page offers
aggregate time series and page, acquisition, referrer, country, device, browser, OS, and
user-agent breakdowns (five visible rows, expandable up to the top 40), plus a separate iOS-app view backed by validated completed-game submissions and
a read-only SQL workspace. The iOS view reports completed games, moves, score averages,
mode mix, and app-version mix without joining to an account or device identifier. Custom SQL
accepts one `SELECT` or `WITH`
statement, returns at most 500 rows, and runs in a dedicated Athena engine-v3 workgroup
that enforces its result location and a 1 GiB per-query scan ceiling. The site Lambda role
can read only the analytics Glue resources and S3 bucket, write only Athena results, run
queries only in that workgroup, and put records only into the stage analytics stream.

The navigation canvas groups existing page views into inferred sessions after 30 minutes
of inactivity. It deduplicates event deliveries, keeps each full route prefix separate,
and returns aggregate counts for the first six pages of the 400 most frequent sequences.
Coverage is shown against all inferred sessions in the selected period; conditional edge
percentages describe the represented subset. The time window can cut across a session,
shared visitor hashes or tabs can mix paths, and missing events can omit steps. A missing
next page is an observed endpoint, not proof of an exit. No new tracking is collected.

Deployments require a non-empty `ADMIN_GITHUB_USERNAME` and stop before changing
resources if it is missing. The GitHub workflow checks the repository variable before
installing dependencies; local SST deployments must pass the same value explicitly.

For a local UI build, only `ADMIN_GITHUB_USERNAME` is needed; Firehose collection is a
no-op when `DROP7_ANALYTICS_FIREHOSE_STREAM` is absent. To exercise live analytics locally,
set all four `DROP7_ANALYTICS_*` variables shown in `web/.env.example` and use AWS
credentials that have the same Firehose/Athena/Glue/S3 permissions as the deployed site.

After deployment, allow one five-minute delivery window, visit a few public routes, sign in
as the configured admin, and open `/analytics`. Delivery failures appear in the Firehose
CloudWatch log group `/aws/kinesisfirehose/drop7-<stage>-page-views`; query IDs and scanned
bytes are shown directly below each dashboard or custom result.
