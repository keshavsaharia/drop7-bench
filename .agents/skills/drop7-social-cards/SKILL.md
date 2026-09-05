---
name: drop7-social-cards
description: Add or change the link preview (OpenGraph and Twitter card) for a page of the Drop7 research console, and generate raster artwork for the site through the Codex CLI. Read this before touching page metadata, opengraph-image files, the sitemap, or any image asset under web/public.
---

# Link previews and images for the console

A link to this site should say what the page is, not only what the site is.
Every route therefore renders its own 1200x630 card from the page's own
title, summary and labels, and any route without one falls back to the site
card. This file is how to add, change and check one, and how to produce a
raster image when a drawing is genuinely the right answer.

## How it fits together

| Piece | File | What it does |
| --- | --- | --- |
| The card template | `web/lib/social-card.tsx` | `renderPageCard({ eyebrow, title, summary, labels, path })` draws the shared card: brand mark, eyebrow, title, summary, up to three chips, the page path, and a Drop7 board seeded from that path |
| The site card | `web/lib/social-image.tsx` | the hand-built home card, used by `app/opengraph-image.tsx` and `app/twitter-image.tsx` and inherited by any route with no card of its own |
| A page's card | `app/<route>/opengraph-image.tsx` | reads the page's own data and calls `renderPageCard` |
| A page's text | the page's `generateMetadata` | title, description, canonical URL and the `openGraph` block |
| Discovery | `app/sitemap.ts`, `app/robots.ts` | every real route, and what a crawler may read |

Twitter reads `og:image` when a page has no `twitter:image` of its own, so a
route needs only `opengraph-image.tsx`. The root keeps both because the site
card is the one most often fetched.

## Adding a card to a new route

1. Write `opengraph-image.tsx` beside the route's `page.tsx`. It exports
   `alt`, `size`, `contentType` and a default component, and receives the
   same `params` the page does.

   ```tsx
   import { cardAlt, renderPageCard, SOCIAL_CONTENT_TYPE, SOCIAL_SIZE } from "@/lib/social-card";

   export const size = SOCIAL_SIZE;
   export const contentType = SOCIAL_CONTENT_TYPE;
   export const alt = cardAlt({ eyebrow: "Approach", title: "Drop7 Research" });

   export default async function Image({ params }: { params: Promise<{ slug: string }> }) {
     const { slug } = await params;
     const page = loadThing(slug);
     return renderPageCard({
       eyebrow: "Approach",
       title: page?.title ?? slug,
       summary: page?.summary,
       labels: [page?.status, page?.evidence].filter(Boolean) as string[],
       path: `/things/${slug}`,
     });
   }
   ```

2. Give the page the matching text in `generateMetadata`, through the helper
   in `web/lib/metadata.ts`, so the card and the crawler agree.

3. Add the route to `app/sitemap.ts` if it is a new kind of route rather than
   a new instance of an existing one.

Rules the card inherits from the rest of the console:

- **Every value on a card comes from the page it describes.** Nothing is
  counted, derived, rounded or inferred, and no number appears that is not
  already in a record. A card is a view, not a summary.
- **A missing record is not an error.** The site must render on a checkout
  with no `research/` and no `web/data/`, so a card falls back to the id or
  the slug rather than throwing.
- **A chip is drawn only for a label the record actually carries.** Never
  default a status, a tier or an outcome.
- The `path` argument is both the text in the card's foot and the seed for
  the board motif, so passing the real route gives each page its own board.
- Satori cannot read a CSS variable, so `social-card.tsx` repeats the palette
  as literals. It is one of the few files `scripts/check-tokens.mjs` allows to
  hold a colour. If a token changes in `globals.css`, change it there too.

## Checking a card

From `web/`, with the dev server running:

```bash
curl -sI http://localhost:7777/approaches/fair-expectimax/reference/opengraph-image
curl -s  http://localhost:7777/approaches/fair-expectimax/reference/opengraph-image -o /tmp/card.png
```

Then look at the PNG. Check that the title is not clipped, that the summary
reads as a sentence rather than a truncated clause, and that the chips are
the labels the page itself shows. `renderPageCard` steps the title size down
as the title grows and trims the summary on a word boundary, but a very long
title still needs a shorter one written for it.

To see how a preview will look elsewhere, the page source is the ground
truth: `curl -s <url> | grep -o '<meta property="og:[^>]*>'`.

## Generating a raster image

Most artwork here is inline SVG drawn by hand, because it animates, reads a
design token and has to stay honest about the research. Reach for a raster
only where a drawing is the point: an illustration on a learn page, a texture,
a background for a share card.

The user is signed in to the Codex CLI with `image_generation` enabled, so one
command produces a PNG:

```bash
web/scripts/gen-image.sh web/public/art/<name>.png "<prompt>"
```

A run takes a minute or two and spends the user's quota. **Look at the result
before regenerating, and stop after about three attempts per image.** The
script prints the size, format and whether the image carries alpha.

### The house prompt

Always state the ground, the palette and the geometry, then the subject:

> Flat vector illustration on a near-black ground `#0a0a0c`, technical
> diagram style, thin 1px rules in dark gray `#26262b`, no gradients except a
> single soft blue glow, no photographic texture, no text, no drop shadows.
> Accent colour is a soft periwinkle blue `#8fb0ff`. Any discs are flat
> circles in this exact palette: green `#218a57`, yellow `#d7b33f`, orange
> `#d7742e`, red `#c4443e`, purple `#9e4c8b`, teal `#238391`, blue `#405db0`.

Then the composition, stated exactly: the aspect ratio, what occupies each
region, and what must not appear. Say **no text** unless a word is genuinely
part of the drawing, because generated lettering is almost always wrong.

### After generating

1. Look at the PNG. Check the palette against the `@theme static` block in
   `web/app/globals.css`, and check that nothing was invented: no fake
   numbers, no fake chart, no lettering.
2. Put it under `web/public/art/` and reference it by its public path.
3. Compress it: `npx sharp-cli --input <file> --output <file> png --quality 80`,
   or add a step to `web/scripts/generate-brand-icons.mjs` if it is a brand
   asset with several sizes.
4. Give it real alt text wherever it is used. A decorative background gets
   `alt=""` and `aria-hidden`, never a description of the decoration.

## Before you finish

From `web/`:

```bash
npx tsc --noEmit
npm run lint
node scripts/check-tokens.mjs
npm run build
```

Then fetch the affected `opengraph-image` routes and look at them. A card
that compiles and renders a clipped title is not finished.
