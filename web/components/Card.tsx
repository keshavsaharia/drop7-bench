/**
 * The card shell: an <article class="card"> on the surface colour with a
 * rule border. With `href` the whole card is one link. The `art` slot sits
 * above the body in a 16:9 well (technique art plays on hover / focus-within
 * through the `.tart` contract in globals.css). Styled by `.card*`.
 */
import Link from "next/link";
import type { ReactNode } from "react";

export interface CardProps {
  /** Wraps the whole card in a Link. */
  href?: string;
  /** Rendered in the 16:9 art well above the body. */
  art?: ReactNode;
  /** Mono label above the title. */
  eyebrow?: ReactNode;
  title?: ReactNode;
  /** Heading element for the title. */
  heading?: "h2" | "h3" | "h4";
  summary?: ReactNode;
  /** Bottom row, pinned to the end of the card. */
  foot?: ReactNode;
  /** Arrow glyph at the end of the foot row (on by default when `href` is set). */
  arrow?: boolean;
  className?: string;
  /** Sets `data-playing` for the art contract ("once" plays a single loop). */
  playing?: "once" | "loop";
  children?: ReactNode;
}

export function Card({
  href,
  art,
  eyebrow,
  title,
  heading = "h3",
  summary,
  foot,
  arrow,
  className,
  playing,
  children,
}: CardProps) {
  const Heading = heading;
  const showArrow = arrow ?? href !== undefined;
  const body = (
    <>
      {art !== undefined && <div className="card-art">{art}</div>}
      <div className="card-body">
        {eyebrow && <span className="label card-eyebrow">{eyebrow}</span>}
        {title !== undefined && <Heading className="card-title">{title}</Heading>}
        {summary !== undefined && <p className="card-summary">{summary}</p>}
        {children}
        {(foot !== undefined || showArrow) && (
          <div className="card-foot">
            <span>{foot}</span>
            {showArrow && (
              <span className="arrow" aria-hidden="true">
                →
              </span>
            )}
          </div>
        )}
      </div>
    </>
  );
  return (
    <article className={className ? `card ${className}` : "card"} data-playing={playing}>
      {href ? (
        <Link href={href} className="card-link">
          {body}
        </Link>
      ) : (
        body
      )}
    </article>
  );
}
