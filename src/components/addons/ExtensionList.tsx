import { Download, Loader2, Puzzle, Settings2, Trash2 } from "lucide-react";
import type { T } from "../../i18n";

export type ExtItem = {
  id: string;
  name: string;
  lang: string;
  version: string;
  iconUrl?: string | null;
  repoUrl?: string;
  installed: boolean;
};

export default function ExtensionList({
  items,
  busyId,
  onInstall,
  onRemove,
  onConfigure,
  t,
}: {
  items: ExtItem[];
  busyId: string | null;
  onInstall: (item: ExtItem) => void;
  onRemove: (item: ExtItem) => void;
  onConfigure: (item: ExtItem) => void;
  t: T;
}) {
  if (items.length === 0) {
    return <div className="ext-empty muted">{t("addons.empty")}</div>;
  }

  return (
    <div className="ext-list">
      {items.map((it) => (
        <div key={it.id} className="ext-row">
          <div className="ext-icon">
            {it.iconUrl ? <img src={it.iconUrl} alt="" /> : <Puzzle size={24} />}
          </div>
          <div className="ext-meta">
            <div className="ext-name">{it.name}</div>
            <div className="muted small">
              {it.lang} · v{it.version}
            </div>
          </div>
          <div className="ext-actions">
            {it.installed ? (
              <>
                <button className="icon-btn" title={t("addons.settings")} onClick={() => onConfigure(it)}>
                  <Settings2 size={18} />
                </button>
                <button className="icon-btn" title={t("addons.remove")} onClick={() => onRemove(it)}>
                  <Trash2 size={18} />
                </button>
              </>
            ) : (
              <button className="btn" disabled={busyId === it.id} onClick={() => onInstall(it)}>
                {busyId === it.id ? <Loader2 size={14} className="spin" /> : <Download size={14} />}{" "}
                {t("addons.install")}
              </button>
            )}
          </div>
        </div>
      ))}
    </div>
  );
}
