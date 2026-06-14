import { useEffect, useState } from "react";
import { ArrowLeft } from "lucide-react";
import type { Preference } from "../../types";
import type { T } from "../../i18n";
import { addonGetConfig, addonPreferences, addonSetConfig } from "../../api";

export default function ExtensionConfig({
  id,
  name,
  onBack,
  t,
}: {
  id: string;
  name: string;
  onBack: () => void;
  t: T;
}) {
  const [prefs, setPrefs] = useState<Preference[]>([]);
  const [values, setValues] = useState<Record<string, string>>({});
  const [saved, setSaved] = useState(false);

  useEffect(() => {
    addonPreferences(id).then(setPrefs).catch(() => {});
    addonGetConfig(id).then(setValues).catch(() => {});
  }, [id]);

  const set = (key: string, v: string) => {
    setValues((prev) => ({ ...prev, [key]: v }));
    setSaved(false);
  };

  const save = async () => {
    await addonSetConfig(id, values);
    setSaved(true);
  };

  return (
    <div className="ext-config">
      <div className="ext-config-head">
        <button className="icon-btn" onClick={onBack} title={t("addons.back")}>
          <ArrowLeft size={18} />
        </button>
        <h3>{name}</h3>
      </div>

      {prefs.length === 0 ? (
        <div className="muted">{t("addons.no_settings")}</div>
      ) : (
        <>
          {prefs.map((p) => {
            const v = values[p.key] ?? p.default;
            return (
              <div key={p.key} className="cfg-field">
                <label className="field-label">{p.title}</label>
                {p.summary && <div className="muted small">{p.summary}</div>}
                {p.type === "select" ? (
                  <select value={v} onChange={(e) => set(p.key, e.target.value)}>
                    {p.options.map((o) => (
                      <option key={o} value={o}>
                        {o}
                      </option>
                    ))}
                  </select>
                ) : p.type === "bool" ? (
                  <label className="row">
                    <input
                      type="checkbox"
                      checked={v === "true"}
                      onChange={(e) => set(p.key, String(e.target.checked))}
                    />
                  </label>
                ) : (
                  <input value={v} onChange={(e) => set(p.key, e.target.value)} />
                )}
              </div>
            );
          })}
          <div className="row end">
            {saved && <span className="muted small">{t("addons.saved")}</span>}
            <button className="btn primary" onClick={save}>
              {t("addons.save")}
            </button>
          </div>
        </>
      )}
    </div>
  );
}
