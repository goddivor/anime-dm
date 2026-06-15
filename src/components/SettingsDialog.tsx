import { useEffect, useState } from "react";
import type { T } from "../i18n";
import { getSettings, setFolderIcons, listFolderTemplates } from "../api";

export default function SettingsDialog({ onClose, t }: { onClose: () => void; t: T }) {
  const [enabled, setEnabled] = useState(false);
  const [template, setTemplate] = useState("none");
  const [templates, setTemplates] = useState<{ id: string; name: string }[]>([]);

  useEffect(() => {
    getSettings()
      .then((s) => {
        setEnabled(s.folderIcons);
        if (s.folderTemplate) setTemplate(s.folderTemplate);
      })
      .catch(() => {});
    listFolderTemplates()
      .then(setTemplates)
      .catch(() => {});
  }, []);

  const persist = (en: boolean, tpl: string) => {
    setFolderIcons(en, tpl).catch(() => {});
  };

  const toggle = (en: boolean) => {
    setEnabled(en);
    persist(en, template);
  };
  const pick = (tpl: string) => {
    setTemplate(tpl);
    persist(enabled, tpl);
  };

  return (
    <div className="modal-backdrop" onMouseDown={onClose}>
      <div className="modal" onMouseDown={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <span>{t("settings.title")}</span>
          <button className="icon-btn" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">
          <label className="field-label">{t("settings.folder_icons")}</label>
          <label className="row" style={{ cursor: "pointer" }}>
            <input type="checkbox" checked={enabled} onChange={(e) => toggle(e.target.checked)} />
            <span>{t("settings.folder_icons_desc")}</span>
          </label>

          {enabled && (
            <>
              <label className="field-label">{t("settings.template")}</label>
              <select value={template} onChange={(e) => pick(e.target.value)}>
                {templates.map((tp) => (
                  <option key={tp.id} value={tp.id}>
                    {tp.name}
                  </option>
                ))}
              </select>
            </>
          )}

          <div className="modal-foot">
            <button className="btn primary" onClick={onClose}>
              {t("settings.done")}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
