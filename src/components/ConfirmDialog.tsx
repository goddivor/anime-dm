import { useEffect, useState } from "react";
import type { T } from "../i18n";

export type ConfirmOption = { key: string; label: string; danger?: boolean; default?: boolean };

export type Confirm = {
  title: string;
  message: string;
  confirmLabel: string;
  options?: ConfirmOption[];
  onConfirm: (checked: Record<string, boolean>) => void;
};

export default function ConfirmDialog({
  confirm,
  onClose,
  t,
}: {
  confirm: Confirm;
  onClose: () => void;
  t: T;
}) {
  const [checked, setChecked] = useState<Record<string, boolean>>(() =>
    Object.fromEntries((confirm.options ?? []).map((o) => [o.key, o.default ?? false])),
  );

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => e.key === "Escape" && onClose();
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, [onClose]);

  return (
    <div className="modal-backdrop" onMouseDown={onClose}>
      <div className="modal sm" onMouseDown={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <span>{confirm.title}</span>
          <button className="icon-btn" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">
          <div>{confirm.message}</div>
          {confirm.options?.map((o) => (
            <label key={o.key} className={`check-row${o.danger ? " danger" : ""}`}>
              <input
                type="checkbox"
                checked={checked[o.key] ?? false}
                onChange={(e) => setChecked((c) => ({ ...c, [o.key]: e.target.checked }))}
              />
              <span>{o.label}</span>
            </label>
          ))}
          <div className="row end">
            <button className="btn" onClick={onClose}>
              {t("confirm.cancel")}
            </button>
            <button
              className="btn danger"
              onClick={() => {
                confirm.onConfirm(checked);
                onClose();
              }}
            >
              {confirm.confirmLabel}
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
