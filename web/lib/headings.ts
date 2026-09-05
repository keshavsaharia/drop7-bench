/**
 * Heading extraction for the table of contents. `extractHeadings` reads a
 * Markdown or MDX source (frontmatter already stripped, or not: a leading
 * `---` block is skipped) and returns each heading's id, text and depth in
 * document order. Ids are produced with github-slugger exactly as
 * rehype-slug does in Mdx.tsx and Markdown.tsx, so a page can build its
 * `Toc` without rendering the document twice.
 *
 * Text is reduced the way hast-util-to-string sees the rendered heading:
 * markup markers removed, link and emphasis text kept, inline JSX tags
 * dropped, images dropped, entities decoded.
 */
import GithubSlugger from "github-slugger";

export interface Heading {
  id: string;
  text: string;
  depth: number;
}

const ATX = /^ {0,3}(#{1,6})[ \t]+(.*?)(?:[ \t]+#+)?[ \t]*$/;
const ATX_EMPTY = /^ {0,3}(#{1,6})[ \t]*$/;
const FENCE = /^ {0,3}(`{3,}|~{3,})/;
const SETEXT_H1 = /^ {0,3}=+[ \t]*$/;
const SETEXT_H2 = /^ {0,3}-+[ \t]*$/;
const PARAGRAPH_BREAKER = /^ {0,3}(?:[-*+]|\d+[.)])[ \t]|^ {0,3}(?:>|#{1,6}[ \t]|<|\{|\||`{3,}|~{3,})|^ {4}/;

const ENTITIES: Record<string, string> = {
  amp: "&",
  lt: "<",
  gt: ">",
  quot: '"',
  apos: "'",
  nbsp: " ",
  hellip: "…",
  mdash: "—",
  ndash: "–",
  times: "×",
};

function decodeEntities(text: string): string {
  return text.replace(/&(#x[0-9a-f]+|#\d+|[a-z]+);/gi, (match, body: string) => {
    if (body[0] === "#") {
      const code = body[1].toLowerCase() === "x" ? parseInt(body.slice(2), 16) : parseInt(body.slice(1), 10);
      return Number.isFinite(code) ? String.fromCodePoint(code) : match;
    }
    return ENTITIES[body.toLowerCase()] ?? match;
  });
}

/** Reduce heading markup to the text hast-util-to-string would return for it. */
export function headingText(raw: string): string {
  let text = raw;
  text = text.replace(/<\/?[A-Za-z][^>]*>/g, "");                 // inline JSX / HTML tags
  text = text.replace(/!\[[^\]]*\]\([^)]*\)/g, "");               // images
  text = text.replace(/\[([^\]]*)\]\([^)]*\)/g, "$1");            // links
  text = text.replace(/\[([^\]]*)\]\[[^\]]*\]/g, "$1");           // reference links
  text = text.replace(/`+([^`]*)`+/g, "$1");                      // inline code
  text = text.replace(/(\*\*|__)(.+?)\1/g, "$2");                 // strong
  text = text.replace(/(^|[^\\*_])(\*|_)(?!\s)(.+?)(?<!\s)\2/g, "$1$3"); // emphasis
  text = text.replace(/~~(.+?)~~/g, "$1");                        // strikethrough
  text = text.replace(/\\([\\`*_{}[\]()#+\-.!~<>|])/g, "$1");     // backslash escapes
  text = decodeEntities(text);
  return text.replace(/\s+/g, " ").trim();
}

export function extractHeadings(
  source: string,
  options: { minDepth?: number; maxDepth?: number } = {},
): Heading[] {
  const { minDepth = 1, maxDepth = 6 } = options;
  const slugger = new GithubSlugger();
  const lines = source.replace(/\r\n?/g, "\n").split("\n");
  const found: Heading[] = [];

  let index = 0;
  // Frontmatter: a leading `---` block.
  if (lines[0]?.trim() === "---") {
    const end = lines.findIndex((line, i) => i > 0 && line.trim() === "---");
    if (end > 0) index = end + 1;
  }

  let fence: string | null = null;
  let previous: string | null = null; // last non-blank line that could open a setext heading

  function push(depth: number, rawText: string) {
    const text = headingText(rawText);
    const id = slugger.slug(text); // every heading feeds the slugger, as rehype-slug does
    if (depth >= minDepth && depth <= maxDepth) found.push({ id, text, depth });
  }

  for (; index < lines.length; index += 1) {
    const line = lines[index];
    const fenceMatch = FENCE.exec(line);
    if (fence) {
      if (fenceMatch && fenceMatch[1][0] === fence[0] && fenceMatch[1].length >= fence.length && line.trim() === fenceMatch[1]) {
        fence = null;
      }
      previous = null;
      continue;
    }
    if (fenceMatch) {
      fence = fenceMatch[1];
      previous = null;
      continue;
    }

    const atx = ATX.exec(line);
    if (atx) {
      push(atx[1].length, atx[2]);
      previous = null;
      continue;
    }
    if (ATX_EMPTY.test(line)) {
      previous = null;
      continue;
    }

    if (previous !== null) {
      if (SETEXT_H1.test(line)) {
        push(1, previous);
        previous = null;
        continue;
      }
      if (SETEXT_H2.test(line)) {
        push(2, previous);
        previous = null;
        continue;
      }
    }

    if (line.trim() === "" || PARAGRAPH_BREAKER.test(line)) {
      previous = null;
    } else {
      previous = previous === null ? line : `${previous} ${line}`;
    }
  }

  return found;
}
