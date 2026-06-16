import { useEffect, useRef, useState } from "react";
import { ChevronRight } from "lucide-react";

export type CtxItem = {
  key: string;
  label?: string;
  onClick?: () => void;
  disabled?: boolean;
  sep?: boolean;
  children?: CtxItem[];
};

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
  const [openSub, setOpenSub] = useState<string | null>(null);

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
  const flip = left > window.innerWidth - 410;

  const run = (it: CtxItem) => {
    onClose();
    it.onClick?.();
  };

  return (
    <div
      ref={ref}
      className="ctx-menu"
      style={{ left, top }}
      onMouseDown={(e) => e.stopPropagation()}
      onContextMenu={(e) => e.preventDefault()}
    >
      {items.map((it) => {
        if (it.sep) return <div key={it.key} className="menu-sep" />;
        if (it.children) {
          return (
            <div
              key={it.key}
              className="menu-sub"
              onMouseEnter={() => setOpenSub(it.key)}
              onMouseLeave={() => setOpenSub((k) => (k === it.key ? null : k))}
            >
              <button className="menu-item" disabled={it.disabled}>
                <span className="mi-label">{it.label}</span>
                <ChevronRight size={13} className="mi-caret" />
              </button>
              {openSub === it.key && (
                <div className={`ctx-submenu ${flip ? "left" : "right"}`}>
                  {it.children.map((c) =>
                    c.sep ? (
                      <div key={c.key} className="menu-sep" />
                    ) : (
                      <button
                        key={c.key}
                        className="menu-item"
                        disabled={c.disabled}
                        onClick={() => run(c)}
                      >
                        <span className="mi-label">{c.label}</span>
                      </button>
                    ),
                  )}
                </div>
              )}
            </div>
          );
        }
        return (
          <button key={it.key} className="menu-item" disabled={it.disabled} onClick={() => run(it)}>
            <span className="mi-label">{it.label}</span>
          </button>
        );
      })}
    </div>
  );
}
