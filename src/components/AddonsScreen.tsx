import { useEffect, useMemo, useState } from "react";
import { Library, RefreshCw } from "lucide-react";
import type { InstalledAddon, StoreEntry } from "../types";
import type { T } from "../i18n";
import {
  addRepo,
  addonIcon,
  addonRemove,
  getSettings,
  removeRepo,
  storeFetch,
  storeInstall,
} from "../api";
import ExtensionList, { type ExtItem } from "./addons/ExtensionList";
import ExtensionConfig from "./addons/ExtensionConfig";
import ReposView from "./addons/ReposView";

type View = { kind: "list" } | { kind: "repos" } | { kind: "config"; id: string; name: string };

export default function AddonsScreen({
  installed,
  onChange,
  t,
}: {
  installed: InstalledAddon[];
  onChange: () => void;
  t: T;
}) {
  const [view, setView] = useState<View>({ kind: "list" });
  const [repos, setRepos] = useState<string[]>([]);
  const [store, setStore] = useState<StoreEntry[]>([]);
  const [diskIcons, setDiskIcons] = useState<Record<string, string>>({});
  const [loading, setLoading] = useState(false);
  const [busyId, setBusyId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);

  const loadRepos = () => getSettings().then((s) => setRepos(s.repos)).catch(() => {});

  const refresh = async () => {
    setLoading(true);
    setError(null);
    try {
      setStore(await storeFetch());
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  };

  useEffect(() => {
    loadRepos();
    refresh();
  }, []);

  // Icons for installed addons that no repo lists anymore (read from disk).
  useEffect(() => {
    const orphans = installed.filter((a) => !store.some((e) => e.id === a.id));
    orphans.forEach((a) => {
      if (!diskIcons[a.id]) {
        addonIcon(a.id).then((d) => {
          if (d) setDiskIcons((m) => ({ ...m, [a.id]: d }));
        });
      }
    });
  }, [installed, store]);

  const items: ExtItem[] = useMemo(() => {
    const fromStore: ExtItem[] = store.map((e) => ({
      id: e.id,
      name: e.name,
      lang: e.lang,
      version: e.version,
      iconUrl: e.iconUrl,
      repoUrl: e.repoUrl,
      installed: e.installed,
    }));
    const extra: ExtItem[] = installed
      .filter((a) => !store.some((e) => e.id === a.id))
      .map((a) => ({
        id: a.id,
        name: a.name,
        lang: a.lang,
        version: a.version,
        iconUrl: diskIcons[a.id],
        installed: true,
      }));
    return [...fromStore, ...extra];
  }, [store, installed, diskIcons]);

  const install = async (item: ExtItem) => {
    if (!item.repoUrl) return;
    setBusyId(item.id);
    setError(null);
    try {
      await storeInstall(item.repoUrl, item.id);
      await refresh();
      onChange();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusyId(null);
    }
  };

  const remove = async (id: string) => {
    await addonRemove(id);
    await refresh();
    onChange();
  };

  const onAddRepo = async (url: string) => {
    await addRepo(url);
    await loadRepos();
    await refresh();
  };

  const onRemoveRepo = async (url: string) => {
    await removeRepo(url);
    await loadRepos();
    await refresh();
  };

  if (view.kind === "config") {
    return (
      <div className="addons-screen">
        <ExtensionConfig
          id={view.id}
          name={view.name}
          onBack={() => setView({ kind: "list" })}
          t={t}
        />
      </div>
    );
  }

  if (view.kind === "repos") {
    return (
      <div className="addons-screen">
        <ReposView
          repos={repos}
          onAdd={onAddRepo}
          onRemove={onRemoveRepo}
          onBack={() => setView({ kind: "list" })}
          t={t}
        />
      </div>
    );
  }

  return (
    <div className="addons-screen">
      <div className="addons-header">
        <h2>{t("addons.title")}</h2>
        <span className="grow" />
        <button className="icon-btn" title={t("addons.refresh")} onClick={refresh}>
          <RefreshCw size={18} className={loading ? "spin" : ""} />
        </button>
        <button
          className="icon-btn"
          title={t("addons.repos_title")}
          onClick={() => setView({ kind: "repos" })}
        >
          <Library size={18} />
        </button>
      </div>

      {error && <div className="err-box">{error}</div>}

      <ExtensionList
        items={items}
        busyId={busyId}
        onInstall={install}
        onRemove={remove}
        onConfigure={(it) => setView({ kind: "config", id: it.id, name: it.name })}
        t={t}
      />
    </div>
  );
}
