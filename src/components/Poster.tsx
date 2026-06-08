import { useEffect, useState, type ReactNode } from "react";
import { fetchImage } from "../api";

const cache = new Map<string, string>();

export default function Poster({
  url,
  className,
  fallback,
}: {
  url?: string | null;
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
    fetchImage(url)
      .then((d) => {
        cache.set(url, d);
        if (alive) setSrc(d);
      })
      .catch(() => {});
    return () => {
      alive = false;
    };
  }, [url]);

  if (src) return <img className={className} src={src} alt="" />;
  return <>{fallback}</>;
}
