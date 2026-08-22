/// <reference path="./.sst/platform/config.d.ts" />

interface CompetitionArtifact {
  gameKey: string;
  artifactPath: string;
  artifactS3Key: string;
  artifactSha256: string;
}

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
    const { createHash } = await import("node:crypto");
    const { readFileSync } = await import("node:fs");
    const competitionCatalog = JSON.parse(
      readFileSync("web/content/competition/catalog.json", "utf8"),
    ) as {
      games: { gameKey: string; manifestPath: string }[];
    };
    const competitionGameArtifacts: CompetitionArtifact[] =
      competitionCatalog.games.map((entry) => {
        const manifest = JSON.parse(
          readFileSync(entry.manifestPath, "utf8"),
        ) as {
          competitionId: string;
          gameVersion: string;
          artifactPath: string;
          artifactS3Key: string;
          artifactSha256: string;
        };
        const gameKey = `${manifest.competitionId}#${manifest.gameVersion}`;
        if (gameKey !== entry.gameKey) {
          throw new Error(
            `Competition catalog key mismatch for ${entry.manifestPath}`,
          );
        }
        const actualSha256 = createHash("sha256")
          .update(readFileSync(manifest.artifactPath))
          .digest("hex");
        if (actualSha256 !== manifest.artifactSha256) {
          throw new Error(`Competition artifact hash mismatch for ${gameKey}`);
        }
        return {
          gameKey,
          artifactPath: manifest.artifactPath,
          artifactS3Key: manifest.artifactS3Key,
          artifactSha256: manifest.artifactSha256,
        };
      });
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
    for (const game of competitionGameArtifacts) {
      const legacyName =
        game.gameKey === "global#2026-08-v1"
          ? "GlobalCompetitionGame202608V1"
          : null;
      const resourceName =
        legacyName ??
        `CompetitionGame${createHash("sha256").update(game.gameKey).digest("hex").slice(0, 12)}`;
      new aws.s3.BucketObjectv2(
        resourceName,
        {
          bucket: competitionArtifacts.name,
          key: game.artifactS3Key,
          contentType: "application/json",
          metadata: {
            "drop7-artifact-sha256": game.artifactSha256,
          },
          source: $asset(game.artifactPath),
        },
        { dependsOn: [competitionArtifacts] },
      );
    }
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
