interface AnalyticsUser {
  provider?: string | null;
  handle?: string | null;
}

export function isAnalyticsAdmin(
  user: AnalyticsUser | null | undefined,
  configuredUsername = process.env.ADMIN_GITHUB_USERNAME,
): boolean {
  const expected = configuredUsername?.trim().toLowerCase();
  const actual = user?.handle?.trim().toLowerCase();
  return Boolean(expected && actual && user?.provider === "github" && actual === expected);
}
