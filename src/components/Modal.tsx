import {
  useEffect,
  useRef,
  useState,
  type MouseEvent as ReactMouseEvent,
  type ReactNode,
} from "react";

// A draggable modal closable only by Escape or its close button — never by an
// outside click. All app dialogs build on this so the drag + Escape behaviour
// lives in one place. `closeOnEsc` lets a host suppress Escape-to-close while
// it owns a nested overlay (e.g. an open context menu).
export default function Modal({
  title,
  onClose,
  children,
  size = "md",
  closeOnEsc = true,
}: {
  title: ReactNode;
  onClose: () => void;
  children: ReactNode;
  size?: "sm" | "md";
  closeOnEsc?: boolean;
}) {
  const [pos, setPos] = useState({ x: 0, y: 0 });
  const drag = useRef<{ sx: number; sy: number; px: number; py: number } | null>(null);

  useEffect(() => {
    if (!closeOnEsc) return;
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [closeOnEsc, onClose]);

  const onHeadDown = (e: ReactMouseEvent) => {
    if ((e.target as HTMLElement).closest("button")) return;
    drag.current = { sx: e.clientX, sy: e.clientY, px: pos.x, py: pos.y };
    const move = (ev: MouseEvent) => {
      if (!drag.current) return;
      setPos({
        x: drag.current.px + ev.clientX - drag.current.sx,
        y: drag.current.py + ev.clientY - drag.current.sy,
      });
    };
    const up = () => {
      drag.current = null;
      window.removeEventListener("mousemove", move);
      window.removeEventListener("mouseup", up);
    };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
  };

  return (
    <div className="modal-backdrop">
      <div
        className={size === "sm" ? "modal sm" : "modal"}
        style={{ transform: `translate(${pos.x}px, ${pos.y}px)` }}
        onMouseDown={(e) => e.stopPropagation()}
      >
        <div className="modal-head drag" onMouseDown={onHeadDown}>
          <span>{title}</span>
          <button className="icon-btn" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">{children}</div>
      </div>
    </div>
  );
}
