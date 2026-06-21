import { useEffect, useState } from "react";
import {
  ArrowLeft,
  Plus,
  Trash2,
  ExternalLink,
  Copy,
  Eye,
  EyeOff,
  Puzzle,
  RefreshCw,
} from "lucide-react";
import type { T } from "../../i18n";
import type { RepoInfo } from "../../types";
import { listRepos, addRepo, removeRepo, setRepoDisabled, openExternal } from "../../api";
import AddRepoDialog from "./AddRepoDialog";

export default function ReposView({
  onChanged,
  onBack,
  t,
}: {
  onChanged: () => void;
  onBack: () => void;
  t: T;
}) {
  const [list, setList] = useState<RepoInfo[]>([]);
  const [adding, setAdding] = useState(false);

  const reload = () => listRepos().then(setList).catch(() => {});
  useEffect(() => {
    reload();
  }, []);

  const change = async (fn: () => Promise<unknown>) => {
    await fn();
    await reload();
    onChanged();
  };

  return (
    <div className="repos-view">
      <div className="ext-config-head">
        <button className="icon-btn" onClick={onBack} title={t("addons.back")}>
          <ArrowLeft size={18} />
        </button>
        <h3>{t("addons.repos_title")}</h3>
        <span className="grow" />
        <button className="icon-btn" title={t("addons.refresh")} onClick={reload}>
          <RefreshCw size={18} />
        </button>
        <button className="btn primary" onClick={() => setAdding(true)}>
          <Plus size={15} /> {t("addons.add_repo")}
        </button>
      </div>

      {list.length === 0 ? (
        <div className="muted">{t("addons.no_repos")}</div>
      ) : (
        <div className="repo-list">
          {list.map((r) => (
            <div key={r.url} className={"repo-card" + (r.disabled ? " disabled" : "")}>
              <div className="repo-icon">
                {r.iconUrl ? <img src={r.iconUrl} alt="" /> : <Puzzle size={26} />}
              </div>
              <div className="repo-meta">
                <div className="repo-name">{r.name}</div>
                <div className="muted small repo-url" title={r.url}>
                  {r.url}
                </div>
              </div>
              <div className="repo-actions">
                <button
                  className="icon-btn"
                  title={t("addons.repo_open")}
                  disabled={!r.website}
                  onClick={() => r.website && openExternal(r.website)}
                >
                  <ExternalLink size={18} />
                </button>
                <button
                  className="icon-btn"
                  title={t("addons.repo_copy")}
                  onClick={() => navigator.clipboard?.writeText(r.url).catch(() => {})}
                >
                  <Copy size={18} />
                </button>
                <button
                  className="icon-btn"
                  title={t(r.disabled ? "addons.repo_enable" : "addons.repo_disable")}
                  onClick={() => change(() => setRepoDisabled(r.url, !r.disabled))}
                >
                  {r.disabled ? <Eye size={18} /> : <EyeOff size={18} />}
                </button>
                <button
                  className="icon-btn"
                  title={t("addons.remove")}
                  onClick={() => change(() => removeRepo(r.url))}
                >
                  <Trash2 size={18} />
                </button>
              </div>
            </div>
          ))}
        </div>
      )}

      {adding && (
        <AddRepoDialog
          onClose={() => setAdding(false)}
          onAdd={(url) => {
            setAdding(false);
            change(() => addRepo(url));
          }}
          t={t}
        />
      )}
    </div>
  );
}
