"use client";

import { useEffect } from "react";

/**
 * Opens every <details> ancestor of the element named by the URL hash, on
 * load and on hashchange, so a link to `#record-RS-…` reveals the accordion
 * it lives in. Mount once per article layout; renders nothing.
 */
export function OpenOnHash() {
  useEffect(() => {
    function reveal() {
      const id = decodeURIComponent(window.location.hash.slice(1));
      if (!id) return;
      const target = document.getElementById(id);
      if (!target) return;
      let opened = false;
      let node: HTMLDetailsElement | null = target.closest("details");
      while (node) {
        if (!node.open) {
          node.open = true;
          opened = true;
        }
        node = node.parentElement?.closest("details") ?? null;
      }
      if (opened) target.scrollIntoView();
    }
    reveal();
    window.addEventListener("hashchange", reveal);
    return () => window.removeEventListener("hashchange", reveal);
  }, []);
  return null;
}
