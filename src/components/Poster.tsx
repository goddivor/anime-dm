import { useEffect, useState, type ReactNode } from "react";
import { ZoomIn } from "lucide-react";
import { fetchImage } from "../api";

const cache = new Map<string, string>();

export default function Poster({
  url,
  referer,
  className,
  fallback,
  zoomable,
}: {
  url?: string | null;
  referer?: string;
  className?: string;
  fallback: ReactNode;
  zoomable?: boolean;
}) {
  const [src, setSrc] = useState<string | undefined>(url ? cache.get(url) : undefined);
  const [zoom, setZoom] = useState(false);

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

  if (!src) return <>{fallback}</>;
  if (!zoomable) return <img className={className} src={src} alt="" />;

  return (
    <>
      <span className="poster-zoom">
        <img className={className} src={src} alt="" />
        <button className="poster-zoom-btn" onClick={() => setZoom(true)} title="">
          <ZoomIn size={20} />
        </button>
      </span>
      {zoom && (
        <div className="lightbox" onClick={() => setZoom(false)}>
          <img src={src} alt="" />
        </div>
      )}
    </>
  );
}

function safeOrigin(u: string): string | undefined {
  try {
    return new URL(u).origin + "/";
  } catch {
    return undefined;
  }
}
