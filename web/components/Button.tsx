/**
 * The site's button. `href` renders a Link, otherwise a <button>. One
 * `primary` per page (the accent fill); `secondary` is the raised outline;
 * `ghost` is text only. Styled by the `.button*` block in globals.css: hover
 * is a brightness lift under (hover: hover) and nothing under reduced motion.
 */
import Link from "next/link";
import type { AnchorHTMLAttributes, ButtonHTMLAttributes, ReactNode } from "react";

export type ButtonVariant = "primary" | "secondary" | "ghost";

interface ButtonBase {
  variant?: ButtonVariant;
  /** A small leading glyph, e.g. `<DiscFace cell={7} />`. */
  icon?: ReactNode;
  className?: string;
  children?: ReactNode;
}

export type ButtonLinkProps = ButtonBase & { href: string } & Omit<
    AnchorHTMLAttributes<HTMLAnchorElement>,
    "href" | "className" | "children"
  >;

export type ButtonButtonProps = ButtonBase & { href?: undefined } & Omit<
    ButtonHTMLAttributes<HTMLButtonElement>,
    "className" | "children"
  >;

export type ButtonProps = ButtonLinkProps | ButtonButtonProps;

function classes(variant: ButtonVariant, icon: ReactNode, className?: string): string {
  return ["button", `button--${variant}`, icon ? "button--with-icon" : "", className ?? ""]
    .filter(Boolean)
    .join(" ");
}

function Content({ icon, children }: { icon?: ReactNode; children?: ReactNode }) {
  return (
    <>
      {icon && (
        <span className="button-icon" aria-hidden="true">
          {icon}
        </span>
      )}
      {children}
    </>
  );
}

export function Button(props: ButtonProps) {
  if (props.href !== undefined) {
    const { href, variant = "primary", icon, className, children, ...anchor } = props;
    return (
      <Link href={href} className={classes(variant, icon, className)} {...anchor}>
        <Content icon={icon}>{children}</Content>
      </Link>
    );
  }
  const { variant = "primary", icon, className, children, type, ...button } = props;
  return (
    <button type={type ?? "button"} className={classes(variant, icon, className)} {...button}>
      <Content icon={icon}>{children}</Content>
    </button>
  );
}
