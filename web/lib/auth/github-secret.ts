import "server-only";

import {
  GetSecretValueCommand,
  SecretsManagerClient,
} from "@aws-sdk/client-secrets-manager";

export interface GithubAuthCredentials {
  clientId: string;
  clientSecret: string;
  authSecret: string;
}

interface GithubSecretDocument {
  GITHUB_CLIENT_ID?: string;
  GITHUB_CLIENT_SECRET?: string;
  GITHUB_DEV_CLIENT_ID?: string;
  GITHUB_DEV_CLIENT_SECRET?: string;
  AUTH_SECRET?: string;
}

let credentialsPromise: Promise<GithubAuthCredentials> | undefined;

export function loadGithubAuthCredentials(): Promise<GithubAuthCredentials> {
  credentialsPromise ??= loadCredentials();
  return credentialsPromise;
}

async function loadCredentials(): Promise<GithubAuthCredentials> {
  const localClientId = process.env.GITHUB_CLIENT_ID;
  const localClientSecret = process.env.GITHUB_CLIENT_SECRET;
  if (usable(localClientId) && usable(localClientSecret)) {
    return {
      clientId: localClientId,
      clientSecret: localClientSecret,
      authSecret:
        process.env.AUTH_SECRET ??
        "drop7-local-auth-secret-not-for-production-2026",
    };
  }

  const secretName = process.env.DROP7_GITHUB_SECRET_NAME;
  if (!secretName) {
    throw new Error(
      "GitHub authentication is not configured: DROP7_GITHUB_SECRET_NAME is absent",
    );
  }

  const response = await new SecretsManagerClient({}).send(
    new GetSecretValueCommand({ SecretId: secretName }),
  );
  if (!response.SecretString) {
    throw new Error(`Secrets Manager secret ${secretName} has no SecretString`);
  }
  const document = JSON.parse(response.SecretString) as GithubSecretDocument;
  const stage = process.env.DROP7_STAGE;
  const clientId =
    stage === "dev" ? document.GITHUB_DEV_CLIENT_ID : document.GITHUB_CLIENT_ID;
  const clientSecret =
    stage === "dev"
      ? document.GITHUB_DEV_CLIENT_SECRET
      : document.GITHUB_CLIENT_SECRET;

  if (!usable(clientId) || !usable(clientSecret) || !usable(document.AUTH_SECRET)) {
    throw new Error(
      stage === "dev"
        ? `Secrets Manager secret ${secretName} still has placeholder dev OAuth values`
        : `Secrets Manager secret ${secretName} still has placeholder OAuth values`,
    );
  }
  return { clientId, clientSecret, authSecret: document.AUTH_SECRET };
}

function usable(value: string | undefined): value is string {
  return Boolean(value && !value.startsWith("REPLACE_") && value.length >= 8);
}
