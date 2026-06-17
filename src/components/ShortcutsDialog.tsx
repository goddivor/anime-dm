import { useEffect } from "react";
import type { T } from "../i18n";

type Shortcut = { keys: string[]; desc: string };

export default function ShortcutsDialog({ onClose, t }: { onClose: () => void; t: T }) {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  const mod = navigator.platform.toLowerCase().includes("mac") ? "⌘" : "Ctrl";
  const shortcuts: Shortcut[] = [
    { keys: [mod, "N"], desc: t("shortcuts.add") },
    { keys: [mod, "F"], desc: t("shortcuts.search") },
    { keys: [mod, "A"], desc: t("shortcuts.select_all") },
    { keys: ["↑", "↓"], desc: t("shortcuts.navigate") },
    { keys: ["Shift", "↑", "↓"], desc: t("shortcuts.extend") },
    { keys: [t("shortcuts.key_enter")], desc: t("shortcuts.open") },
    { keys: [t("shortcuts.key_delete")], desc: t("shortcuts.delete") },
    { keys: [t("shortcuts.key_esc")], desc: t("shortcuts.close") },
  ];

  return (
    <div className="modal-backdrop" onMouseDown={onClose}>
      <div className="modal sm" onMouseDown={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <span>{t("shortcuts.title")}</span>
          <button className="icon-btn" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">
          <div className="shortcut-list">
            {shortcuts.map((s, i) => (
              <div key={i} className="shortcut-row">
                <span className="shortcut-keys">
                  {s.keys.map((k, j) => (
                    <kbd key={j}>{k}</kbd>
                  ))}
                </span>
                <span className="shortcut-desc">{s.desc}</span>
              </div>
            ))}
          </div>
        </div>
      </div>
    </div>
  );
}
