import { useEffect, useRef } from "react";

export type CtxItem = { key: string; label?: string; onClick?: () => void; disabled?: boolean; sep?: boolean };

export default function ContextMenu({
  x,
  y,
  items,
  onClose,
}: {
  x: number;
  y: number;
  items: CtxItem[];
  onClose: () => void;
}) {
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const close = () => onClose();
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    window.addEventListener("mousedown", close);
    window.addEventListener("resize", close);
    window.addEventListener("keydown", onKey);
    return () => {
      window.removeEventListener("mousedown", close);
      window.removeEventListener("resize", close);
      window.removeEventListener("keydown", onKey);
    };
  }, [onClose]);

  // Keep the menu inside the viewport.
  const left = Math.min(x, window.innerWidth - 200);
  const top = Math.min(y, window.innerHeight - items.length * 30 - 8);

  return (
    <div
      ref={ref}
      className="ctx-menu"
      style={{ left, top }}
      onMouseDown={(e) => e.stopPropagation()}
      onContextMenu={(e) => e.preventDefault()}
    >
      {items.map((it) =>
        it.sep ? (
          <div key={it.key} className="menu-sep" />
        ) : (
          <button
            key={it.key}
            className="menu-item"
            disabled={it.disabled}
            onClick={() => {
              onClose();
              it.onClick?.();
            }}
          >
            <span className="mi-label">{it.label}</span>
          </button>
        ),
      )}
    </div>
  );
}
