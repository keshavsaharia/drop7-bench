/**
 * Per-page metadata: the canonical URL and the Open Graph block.
 *
 * A page's `generateMetadata` already computes the one title and description
 * it wants. This turns those into the canonical link and the per-page Open
 * Graph text so the same three lines are not repeated in a dozen files.
 * Nothing here writes copy: every string comes from the caller, which read it
 * from the record or document the page is showing.
 *
 * The social image itself is not named here. Next resolves it from the
 * `opengraph-image` file beside the page, falling back to the site card at
 * `app/opengraph-image.tsx` for a route that has none.
 */
import type { Metadata } from "next";

/** The same source and default as the root layout's `metadataBase`. */
export const SITE_URL = process.env.DROP7_SITE_URL ?? "https://drop7.dev";

export interface PageMetadataInput {
  /** The page's own title, exactly as it already sets it. */
  title: string;
  /** The page's own description, when it has one; absent is left absent. */
  description?: string;
  /** The page's route path, e.g. "/approaches/fair-expectimax/reference". */
  path: string;
  /**
   * The social card to name explicitly. Only for a segment that cannot carry
   * an `opengraph-image` file of its own: Next refuses a route where a
   * catch-all is not the last part of the URL, so `/docs/[...slug]` has no
   * place to put one. Every other page leaves this unset and Next resolves
   * the `opengraph-image` file beside it.
   */
  image?: string;
}

/**
 * The metadata object a dynamic page returns: its title and description
 * unchanged, plus the canonical URL and the Open Graph and Twitter blocks
 * carrying the same words. Paths are resolved against `metadataBase`.
 */
export function pageMetadata({ title, description, path, image }: PageMetadataInput): Metadata {
  return {
    title,
    description,
    alternates: { canonical: path },
    openGraph: {
      title,
      description,
      type: "article",
      url: path,
      ...(image === undefined ? {} : { images: [image] }),
    },
    // The root layout sets a site-wide twitter title; without this the card
    // on X would carry the site's name where the page's belongs.
    twitter: {
      card: "summary_large_image",
      title,
      description,
    },
  };
}
