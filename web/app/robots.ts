/**
 * robots.txt. Everything on the console is public and indexable except the
 * API routes and the source browser, which is a second rendering of files
 * that are already in the repository.
 */
import type { MetadataRoute } from "next";
import { SITE_URL } from "@/lib/metadata";

export default function robots(): MetadataRoute.Robots {
  return {
    rules: {
      userAgent: "*",
      allow: "/",
      disallow: ["/api/", "/share/", "/src/"],
    },
    sitemap: new URL("/sitemap.xml", SITE_URL).toString(),
  };
}
