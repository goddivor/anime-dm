import { useEffect, useState, type ReactNode } from "react";
import { fetchImage } from "../api";

const cache = new Map<string, string>();

export default function Poster({
  url,
  referer,
  className,
  fallback,
}: {
  url?: string | null;
  referer?: string;
  className?: string;
  fallback: ReactNode;
}) {
  const [src, setSrc] = useState<string | undefined>(url ? cache.get(url) : undefined);

  useEffect(() => {
    if (!url) {
      setSrc(undefined);
      return;
    }
    const cached = cache.get(url);
    if (cached) {
      setSrc(cached);
      return;
    }
    let alive = true;
    const ref = referer ? safeOrigin(referer) : undefined;
    fetchImage(url, ref)
      .then((d) => {
        cache.set(url, d);
        if (alive) setSrc(d);
      })
      .catch(() => {});
    return () => {
      alive = false;
    };
  }, [url, referer]);

  if (src) return <img className={className} src={src} alt="" />;
  return <>{fallback}</>;
}

function safeOrigin(u: string): string | undefined {
  try {
    return new URL(u).origin + "/";
  } catch {
    return undefined;
  }
}
