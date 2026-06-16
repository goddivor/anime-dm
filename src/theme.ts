import type { Theme } from "./types";

// Applies a theme by setting data-theme on the document root. In "system" mode
// it tracks the OS preference live; switching to a fixed theme drops the watcher.
let media: MediaQueryList | null = null;
let watcher: (() => void) | null = null;

export function applyTheme(theme: Theme) {
  const root = document.documentElement;
  if (media && watcher) {
    media.removeEventListener("change", watcher);
    media = null;
    watcher = null;
  }
  if (theme === "system") {
    media = window.matchMedia("(prefers-color-scheme: light)");
    const resolve = () => {
      root.dataset.theme = media && media.matches ? "light" : "dark";
    };
    resolve();
    watcher = resolve;
    media.addEventListener("change", resolve);
  } else {
    root.dataset.theme = theme;
  }
}

export const isTheme = (v: string): v is Theme =>
  v === "dark" || v === "light" || v === "system";
