import "next-auth";
import "next-auth/jwt";

declare module "next-auth" {
  interface Session {
    user: {
      id: string;
      provider: string;
      providerAccountId: string;
      handle: string;
      name?: string | null;
      email?: string | null;
      image?: string | null;
    };
  }
}

declare module "next-auth/jwt" {
  interface JWT {
    identityProvider?: string;
    providerAccountId?: string;
    handle?: string | null;
  }
}
