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
    analyticsBucket: "drop7-prod-analytics",
    submissionsBucket: "drop7-prod-game-submissions",
  },
  dev: {
    domain: "dev.drop7.dev",
    bucket: "drop7-dev-public",
    ledgerTable: "drop7-dev-competition-ledger",
    artifactBucket: "drop7-dev-competition-artifacts",
    analyticsBucket: "drop7-dev-analytics",
    submissionsBucket: "drop7-dev-game-submissions",
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
    const analyticsCatalogArn = caller.accountId.apply(
      (accountId) => `arn:aws:glue:us-east-1:${accountId}:catalog`,
    );
    const analyticsDatabaseName = `drop7_${$app.stage.replace(/-/g, "_")}_analytics`;
    const analyticsTableName = "page_views";
    const analyticsLake = new sst.aws.Bucket("AnalyticsLake", {
      lifecycle: [
        {
          id: "expire-athena-query-results",
          prefix: "athena-results/",
          expiresIn: "7 days",
        },
        {
          id: "expire-firehose-errors",
          prefix: "firehose-errors/",
          expiresIn: "30 days",
        },
      ],
      transform: {
        bucket: deployment ? { bucket: deployment.analyticsBucket } : undefined,
      },
    });
    const gameSubmissionLake = new sst.aws.Bucket("GameSubmissionLake", {
      versioning: true,
      lifecycle: [
        {
          id: "expire-submission-firehose-errors",
          prefix: "firehose-errors/",
          expiresIn: "30 days",
        },
      ],
      transform: {
        bucket: deployment ? { bucket: deployment.submissionsBucket } : undefined,
      },
    });
    const analyticsDatabase = new aws.glue.CatalogDatabase(
      "AnalyticsDatabase",
      {
        name: analyticsDatabaseName,
        description: `Drop7 ${$app.stage} first-party page-view analytics`,
      },
    );
    const analyticsTable = new aws.glue.CatalogTable("AnalyticsPageViews", {
      name: analyticsTableName,
      databaseName: analyticsDatabase.name,
      description:
        "Append-only, server-side page-view events delivered by Amazon Data Firehose",
      tableType: "EXTERNAL_TABLE",
      parameters: {
        // Glue owns reserved Iceberg parameters when openTableFormatInput creates the table.
        format: "parquet",
      },
      openTableFormatInput: {
        icebergInput: {
          metadataOperation: "CREATE",
          version: "2",
        },
      },
      storageDescriptor: {
        location: analyticsLake.name.apply(
          (bucketName) => `s3://${bucketName}/warehouse/page_views/`,
        ),
        columns: [
          { name: "event_id", type: "string" },
          { name: "event_name", type: "string" },
          { name: "schema_version", type: "int" },
          { name: "occurred_at", type: "string" },
          { name: "occurred_at_ms", type: "bigint" },
          { name: "path", type: "string" },
          { name: "host", type: "string" },
          { name: "referrer_host", type: "string" },
          { name: "referrer_channel", type: "string" },
          { name: "user_agent", type: "string" },
          { name: "accept_language", type: "string" },
          { name: "country_code", type: "string" },
          { name: "region_code", type: "string" },
          { name: "city", type: "string" },
          { name: "device_type", type: "string" },
          { name: "browser_family", type: "string" },
          { name: "os_family", type: "string" },
          { name: "is_bot", type: "boolean" },
          { name: "visitor_id", type: "string" },
          { name: "stage", type: "string" },
        ],
      },
    });
    const gameSubmissionsTable = new aws.glue.CatalogTable(
      "AnalyticsGameSubmissions",
      {
        name: "game_submissions",
        databaseName: analyticsDatabase.name,
        description: "Server-validated completed mobile games delivered by Amazon Data Firehose",
        tableType: "EXTERNAL_TABLE",
        parameters: {
          EXTERNAL: "TRUE",
          classification: "json",
        },
        storageDescriptor: {
          location: gameSubmissionLake.name.apply(
            (bucketName) => `s3://${bucketName}/game-submissions/`,
          ),
          inputFormat: "org.apache.hadoop.mapred.TextInputFormat",
          outputFormat: "org.apache.hadoop.hive.ql.io.HiveIgnoreKeyTextOutputFormat",
          serDeInfo: {
            serializationLibrary: "org.openx.data.jsonserde.JsonSerDe",
          },
          columns: [
            { name: "event_id", type: "string" },
            { name: "event_name", type: "string" },
            { name: "schema_version", type: "int" },
            { name: "received_at", type: "string" },
            { name: "received_at_ms", type: "bigint" },
            { name: "game_id", type: "string" },
            { name: "started_at", type: "string" },
            { name: "completed_at", type: "string" },
            { name: "source_application", type: "string" },
            { name: "source_platform", type: "string" },
            { name: "app_version", type: "string" },
            { name: "mode", type: "string" },
            { name: "ruleset", type: "string" },
            { name: "verified_score", type: "bigint" },
            { name: "verified_level", type: "int" },
            { name: "verified_moves", type: "int" },
            { name: "tape_json", type: "string" },
            { name: "stage", type: "string" },
          ],
        },
      },
    );
    const optimizerRole = new aws.iam.Role("AnalyticsIcebergOptimizerRole", {
      assumeRolePolicy: aws.iam.getPolicyDocumentOutput({
        statements: [
          {
            effect: "Allow",
            actions: ["sts:AssumeRole"],
            principals: [
              { type: "Service", identifiers: ["glue.amazonaws.com"] },
            ],
          },
        ],
      }).json,
    });
    const analyticsLakeObjectsArn = analyticsLake.arn.apply((arn) => `${arn}/*`);
    const optimizerLogArn = caller.accountId.apply(
      (accountId) =>
        `arn:aws:logs:us-east-1:${accountId}:log-group:/aws-glue/iceberg-*:*`,
    );
    const optimizerRolePolicy = new aws.iam.RolePolicy(
      "AnalyticsIcebergOptimizerRolePolicy",
      {
        role: optimizerRole.id,
        policy: aws.iam.getPolicyDocumentOutput({
          statements: [
            {
              sid: "MaintainIcebergObjects",
              effect: "Allow",
              actions: ["s3:GetObject", "s3:PutObject", "s3:DeleteObject"],
              resources: [analyticsLakeObjectsArn],
            },
            {
              sid: "ListIcebergBucket",
              effect: "Allow",
              actions: ["s3:GetBucketLocation", "s3:ListBucket"],
              resources: [analyticsLake.arn],
            },
            {
              sid: "MaintainIcebergMetadata",
              effect: "Allow",
              actions: ["glue:GetTable", "glue:UpdateTable"],
              resources: [
                analyticsCatalogArn,
                analyticsDatabase.arn,
                analyticsTable.arn,
              ],
            },
            {
              sid: "WriteOptimizerLogs",
              effect: "Allow",
              actions: [
                "logs:CreateLogGroup",
                "logs:CreateLogStream",
                "logs:PutLogEvents",
              ],
              resources: [optimizerLogArn],
            },
            {
              sid: "WriteOptimizerMetrics",
              effect: "Allow",
              actions: ["cloudwatch:PutMetricData"],
              resources: ["*"],
            },
          ],
        }).json,
      },
    );
    new aws.glue.CatalogTableOptimizer(
      "AnalyticsIcebergCompaction",
      {
        catalogId: caller.accountId,
        databaseName: analyticsDatabase.name,
        tableName: analyticsTable.name,
        type: "compaction",
        configuration: { enabled: true, roleArn: optimizerRole.arn },
      },
      { dependsOn: [analyticsTable, optimizerRolePolicy] },
    );
    new aws.glue.CatalogTableOptimizer(
      "AnalyticsIcebergSnapshotRetention",
      {
        catalogId: caller.accountId,
        databaseName: analyticsDatabase.name,
        tableName: analyticsTable.name,
        type: "retention",
        configuration: {
          enabled: true,
          roleArn: optimizerRole.arn,
          retentionConfiguration: {
            icebergConfiguration: {
              snapshotRetentionPeriodInDays: 7,
              numberOfSnapshotsToRetain: 10,
              cleanExpiredFiles: true,
              runRateInHours: 24,
            },
          },
        },
      },
      { dependsOn: [analyticsTable, optimizerRolePolicy] },
    );
    new aws.glue.CatalogTableOptimizer(
      "AnalyticsIcebergOrphanCleanup",
      {
        catalogId: caller.accountId,
        databaseName: analyticsDatabase.name,
        tableName: analyticsTable.name,
        type: "orphan_file_deletion",
        configuration: {
          enabled: true,
          roleArn: optimizerRole.arn,
          orphanFileDeletionConfiguration: {
            icebergConfiguration: {
              location: analyticsLake.name.apply(
                (bucketName) => `s3://${bucketName}/warehouse/page_views/`,
              ),
              orphanFileRetentionPeriodInDays: 7,
              runRateInHours: 24,
            },
          },
        },
      },
      { dependsOn: [analyticsTable, optimizerRolePolicy] },
    );
    const firehoseLogGroup = new aws.cloudwatch.LogGroup(
      "AnalyticsFirehoseLogGroup",
      {
        name: `/aws/kinesisfirehose/drop7-${$app.stage}-page-views`,
        retentionInDays: 30,
      },
    );
    const firehoseLogStream = new aws.cloudwatch.LogStream(
      "AnalyticsFirehoseLogStream",
      {
        name: "IcebergDelivery",
        logGroupName: firehoseLogGroup.name,
      },
    );
    const firehoseRole = new aws.iam.Role("AnalyticsFirehoseRole", {
      assumeRolePolicy: aws.iam.getPolicyDocumentOutput({
        statements: [
          {
            effect: "Allow",
            actions: ["sts:AssumeRole"],
            principals: [
              { type: "Service", identifiers: ["firehose.amazonaws.com"] },
            ],
          },
        ],
      }).json,
    });
    const firehoseRolePolicy = new aws.iam.RolePolicy(
      "AnalyticsFirehoseRolePolicy",
      {
        role: firehoseRole.id,
        policy: aws.iam.getPolicyDocumentOutput({
          statements: [
            {
              sid: "WriteIcebergData",
              effect: "Allow",
              actions: [
                "s3:AbortMultipartUpload",
                "s3:GetBucketLocation",
                "s3:GetObject",
                "s3:ListBucket",
                "s3:ListBucketMultipartUploads",
                "s3:PutObject",
                "s3:DeleteObject",
              ],
              resources: [analyticsLake.arn, analyticsLakeObjectsArn],
            },
            {
              sid: "CommitIcebergMetadata",
              effect: "Allow",
              actions: [
                "glue:GetDatabase",
                "glue:GetTable",
                "glue:GetTableVersion",
                "glue:GetTableVersions",
                "glue:UpdateTable",
              ],
              resources: [
                analyticsCatalogArn,
                analyticsDatabase.arn,
                analyticsTable.arn,
              ],
            },
            {
              sid: "WriteDeliveryLogs",
              effect: "Allow",
              actions: ["logs:PutLogEvents"],
              resources: [firehoseLogStream.arn],
            },
          ],
        }).json,
      },
    );
    const analyticsFirehose = new aws.kinesis.FirehoseDeliveryStream(
      "AnalyticsFirehose",
      {
        name: `drop7-${$app.stage}-page-views`,
        destination: "iceberg",
        icebergConfiguration: {
          appendOnly: true,
          roleArn: firehoseRole.arn,
          catalogArn: analyticsCatalogArn,
          bufferingInterval: 300,
          bufferingSize: 64,
          retryDuration: 300,
          s3BackupMode: "FailedDataOnly",
          cloudwatchLoggingOptions: {
            enabled: true,
            logGroupName: firehoseLogGroup.name,
            logStreamName: firehoseLogStream.name,
          },
          s3Configuration: {
            roleArn: firehoseRole.arn,
            bucketArn: analyticsLake.arn,
            bufferingInterval: 300,
            bufferingSize: 5,
            compressionFormat: "GZIP",
            errorOutputPrefix:
              "firehose-errors/!{firehose:error-output-type}/year=!{timestamp:yyyy}/month=!{timestamp:MM}/day=!{timestamp:dd}/hour=!{timestamp:HH}/",
            cloudwatchLoggingOptions: {
              enabled: true,
              logGroupName: firehoseLogGroup.name,
              logStreamName: firehoseLogStream.name,
            },
          },
          destinationTableConfigurations: [
            {
              databaseName: analyticsDatabase.name,
              tableName: analyticsTable.name,
              s3ErrorOutputPrefix: "firehose-errors/page_views/",
            },
          ],
        },
      },
      { dependsOn: [analyticsTable, firehoseRolePolicy] },
    );
    const gameSubmissionFirehoseLogGroup = new aws.cloudwatch.LogGroup(
      "GameSubmissionFirehoseLogGroup",
      {
        name: `/aws/kinesisfirehose/drop7-${$app.stage}-game-submissions`,
        retentionInDays: 30,
      },
    );
    const gameSubmissionFirehoseLogStream = new aws.cloudwatch.LogStream(
      "GameSubmissionFirehoseLogStream",
      {
        name: "S3Delivery",
        logGroupName: gameSubmissionFirehoseLogGroup.name,
      },
    );
    const gameSubmissionFirehoseRole = new aws.iam.Role(
      "GameSubmissionFirehoseRole",
      {
        assumeRolePolicy: aws.iam.getPolicyDocumentOutput({
          statements: [
            {
              effect: "Allow",
              actions: ["sts:AssumeRole"],
              principals: [
                { type: "Service", identifiers: ["firehose.amazonaws.com"] },
              ],
            },
          ],
        }).json,
      },
    );
    const gameSubmissionObjectsArn = gameSubmissionLake.arn.apply(
      (arn) => `${arn}/*`,
    );
    const gameSubmissionFirehoseRolePolicy = new aws.iam.RolePolicy(
      "GameSubmissionFirehoseRolePolicy",
      {
        role: gameSubmissionFirehoseRole.id,
        policy: aws.iam.getPolicyDocumentOutput({
          statements: [
            {
              sid: "WriteValidatedGames",
              effect: "Allow",
              actions: [
                "s3:AbortMultipartUpload",
                "s3:GetBucketLocation",
                "s3:GetObject",
                "s3:ListBucket",
                "s3:ListBucketMultipartUploads",
                "s3:PutObject",
              ],
              resources: [gameSubmissionLake.arn, gameSubmissionObjectsArn],
            },
            {
              sid: "WriteDeliveryLogs",
              effect: "Allow",
              actions: ["logs:PutLogEvents"],
              resources: [gameSubmissionFirehoseLogStream.arn],
            },
          ],
        }).json,
      },
    );
    const gameSubmissionFirehose = new aws.kinesis.FirehoseDeliveryStream(
      "GameSubmissionFirehose",
      {
        name: `drop7-${$app.stage}-game-submissions`,
        destination: "extended_s3",
        extendedS3Configuration: {
          roleArn: gameSubmissionFirehoseRole.arn,
          bucketArn: gameSubmissionLake.arn,
          // Firehose's maximum time buffer is 15 minutes. Objects are grouped
          // under an hourly prefix, so every active hour is independently queryable.
          bufferingInterval: 900,
          bufferingSize: 128,
          compressionFormat: "GZIP",
          prefix:
            "game-submissions/year=!{timestamp:yyyy}/month=!{timestamp:MM}/day=!{timestamp:dd}/hour=!{timestamp:HH}/",
          errorOutputPrefix:
            "firehose-errors/!{firehose:error-output-type}/year=!{timestamp:yyyy}/month=!{timestamp:MM}/day=!{timestamp:dd}/hour=!{timestamp:HH}/",
          cloudwatchLoggingOptions: {
            enabled: true,
            logGroupName: gameSubmissionFirehoseLogGroup.name,
            logStreamName: gameSubmissionFirehoseLogStream.name,
          },
        },
      },
      { dependsOn: [gameSubmissionFirehoseRolePolicy] },
    );
    const analyticsWorkgroup = new aws.athena.Workgroup(
      "AnalyticsAthenaWorkgroup",
      {
        name: `drop7-${$app.stage}-analytics`,
        description: `Admin analytics queries for Drop7 ${$app.stage}`,
        forceDestroy: !isProduction,
        configuration: {
          bytesScannedCutoffPerQuery: 1_073_741_824,
          enforceWorkgroupConfiguration: true,
          publishCloudwatchMetricsEnabled: true,
          engineVersion: { selectedEngineVersion: "Athena engine version 3" },
          resultConfiguration: {
            outputLocation: analyticsLake.name.apply(
              (bucketName) => `s3://${bucketName}/athena-results/`,
            ),
            encryptionConfiguration: { encryptionOption: "SSE_S3" },
          },
        },
      },
    );
    const athenaResultsArn = analyticsLake.arn.apply(
      (arn) => `${arn}/athena-results/*`,
    );

    const site = new sst.aws.Nextjs("ResearchWeb", {
      path: "web",
      domain: deployment
        ? {
            name: deployment.domain,
            dns: sst.aws.dns({ zone: DROP7_HOSTED_ZONE_ID }),
          }
        : undefined,
      edge: {
        viewerRequest: {
          injection:
            'event.request.headers["x-drop7-viewer-address"] = { value: event.viewer.ip };',
        },
      },
      environment: {
        DROP7_STAGE: $app.stage,
        DROP7_SITE_URL: deployment?.domain
          ? "https://" + deployment.domain
          : "http://localhost:3000",
        DROP7_COMPETITION_TABLE: competitionLedger.name,
        DROP7_COMPETITION_ARTIFACT_BUCKET: competitionArtifacts.name,
        DROP7_GITHUB_SECRET_NAME: "drop7-prod-github",
        DROP7_ANALYTICS_FIREHOSE_STREAM: analyticsFirehose.name,
        DROP7_ANALYTICS_DATABASE: analyticsDatabase.name,
        DROP7_ANALYTICS_TABLE: analyticsTable.name,
        DROP7_ANALYTICS_ATHENA_WORKGROUP: analyticsWorkgroup.name,
        DROP7_GAME_SUBMISSIONS_FIREHOSE_STREAM: gameSubmissionFirehose.name,
        DROP7_GAME_SUBMISSIONS_TABLE: gameSubmissionsTable.name,
        ADMIN_GITHUB_USERNAME: process.env.ADMIN_GITHUB_USERNAME?.trim() ?? "",
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
        {
          actions: ["firehose:PutRecord"],
          resources: [analyticsFirehose.arn, gameSubmissionFirehose.arn],
        },
        {
          actions: [
            "athena:GetQueryExecution",
            "athena:GetQueryResults",
            "athena:GetWorkGroup",
            "athena:StartQueryExecution",
            "athena:StopQueryExecution",
          ],
          resources: [analyticsWorkgroup.arn],
        },
        {
          actions: [
            "glue:BatchGetPartition",
            "glue:GetDatabase",
            "glue:GetPartition",
            "glue:GetPartitions",
            "glue:GetTable",
            "glue:GetTableVersion",
            "glue:GetTableVersions",
          ],
          resources: [
            analyticsCatalogArn,
            analyticsDatabase.arn,
            analyticsTable.arn,
            gameSubmissionsTable.arn,
          ],
        },
        {
          actions: [
            "s3:GetBucketLocation",
            "s3:ListBucket",
            "s3:ListBucketMultipartUploads",
          ],
          resources: [analyticsLake.arn, gameSubmissionLake.arn],
        },
        {
          actions: ["s3:GetObject"],
          resources: [analyticsLakeObjectsArn, gameSubmissionObjectsArn],
        },
        {
          actions: ["s3:AbortMultipartUpload", "s3:PutObject"],
          resources: [athenaResultsArn],
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
      AnalyticsBucket: analyticsLake.name,
      GameSubmissionBucket: gameSubmissionLake.name,
      GameSubmissionFirehose: gameSubmissionFirehose.name,
      GameSubmissionsTable: gameSubmissionsTable.name,
      AnalyticsFirehose: analyticsFirehose.name,
      AnalyticsDatabase: analyticsDatabase.name,
      AnalyticsTable: analyticsTable.name,
      AnalyticsAthenaWorkgroup: analyticsWorkgroup.name,
    };
  },
});
