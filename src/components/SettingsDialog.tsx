import { useEffect, useState } from "react";
import type { T } from "../i18n";
import {
  getSettings,
  setFolderIcons,
  listFolderTemplates,
  setAria2,
  setMaxConcurrent,
} from "../api";
import Modal from "./Modal";

export default function SettingsDialog({ onClose, t }: { onClose: () => void; t: T }) {
  const [enabled, setEnabled] = useState(false);
  const [template, setTemplate] = useState("none");
  const [templates, setTemplates] = useState<{ id: string; name: string }[]>([]);
  const [aria2On, setAria2On] = useState(false);
  const [connections, setConnections] = useState(4);
  const [maxConcurrent, setMaxConcurrentState] = useState(3);

  useEffect(() => {
    getSettings()
      .then((s) => {
        setEnabled(s.folderIcons);
        if (s.folderTemplate) setTemplate(s.folderTemplate);
        setAria2On(s.useAria2);
        setConnections(s.aria2Connections || 4);
        setMaxConcurrentState(s.maxConcurrentDownloads || 3);
      })
      .catch(() => {});
    listFolderTemplates()
      .then(setTemplates)
      .catch(() => {});
  }, []);

  const pickMaxConcurrent = (n: number) => {
    setMaxConcurrentState(n);
    setMaxConcurrent(n).catch(() => {});
  };

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

  const toggleAria2 = (on: boolean) => {
    setAria2On(on);
    setAria2(on, connections).catch(() => {});
  };
  const pickConnections = (n: number) => {
    setConnections(n);
    setAria2(aria2On, n).catch(() => {});
  };

  return (
    <Modal title={t("settings.title")} onClose={onClose}>
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

      <div className="sep" />

      <label className="field-label">{t("settings.max_concurrent")}</label>
      <select value={maxConcurrent} onChange={(e) => pickMaxConcurrent(Number(e.target.value))}>
        {[1, 2, 3, 4, 5].map((n) => (
          <option key={n} value={n}>
            {n}
          </option>
        ))}
      </select>

      <div className="sep" />

      <label className="field-label">{t("settings.aria2")}</label>
      <label className="row" style={{ cursor: "pointer" }}>
        <input type="checkbox" checked={aria2On} onChange={(e) => toggleAria2(e.target.checked)} />
        <span>{t("settings.aria2_desc")}</span>
      </label>

      {aria2On && (
        <>
          <label className="field-label">{t("settings.aria2_connections")}</label>
          <select
            value={connections}
            onChange={(e) => pickConnections(Number(e.target.value))}
          >
            {[3, 4, 5, 6, 7, 8].map((n) => (
              <option key={n} value={n}>
                {n}
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
    </Modal>
  );
}
