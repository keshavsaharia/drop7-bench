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
