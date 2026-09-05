import { Button, type ButtonLinkProps } from "./Button";

/**
 * @deprecated The shimmer sweep is gone; this is the primary `Button`, kept
 * so existing call sites compile. Import `Button` from "./Button" instead.
 */
export function ShimmerButton(props: Omit<ButtonLinkProps, "variant">) {
  return <Button variant="primary" {...props} />;
}
