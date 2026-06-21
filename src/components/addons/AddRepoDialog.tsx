import { useState } from "react";
import type { T } from "../../i18n";
import Modal from "../Modal";

export default function AddRepoDialog({
  onClose,
  onAdd,
  t,
}: {
  onClose: () => void;
  onAdd: (url: string) => void;
  t: T;
}) {
  const [url, setUrl] = useState("");

  const submit = () => {
    const u = url.trim();
    if (u) onAdd(u);
  };

  return (
    <Modal title={t("addons.add_repo_title")} onClose={onClose}>
      <label className="field-label">{t("addons.repo_url")}</label>
      <input
        className="grow"
        value={url}
        onChange={(e) => setUrl(e.target.value)}
        onKeyDown={(e) => e.key === "Enter" && submit()}
        placeholder="https://raw.githubusercontent.com/<user>/<repo>/repo/index.min.json"
        autoFocus
      />
      <div className="row end">
        <button className="btn" onClick={onClose}>
          {t("addons.cancel")}
        </button>
        <button className="btn primary" disabled={!url.trim()} onClick={submit}>
          {t("addons.ok")}
        </button>
      </div>
    </Modal>
  );
}
