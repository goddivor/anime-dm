import { useEffect, useState } from "react";
import { Check, Download, RefreshCw, Settings2, Trash2 } from "lucide-react";
import type { InstalledAddon, Preference, StoreEntry } from "../types";
import type { T } from "../i18n";
import {
  addonGetConfig,
  addonPreferences,
  addonRemove,
  addonSetConfig,
  getSettings,
  setRepoUrl,
  storeFetch,
  storeInstall,
} from "../api";

function AddonConfig({ id, onClose, t }: { id: string; onClose: () => void; t: T }) {
  const [prefs, setPrefs] = useState<Preference[]>([]);
  const [values, setValues] = useState<Record<string, string>>({});

  useEffect(() => {
    addonPreferences(id).then(setPrefs).catch(() => {});
    addonGetConfig(id).then(setValues).catch(() => {});
  }, [id]);

  const save = async () => {
    await addonSetConfig(id, values);
    onClose();
  };

  if (prefs.length === 0) return <div className="muted addon-cfg">{t("addons.no_settings")}</div>;

  return (
    <div className="addon-cfg">
      {prefs.map((p) => {
        const v = values[p.key] ?? p.default;
        return (
          <div key={p.key} className="cfg-field">
            <label className="field-label">{p.title}</label>
            {p.summary && <div className="muted small">{p.summary}</div>}
            {p.type === "select" ? (
              <select value={v} onChange={(e) => setValues({ ...values, [p.key]: e.target.value })}>
                {p.options.map((o) => (
                  <option key={o} value={o}>
                    {o}
                  </option>
                ))}
              </select>
            ) : p.type === "bool" ? (
              <input
                type="checkbox"
                checked={v === "true"}
                onChange={(e) => setValues({ ...values, [p.key]: String(e.target.checked) })}
              />
            ) : (
              <input value={v} onChange={(e) => setValues({ ...values, [p.key]: e.target.value })} />
            )}
          </div>
        );
      })}
      <div className="row end">
        <button className="btn" onClick={onClose}>
          {t("addons.cancel")}
        </button>
        <button className="btn primary" onClick={save}>
          {t("addons.save")}
        </button>
      </div>
    </div>
  );
}

export default function AddonsScreen({
  installed,
  onChange,
  t,
}: {
  installed: InstalledAddon[];
  onChange: () => void;
  t: T;
}) {
  const [repoUrl, setRepoUrlState] = useState("");
  const [store, setStore] = useState<StoreEntry[]>([]);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [configuring, setConfiguring] = useState<string | null>(null);

  useEffect(() => {
    getSettings().then((s) => setRepoUrlState(s.repoUrl)).catch(() => {});
  }, []);

  const loadStore = async () => {
    setBusy(true);
    setError(null);
    try {
      await setRepoUrl(repoUrl.trim());
      setStore(await storeFetch());
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  };

  const install = async (id: string) => {
    setBusy(true);
    setError(null);
    try {
      await storeInstall(id);
      setStore(await storeFetch());
      onChange();
    } catch (e) {
      setError(String(e));
    } finally {
      setBusy(false);
    }
  };

  const remove = async (id: string) => {
    await addonRemove(id);
    if (store.length) setStore(await storeFetch());
    onChange();
  };

  return (
    <div className="addons-screen">
      <h2>{t("addons.title")}</h2>

      <section>
        <div className="field-label">{t("addons.repo_url")}</div>
        <div className="row">
          <input
            className="grow"
            value={repoUrl}
            onChange={(e) => setRepoUrlState(e.target.value)}
            placeholder="https://raw.githubusercontent.com/<user>/<repo>/repo/index.min.json"
          />
          <button className="btn primary" onClick={loadStore} disabled={busy}>
            <RefreshCw size={15} className={busy ? "spin" : ""} /> {t("addons.load")}
          </button>
        </div>
        {error && <div className="err-box">{error}</div>}
      </section>

      {store.length > 0 && (
        <section>
          <h3>{t("addons.available")}</h3>
          {store.map((e) => (
            <div key={e.id} className="addon-row">
              <span className="addon-name">
                {e.name} <span className="muted small">{e.lang} · v{e.version}</span>
              </span>
              {e.installed ? (
                <span className="installed-tag">
                  <Check size={14} /> {t("addons.installed")}
                </span>
              ) : (
                <button className="btn" onClick={() => install(e.id)} disabled={busy}>
                  <Download size={14} /> {t("addons.install")}
                </button>
              )}
            </div>
          ))}
        </section>
      )}

      <section>
        <h3>{t("addons.installed_title")}</h3>
        {installed.length === 0 && <div className="muted">{t("addons.none")}</div>}
        {installed.map((a) => (
          <div key={a.id} className="addon-block">
            <div className="addon-row">
              <span className="addon-name">
                {a.name} <span className="muted small">{a.lang} · v{a.version}</span>
              </span>
              <span className="row">
                <button
                  className="icon-btn"
                  title={t("addons.settings")}
                  onClick={() => setConfiguring(configuring === a.id ? null : a.id)}
                >
                  <Settings2 size={16} />
                </button>
                <button className="icon-btn" title={t("addons.remove")} onClick={() => remove(a.id)}>
                  <Trash2 size={16} />
                </button>
              </span>
            </div>
            {configuring === a.id && (
              <AddonConfig id={a.id} onClose={() => setConfiguring(null)} t={t} />
            )}
          </div>
        ))}
      </section>
    </div>
  );
}
