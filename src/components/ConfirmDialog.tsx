import type { T } from "../i18n";

export type Confirm = {
  title: string;
  message: string;
  confirmLabel: string;
  onConfirm: () => void;
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
          <div className="row end">
            <button className="btn" onClick={onClose}>
              {t("confirm.cancel")}
            </button>
            <button
              className="btn danger"
              onClick={() => {
                confirm.onConfirm();
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
