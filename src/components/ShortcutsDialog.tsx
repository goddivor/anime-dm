import type { T } from "../i18n";
import Modal from "./Modal";

type Shortcut = { keys: string[]; desc: string };

export default function ShortcutsDialog({ onClose, t }: { onClose: () => void; t: T }) {
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
    <Modal title={t("shortcuts.title")} onClose={onClose} size="sm">
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
    </Modal>
  );
}
