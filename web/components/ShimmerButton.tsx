import Link, { type LinkProps } from "next/link";
import type { AnchorHTMLAttributes, ReactNode } from "react";

type ShimmerButtonProps = LinkProps &
  Omit<AnchorHTMLAttributes<HTMLAnchorElement>, keyof LinkProps> & {
    icon?: ReactNode;
  };

/** Primary navigation CTA with the shared Drop7 blue shimmer treatment. */
export function ShimmerButton({
  children,
  className,
  icon,
  ...props
}: ShimmerButtonProps) {
  return (
    <Link
      {...props}
      className={[
        "shimmer-button",
        icon ? "shimmer-button-with-icon" : "",
        className ?? "",
      ]
        .filter(Boolean)
        .join(" ")}
    >
      {icon && (
        <span className="shimmer-button-icon" aria-hidden="true">
          {icon}
        </span>
      )}
      {children}
    </Link>
  );
}
