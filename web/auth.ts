import NextAuth from "next-auth";
import GitHub from "next-auth/providers/github";
import { loadGithubAuthCredentials } from "@/lib/auth/github-secret";

export const { handlers, auth, signIn, signOut } = NextAuth(async () => {
  const credentials = await loadGithubAuthCredentials();
  return {
    secret: credentials.authSecret,
    trustHost: true,
    session: { strategy: "jwt", maxAge: 30 * 24 * 60 * 60 },
    providers: [
      GitHub({
        clientId: credentials.clientId,
        clientSecret: credentials.clientSecret,
        authorization: { params: { scope: "read:user" } },
      }),
    ],
    callbacks: {
      async jwt({ token, account, profile }) {
        if (account?.provider === "github") {
          token.identityProvider = account.provider;
          token.providerAccountId = account.providerAccountId;
          const login = (profile as { login?: unknown } | undefined)?.login;
          token.handle = typeof login === "string" ? login : token.name;
        }
        return token;
      },
      async session({ session, token }) {
        if (session.user) {
          const provider =
            typeof token.identityProvider === "string"
              ? token.identityProvider
              : "";
          const providerAccountId =
            typeof token.providerAccountId === "string"
              ? token.providerAccountId
              : "";
          session.user.id =
            provider && providerAccountId
              ? `${provider}:${providerAccountId}`
              : "";
          session.user.provider = provider;
          session.user.providerAccountId = providerAccountId;
          session.user.handle = String(token.handle ?? session.user.name ?? "player");
        }
        return session;
      },
    },
  };
});
