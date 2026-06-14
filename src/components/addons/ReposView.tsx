import { useState } from "react";
import { ArrowLeft, Plus, Trash2 } from "lucide-react";
import type { T } from "../../i18n";
import AddRepoDialog from "./AddRepoDialog";

export default function ReposView({
  repos,
  onAdd,
  onRemove,
  onBack,
  t,
}: {
  repos: string[];
  onAdd: (url: string) => void;
  onRemove: (url: string) => void;
  onBack: () => void;
  t: T;
}) {
  const [adding, setAdding] = useState(false);

  return (
    <div className="repos-view">
      <div className="ext-config-head">
        <button className="icon-btn" onClick={onBack} title={t("addons.back")}>
          <ArrowLeft size={18} />
        </button>
        <h3>{t("addons.repos_title")}</h3>
        <span className="grow" />
        <button className="btn primary" onClick={() => setAdding(true)}>
          <Plus size={15} /> {t("addons.add_repo")}
        </button>
      </div>

      {repos.length === 0 ? (
        <div className="muted">{t("addons.no_repos")}</div>
      ) : (
        repos.map((r) => (
          <div key={r} className="addon-row">
            <span className="repo-url" title={r}>
              {r}
            </span>
            <button className="icon-btn" title={t("addons.remove")} onClick={() => onRemove(r)}>
              <Trash2 size={16} />
            </button>
          </div>
        ))
      )}

      {adding && (
        <AddRepoDialog
          onClose={() => setAdding(false)}
          onAdd={(url) => {
            setAdding(false);
            onAdd(url);
          }}
          t={t}
        />
      )}
    </div>
  );
}
