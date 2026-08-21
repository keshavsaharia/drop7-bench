# Web deployment

The research console is deployed with SST and OpenNext. It remains a server-rendered
Next.js application: CloudFront serves versioned assets from a private S3 origin and
routes dynamic requests to Lambda.

Each stage also owns a competition ledger and a private, versioned artifact bucket:

| SST stage | DynamoDB ledger | Versioned artifact bucket |
| --- | --- | --- |
| production | drop7-prod-competition-ledger | drop7-prod-competition-artifacts |
| dev | drop7-dev-competition-ledger | drop7-dev-competition-artifacts |

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
`main` and `dev` branches.

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

In GitHub, create a repository **Actions variable** named `AWS_ROLE_ARN` with that value.
No AWS access keys are stored in GitHub. The old
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

The manifest at web/content/competition/global-2026-08-v1.json pins the current
scripted-round format, round ID, ruleset, object key, and SHA-256 digest. SST uploads that
exact round to the stage's versioned artifact bucket. The build fails if the checked-in
round no longer matches the manifest digest.

To promote a future global game, add a new immutable manifest and round, retain the old
definition in the application registry so historical submissions remain replayable, upload
it under a new S3 object key, and then change the current-game pointer. Never edit an old
game in place.

The DynamoDB table is an append-only validated-score ledger:

- primary key: SHA-256 submission ID;
- LeaderboardByScore GSI: game/version partition plus numeric verified score;
- attribution: OAuth provider, provider account ID, and public display handle;
- moves: dense drop7-columns-3bit-v1 binary, with a separate move count;
- audit fields: client score, replayed score, mismatch flag, artifact digest, and timestamps.

The API rejects illegal, incomplete, or trailing choices. It conditionally inserts each
validated run so the same user/game/move stream is idempotent. Client/server score
mismatches remain valid submissions, but are flagged in DynamoDB and structured CloudWatch
logs; only the independently replayed score is ranked.

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

The deploy output includes the site URL, asset bucket, and CloudFront distribution ID.
The checked-in SST config pins the existing `drop7.dev` hosted zone
`Z06342693O6N64NO6EU5M`, so certificate validation and alias records cannot drift to a
same-named zone in another account.

## Analytics

Server rendering provides CloudWatch request/error logs and Lambda/CloudFront operational
metrics without adding browser telemetry. Product analytics are deliberately not collected
yet: define the events, retention period, and IP/user-agent handling first. A first-party
Route Handler backed by a linked DynamoDB table can then record only the approved event
schema without sending visitor data to a third party.
