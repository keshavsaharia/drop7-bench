/// <reference path="./.sst/platform/config.d.ts" />

const deployments = {
  production: {
    domain: "drop7.dev",
    bucket: "drop7-prod-public",
    ledgerTable: "drop7-prod-competition-ledger",
    artifactBucket: "drop7-prod-competition-artifacts",
  },
  dev: {
    domain: "dev.drop7.dev",
    bucket: "drop7-dev-public",
    ledgerTable: "drop7-dev-competition-ledger",
    artifactBucket: "drop7-dev-competition-artifacts",
  },
} as const;
const DROP7_HOSTED_ZONE_ID = "Z06342693O6N64NO6EU5M";

export default $config({
  app(input) {
    return {
      name: "drop7-bench",
      home: "aws",
      removal: input?.stage === "production" ? "retain" : "remove",
      providers: {
        aws: {
          region: "us-east-1",
        },
      },
    };
  },
  async run() {
    const deployment = deployments[$app.stage as keyof typeof deployments];
    const isProduction = $app.stage === "production";
    const competitionLedger = new sst.aws.Dynamo("CompetitionLedger", {
      fields: {
        submissionId: "string",
        gameKey: "string",
        verifiedScore: "number",
      },
      primaryIndex: { hashKey: "submissionId" },
      globalIndexes: {
        LeaderboardByScore: {
          hashKey: "gameKey",
          rangeKey: "verifiedScore",
          projection: "all",
        },
      },
      deletionProtection: isProduction,
      transform: {
        table: deployment ? { name: deployment.ledgerTable } : undefined,
      },
    });
    const competitionArtifacts = new sst.aws.Bucket("CompetitionArtifacts", {
      versioning: true,
      transform: {
        bucket: deployment ? { bucket: deployment.artifactBucket } : undefined,
      },
    });
    new aws.s3.BucketObjectv2("GlobalCompetitionGame202608V1", {
      bucket: competitionArtifacts.name,
      key: "games/global/2026-08-v1.json",
      contentType: "application/json",
      metadata: {
        "drop7-artifact-sha256":
          "edb288f628f4191d91d4de36d042cea14a9cf1a84d19156b1b8e6117f1463832",
      },
      source: $asset("src/bench/rounds/gauntlet-01.json"),
    }, { dependsOn: [competitionArtifacts] });
    const caller = aws.getCallerIdentityOutput({});
    const githubSecretArn = caller.accountId.apply(
      (accountId) =>
        "arn:aws:secretsmanager:us-east-1:" +
        accountId +
        ":secret:drop7-prod-github-*",
    );

    const site = new sst.aws.Nextjs("ResearchWeb", {
      path: "web",
      domain: deployment
        ? {
            name: deployment.domain,
            dns: sst.aws.dns({ zone: DROP7_HOSTED_ZONE_ID }),
          }
        : undefined,
      environment: {
        DROP7_STAGE: $app.stage,
        DROP7_SITE_URL: deployment?.domain
          ? "https://" + deployment.domain
          : "http://localhost:3000",
        DROP7_COMPETITION_TABLE: competitionLedger.name,
        DROP7_COMPETITION_ARTIFACT_BUCKET: competitionArtifacts.name,
        DROP7_GITHUB_SECRET_NAME: "drop7-prod-github",
      },
      permissions: [
        {
          actions: ["dynamodb:GetItem", "dynamodb:PutItem", "dynamodb:Query"],
          resources: [
            competitionLedger.arn,
            competitionLedger.arn.apply((arn) => arn + "/index/*"),
          ],
        },
        {
          actions: ["secretsmanager:GetSecretValue"],
          resources: [githubSecretArn],
        },
      ],
      assets: {
        purge: true,
        versionedFilesCacheHeader: "public,max-age=31536000,immutable",
        nonVersionedFilesCacheHeader:
          "public,max-age=0,s-maxage=86400,stale-while-revalidate=8640",
      },
      invalidation: {
        paths: "all",
        wait: $app.stage === "production",
      },
      server: {
        runtime: "nodejs24.x",
        memory: "1024 MB",
        timeout: "20 seconds",
      },
      transform: {
        assets: deployment
          ? {
              transform: {
                bucket: {
                  bucket: deployment.bucket,
                },
              },
            }
          : undefined,
        cdn: (args) => {
          args.comment = `Drop7 research console (${$app.stage})`;
        },
      },
    });

    return {
      SiteUrl: site.url,
      AssetsBucket: site.nodes.assets?.name,
      DistributionId: site.nodes.cdn?.nodes.distribution.id,
      CompetitionLedger: competitionLedger.name,
      CompetitionArtifactBucket: competitionArtifacts.name,
    };
  },
});
