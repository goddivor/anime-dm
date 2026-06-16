import {
  ChevronDown,
  ChevronRight,
  Film,
  Folder,
  Inbox,
  Timer,
  X,
} from "lucide-react";
import type { AnimeGroup, DownloadRow, Filter } from "../types";
import type { T } from "../i18n";
import Poster from "./Poster";

const statusGlyph: Record<DownloadRow["status"], string> = {
  queued: "·",
  resolving: "·",
  downloading: "↓",
  completed: "✓",
  failed: "✗",
  stopped: "■",
};

export default function Sidebar({
  groups,
  rows,
  filter,
  onFilter,
  onToggle,
  onClose,
  selected,
  onSelectRow,
  onAnimeContext,
  onEpisodeContext,
  t,
}: {
  groups: AnimeGroup[];
  rows: DownloadRow[];
  filter: Filter;
  onFilter: (f: Filter) => void;
  onToggle: (id: number) => void;
  onClose: () => void;
  selected: Set<number>;
  onSelectRow: (id: number) => void;
  onAnimeContext: (groupId: number, x: number, y: number) => void;
  onEpisodeContext: (id: number, x: number, y: number) => void;
  t: T;
}) {
  const count = (pred: (d: DownloadRow) => boolean) => rows.filter(pred).length;
  const isAll = filter.kind === "all";

  return (
    <div className="sidebar">
      <div className="sidebar-head">
        <span>{t("sidebar.title")}</span>
        <button className="icon-btn" title={t("sidebar.close")} onClick={onClose}>
          <X size={14} />
        </button>
      </div>
      <div className="sidebar-body">
        <div
          className={`tree-row root ${isAll ? "sel" : ""}`}
          onClick={() => onFilter({ kind: "all" })}
        >
          <Folder size={15} />
          <span className="tree-label">{t("sidebar.all")}</span>
          <span className="badge">{rows.length}</span>
        </div>

        {groups.map((g) => {
          const eps = rows.filter((d) => d.animeId === g.id);
          const animeSel = filter.kind === "anime" && filter.id === g.id;
          return (
            <div key={g.id} className="tree-group">
              <div
                className={`tree-row ${animeSel ? "sel" : ""}`}
                onContextMenu={(e) => {
                  e.preventDefault();
                  onAnimeContext(g.id, e.clientX, e.clientY);
                }}
              >
                <button
                  className="caret"
                  onClick={(e) => {
                    e.stopPropagation();
                    onToggle(g.id);
                  }}
                >
                  {g.expanded ? <ChevronDown size={14} /> : <ChevronRight size={14} />}
                </button>
                <Poster
                  url={g.posterUrl}
                  data={g.posterData}
                  referer={g.url}
                  className="poster"
                  fallback={<Film size={15} />}
                />
                <span
                  className={`tree-label clickable ${animeSel ? "sel-text" : ""}`}
                  onClick={() => onFilter({ kind: "anime", id: g.id })}
                  title={g.title}
                >
                  {g.title}
                </span>
                <span className="badge">{eps.length}</span>
              </div>
              {g.expanded && (
                <div className="tree-children">
                  {eps.map((d) => (
                    <div
                      key={d.id}
                      className={`tree-row ep ${selected.has(d.id) ? "sel" : ""}`}
                      onClick={() => {
                        onSelectRow(d.id);
                        onFilter({ kind: "anime", id: g.id });
                      }}
                      onContextMenu={(e) => {
                        e.preventDefault();
                        onEpisodeContext(d.id, e.clientX, e.clientY);
                      }}
                    >
                      <span className={`ep-glyph ${d.status}`}>{statusGlyph[d.status]}</span>
                      <span className="tree-label">
                        {d.isMovie
                          ? t("table.movie")
                          : `Ep ${String(Math.round(d.episodeNumber)).padStart(3, "0")}`}
                      </span>
                    </div>
                  ))}
                </div>
              )}
            </div>
          );
        })}

        <div className="sidebar-sep" />
        <div className="sidebar-subtitle">{t("sidebar.queues")}</div>
        <div
          className={`tree-row ${filter.kind === "queue" && filter.queue === "main" ? "sel" : ""}`}
          onClick={() => onFilter({ kind: "queue", queue: "main" })}
        >
          <Inbox size={15} />
          <span className="tree-label">{t("sidebar.queue_main")}</span>
          <span className="badge">{count((d) => d.queue === "main")}</span>
        </div>
        <div
          className={`tree-row ${filter.kind === "queue" && filter.queue === "scheduler" ? "sel" : ""}`}
          onClick={() => onFilter({ kind: "queue", queue: "scheduler" })}
        >
          <Timer size={15} />
          <span className="tree-label">{t("sidebar.queue_scheduler")}</span>
          <span className="badge">{count((d) => d.queue === "scheduler")}</span>
        </div>
      </div>
    </div>
  );
}
