import type { DownloadRow } from "../types";
import type { T } from "../i18n";

export default function StatusBar({
  rows,
  message,
  t,
}: {
  rows: DownloadRow[];
  message: string;
  t: T;
}) {
  const active = rows.filter(
    (d) => d.status === "downloading" || d.status === "resolving" || d.status === "queued",
  ).length;
  return (
    <div className="statusbar">
      <span className="status-msg">{message}</span>
      <span className="status-active">
        {active} {t("status.running")}
      </span>
    </div>
  );
}
