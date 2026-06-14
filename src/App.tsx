import { useEffect, useMemo, useRef, useState, type MouseEvent as ReactMouseEvent } from "react";
import "./App.css";
import { translator, type Lang } from "./i18n";
import type { Anime, AnimeGroup, DownloadRow, Filter, InstalledAddon } from "./types";
import {
  addonsInstalled,
  defaultOutPath,
  onFinished,
  onProgress,
  startDownload,
  stopAll,
  stopDownload,
  type FinishedEvent,
  type ProgressEvent,
} from "./api";
import MenuBar from "./components/MenuBar";
import Toolbar from "./components/Toolbar";
import Sidebar from "./components/Sidebar";
import DownloadsTable from "./components/DownloadsTable";
import StatusBar from "./components/StatusBar";
import AddDialog from "./components/AddDialog";
import AddonsScreen from "./components/AddonsScreen";
import ConfirmDialog, { type Confirm } from "./components/ConfirmDialog";

const isActive = (s: DownloadRow["status"]) =>
  s === "downloading" || s === "resolving" || s === "queued";

const pad = (n: number) => String(n).padStart(3, "0");
const sanitize = (s: string) => s.replace(/[/\\:*?"<>|]/g, "_");

function applyProgress(r: DownloadRow, e: ProgressEvent): DownloadRow {
  let eta = r.eta;
  let tick = r._tick;
  const p = e.progress;
  if (p != null && p >= 0) {
    const now = Date.now();
    if (tick) {
      const dt = (now - tick.at) / 1000;
      const dp = p - tick.p;
      if (dt > 0.3 && dp > 0) {
        eta = (1 - p) / (dp / dt);
        tick = { at: now, p };
      }
    } else {
      tick = { at: now, p };
    }
  }
  return {
    ...r,
    status: e.status as DownloadRow["status"],
    lastTry: Date.now(),
    progress: p != null ? p : r.progress,
    speed: e.speed ?? r.speed,
    sizeBytes: e.sizeBytes ?? r.sizeBytes,
    address: e.address ?? r.address,
    eta,
    _tick: tick,
  };
}

function applyFinished(r: DownloadRow, e: FinishedEvent): DownloadRow {
  if (e.ok) return { ...r, status: "completed", progress: 1, speed: "", eta: undefined };
  return { ...r, status: "failed", error: e.error ?? undefined };
}

export default function App() {
  const [lang, setLang] = useState<Lang>("fr");
  const t = useMemo(() => translator(lang), [lang]);

  const [rows, setRows] = useState<DownloadRow[]>([]);
  const [groups, setGroups] = useState<AnimeGroup[]>([]);
  const [addons, setAddons] = useState<InstalledAddon[]>([]);
  const [view, setView] = useState<"downloads" | "addons">("downloads");
  const [filter, setFilter] = useState<Filter>({ kind: "all" });
  const [selected, setSelected] = useState<Set<number>>(new Set());
  const [anchor, setAnchor] = useState<number | null>(null);
  const [sidebarOn, setSidebarOn] = useState(true);
  const [sidebarW, setSidebarW] = useState(230);
  const [search, setSearch] = useState("");
  const [message, setMessage] = useState("");
  const [showAdd, setShowAdd] = useState(false);
  const [info, setInfo] = useState<{ title: string; lines: string[] } | null>(null);
  const [confirm, setConfirm] = useState<Confirm | null>(null);

  const nextId = useRef(1);
  const nextGroupId = useRef(1);

  const refreshAddons = () => addonsInstalled().then(setAddons).catch(() => {});

  useEffect(() => {
    refreshAddons();
    const ps = onProgress((e) =>
      setRows((rs) => rs.map((r) => (r.id === e.id ? applyProgress(r, e) : r))),
    );
    const fs = onFinished((e) =>
      setRows((rs) => rs.map((r) => (r.id === e.id ? applyFinished(r, e) : r))),
    );
    return () => {
      ps.then((u) => u());
      fs.then((u) => u());
    };
  }, []);

  useEffect(() => {
    setGroups((gs) => {
      const used = gs.filter((g) => rows.some((r) => r.animeId === g.id));
      return used.length === gs.length ? gs : used;
    });
  }, [rows]);

  const soon = (label: string) => setMessage(`« ${label} » — ${t("status.coming_soon")}`);

  const onLaunch = async (addonId: string, anime: Anime, numbers: number[]) => {
    setShowAdd(false);
    const existing = groups.find((g) => g.url === anime.url);
    let animeId: number;
    if (existing) {
      animeId = existing.id;
    } else {
      animeId = nextGroupId.current++;
      setGroups((gs) => [
        ...gs,
        { id: animeId, title: anime.title, url: anime.url, posterUrl: anime.posterUrl, expanded: true },
      ]);
    }
    for (const n of numbers) {
      const ep = anime.episodes.find((e) => Math.round(e.number) === n);
      if (!ep) continue;
      const id = nextId.current++;
      const filename = sanitize(`${anime.title} - Ep ${pad(n)}.mp4`);
      const outPath = await defaultOutPath(filename);
      setRows((rs) => [
        ...rs,
        {
          id,
          addonId,
          animeId,
          animeTitle: anime.title,
          episodeNumber: ep.number,
          filename,
          pageUrl: ep.url,
          queue: "main",
          status: "queued",
          progress: -1,
          speed: "",
          addedAt: Date.now(),
          outPath,
        },
      ]);
      startDownload({ addonId, id, episodeUrl: ep.url, playerName: "", outPath }).catch((err) =>
        setRows((rs) =>
          rs.map((r) => (r.id === id ? { ...r, status: "failed", error: String(err) } : r)),
        ),
      );
    }
  };

  const toggleGroup = (id: number) =>
    setGroups((gs) => gs.map((g) => (g.id === id ? { ...g, expanded: !g.expanded } : g)));

  const q = search.toLowerCase();
  const visible = rows.filter((d) => {
    const byFilter =
      filter.kind === "all"
        ? true
        : filter.kind === "anime"
          ? d.animeId === filter.id
          : d.queue === filter.queue;
    return byFilter && (q === "" || d.filename.toLowerCase().includes(q));
  });

  const selectSingle = (id: number) => {
    setSelected(new Set([id]));
    setAnchor(id);
  };

  const handleSelect = (id: number, ctrl: boolean, shift: boolean) => {
    if (shift && anchor != null) {
      const ids = visible.map((r) => r.id);
      const a = ids.indexOf(anchor);
      const b = ids.indexOf(id);
      if (a >= 0 && b >= 0) {
        const [lo, hi] = a < b ? [a, b] : [b, a];
        setSelected(new Set(ids.slice(lo, hi + 1)));
        return;
      }
    }
    if (ctrl) {
      const s = new Set(selected);
      s.has(id) ? s.delete(id) : s.add(id);
      setSelected(s);
      setAnchor(id);
      return;
    }
    selectSingle(id);
  };

  const restart = (r: DownloadRow) => {
    startDownload({
      addonId: r.addonId,
      id: r.id,
      episodeUrl: r.pageUrl,
      playerName: "",
      outPath: r.outPath,
    }).catch((err) =>
      setRows((rs) =>
        rs.map((x) => (x.id === r.id ? { ...x, status: "failed", error: String(err) } : x)),
      ),
    );
    setRows((rs) =>
      rs.map((x) =>
        x.id === r.id
          ? { ...x, status: "queued", progress: -1, speed: "", error: undefined, eta: undefined, _tick: undefined }
          : x,
      ),
    );
  };

  const stopRows = (ids: Set<number>) => {
    rows.filter((r) => ids.has(r.id) && isActive(r.status)).forEach((r) => stopDownload(r.id));
    setRows((rs) =>
      rs.map((r) =>
        ids.has(r.id) && isActive(r.status) ? { ...r, status: "stopped", speed: "", eta: undefined } : r,
      ),
    );
  };

  const stopSelected = () => stopRows(selected);
  const resumeSelected = () =>
    rows.filter((r) => selected.has(r.id) && !isActive(r.status) && r.status !== "completed").forEach(restart);

  const removeSelected = () => {
    stopRows(selected);
    setRows((rs) => rs.filter((r) => !selected.has(r.id)));
    setSelected(new Set());
  };

  const removeCompleted = () => {
    const removed = rows.filter((r) => r.status === "completed").length;
    setRows((rs) => rs.filter((r) => r.status !== "completed"));
    setMessage(`${removed} ${t("status.completed_removed")}`);
  };

  const confirmStopAll = () =>
    setConfirm({
      title: t("confirm.stop_all_title"),
      message: t("confirm.stop_all_msg"),
      confirmLabel: t("toolbar.stop_all"),
      onConfirm: () => {
        stopAll();
        setRows((rs) =>
          rs.map((r) => (isActive(r.status) ? { ...r, status: "stopped", speed: "", eta: undefined } : r)),
        );
      },
    });

  const confirmDeleteAll = () =>
    setConfirm({
      title: t("confirm.delete_all_title"),
      message: t("confirm.delete_all_msg"),
      confirmLabel: t("toolbar.delete_all"),
      onConfirm: () => {
        stopAll();
        setRows([]);
        setGroups([]);
        setSelected(new Set());
      },
    });

  const startDrag = (e: ReactMouseEvent) => {
    e.preventDefault();
    const startX = e.clientX;
    const startW = sidebarW;
    const move = (ev: MouseEvent) =>
      setSidebarW(Math.min(440, Math.max(180, startW + ev.clientX - startX)));
    const up = () => {
      document.removeEventListener("mousemove", move);
      document.removeEventListener("mouseup", up);
    };
    document.addEventListener("mousemove", move);
    document.addEventListener("mouseup", up);
  };

  return (
    <div className="app">
      <MenuBar
        t={t}
        a={{
          onAdd: () => setShowAdd(true),
          onResume: resumeSelected,
          onStop: stopSelected,
          onStopAll: confirmStopAll,
          onRemoveSelected: removeSelected,
          onRemoveCompleted: removeCompleted,
          onDeleteAll: confirmDeleteAll,
          toggleSidebar: () => setSidebarOn((v) => !v),
          sidebarOn,
          toggleSearch: () => setMessage(t("toolbar.search_hint")),
          openAddons: () => setView("addons"),
          setLang,
          lang,
          onAbout: () =>
            setInfo({
              title: t("dialog.about.title"),
              lines: ["Anime Download Manager", "v0.1.0", t("dialog.about.description")],
            }),
          onHelp: () =>
            setInfo({
              title: t("dialog.help.title"),
              lines: [t("dialog.help.step1"), t("dialog.help.step2"), t("dialog.help.step3")],
            }),
          soon,
        }}
      />
      <Toolbar
        t={t}
        onAdd={() => setShowAdd(true)}
        onResume={resumeSelected}
        onStop={stopSelected}
        onStopAll={confirmStopAll}
        onRemoveSelected={removeSelected}
        onDeleteAll={confirmDeleteAll}
        onOpenAddons={() => setView(view === "addons" ? "downloads" : "addons")}
        soon={soon}
        search={search}
        onSearch={setSearch}
      />
      {view === "addons" ? (
        <div className="main">
          <div className="content scroll">
            <AddonsScreen
              installed={addons}
              onChange={refreshAddons}
              t={t}
            />
            <div className="addons-foot">
              <button className="btn" onClick={() => setView("downloads")}>
                {t("addons.back")}
              </button>
            </div>
          </div>
        </div>
      ) : (
        <div className="main">
          {sidebarOn && (
            <>
              <div className="sidebar-wrap" style={{ width: sidebarW }}>
                <Sidebar
                  groups={groups}
                  rows={rows}
                  filter={filter}
                  onFilter={setFilter}
                  onToggle={toggleGroup}
                  onClose={() => setSidebarOn(false)}
                  selected={selected}
                  onSelectRow={selectSingle}
                  t={t}
                />
              </div>
              <div className="splitter" onMouseDown={startDrag} />
            </>
          )}
          <div className="content">
            <DownloadsTable rows={visible} selected={selected} onSelect={handleSelect} t={t} />
          </div>
        </div>
      )}
      <StatusBar rows={rows} message={message} t={t} />

      {confirm && (
        <ConfirmDialog confirm={confirm} onClose={() => setConfirm(null)} t={t} />
      )}
      {showAdd && (
        <AddDialog
          addons={addons}
          onClose={() => setShowAdd(false)}
          onLaunch={onLaunch}
          onOpenAddons={() => {
            setShowAdd(false);
            setView("addons");
          }}
          t={t}
        />
      )}
      {info && (
        <div className="modal-backdrop" onMouseDown={() => setInfo(null)}>
          <div className="modal sm" onMouseDown={(e) => e.stopPropagation()}>
            <div className="modal-head">
              <span>{info.title}</span>
              <button className="icon-btn" onClick={() => setInfo(null)}>
                ✕
              </button>
            </div>
            <div className="modal-body">
              {info.lines.map((l, i) => (
                <div key={i} className={i === 0 ? "anime-title" : "muted"}>
                  {l}
                </div>
              ))}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
