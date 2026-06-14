import {
  CircleStop,
  ListX,
  OctagonX,
  Play,
  Plus,
  Puzzle,
  Search,
  Settings,
  Timer,
  Trash2,
} from "lucide-react";
import type { ReactNode } from "react";
import type { T } from "../i18n";

type Btn = { key: string; icon: ReactNode; label: string; tip: string; onClick: () => void; sep?: boolean };

export default function Toolbar({
  t,
  onAdd,
  onResume,
  onStop,
  onStopAll,
  onRemoveSelected,
  onDeleteAll,
  onOpenAddons,
  soon,
  search,
  onSearch,
}: {
  t: T;
  onAdd: () => void;
  onResume: () => void;
  onStop: () => void;
  onStopAll: () => void;
  onRemoveSelected: () => void;
  onDeleteAll: () => void;
  onOpenAddons: () => void;
  soon: (l: string) => void;
  search: string;
  onSearch: (v: string) => void;
}) {
  const btns: Btn[] = [
    { key: "add", icon: <Plus size={20} />, label: t("toolbar.add_url"), tip: t("tooltip.add_url"), onClick: onAdd },
    { key: "resume", icon: <Play size={20} />, label: t("toolbar.resume"), tip: t("tooltip.resume"), onClick: onResume },
    { key: "stop", icon: <CircleStop size={20} />, label: t("toolbar.stop"), tip: t("tooltip.stop"), onClick: onStop },
    { key: "stopall", icon: <OctagonX size={20} />, label: t("toolbar.stop_all"), tip: t("tooltip.stop_all"), onClick: onStopAll, sep: true },
    { key: "del", icon: <Trash2 size={20} />, label: t("toolbar.delete"), tip: t("tooltip.delete"), onClick: onRemoveSelected },
    { key: "delall", icon: <ListX size={20} />, label: t("toolbar.delete_all"), tip: t("tooltip.delete_all"), onClick: onDeleteAll, sep: true },
    { key: "opts", icon: <Settings size={20} />, label: t("toolbar.options"), tip: t("tooltip.options"), onClick: () => soon(t("toolbar.options")) },
    { key: "sched", icon: <Timer size={20} />, label: t("toolbar.scheduler"), tip: t("tooltip.scheduler"), onClick: () => soon(t("toolbar.scheduler")), sep: true },
    { key: "addons", icon: <Puzzle size={20} />, label: t("toolbar.addons"), tip: t("tooltip.addons"), onClick: onOpenAddons },
  ];

  return (
    <div className="toolbar">
      {btns.map((b) => (
        <span key={b.key} className={b.sep ? "tb-wrap tb-sep" : "tb-wrap"}>
          <button className="tb-btn" title={b.tip} onClick={b.onClick}>
            {b.icon}
            <span className="tb-label">{b.label}</span>
          </button>
        </span>
      ))}
      <div className="tb-spacer" />
      <div className="tb-search">
        <Search size={15} className="tb-search-ic" />
        <input
          value={search}
          onChange={(e) => onSearch(e.target.value)}
          placeholder={t("toolbar.search_hint")}
        />
      </div>
    </div>
  );
}
