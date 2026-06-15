import { useEffect, useMemo, useRef, useState, type MouseEvent as ReactMouseEvent } from "react";
import "./App.css";
import { translator, type Lang } from "./i18n";
import type { Anime, AnimeGroup, DownloadRow, Filter, InstalledAddon } from "./types";
import {
  addonsInstalled,
  defaultDownloadDir,
  joinPath,
  onFinished,
  onProgress,
  startDownload,
  pauseDownload,
  pauseAll,
  resumeDownload,
  cancelDownload,
  cancelAll,
  stateLoad,
  downloadSave,
  downloadsDelete,
  downloadsClear,
  groupSave,
  getSettings,
  setLangPref,
  applyFolderIcon,
  listFolderTemplates,
  fetchImage,
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
import SettingsDialog from "./components/SettingsDialog";
import ConfirmDialog, { type Confirm } from "./components/ConfirmDialog";
import ContextMenu, { type CtxItem } from "./components/ContextMenu";

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
  const changeLang = (l: Lang) => {
    setLang(l);
    setLangPref(l).catch(() => {});
  };

  const [rows, setRows] = useState<DownloadRow[]>([]);
  const [groups, setGroups] = useState<AnimeGroup[]>([]);
  const [addons, setAddons] = useState<InstalledAddon[]>([]);
  const [view, setView] = useState<"downloads" | "addons">("downloads");
  const [filter, setFilter] = useState<Filter>({ kind: "all" });
  const [selected, setSelected] = useState<Set<number>>(new Set());
  const [anchor, setAnchor] = useState<number | null>(null);
  const [cursor, setCursor] = useState<number | null>(null);
  const [menu, setMenu] = useState<{ x: number; y: number } | null>(null);
  const [sidebarOn, setSidebarOn] = useState(true);
  const [sidebarW, setSidebarW] = useState(230);
  const [search, setSearch] = useState("");
  const [message, setMessage] = useState("");
  const [showAdd, setShowAdd] = useState(false);
  const [showSettings, setShowSettings] = useState(false);
  const [iconTemplates, setIconTemplates] = useState<{ id: string; name: string }[]>([]);
  const [iconMenu, setIconMenu] = useState<{ x: number; y: number; groupId: number } | null>(null);
  const [info, setInfo] = useState<{ title: string; lines: string[] } | null>(null);
  const [confirm, setConfirm] = useState<Confirm | null>(null);

  const nextId = useRef(1);
  const nextGroupId = useRef(1);
  const navRef = useRef<{ ids: number[]; cursor: number | null; anchor: number | null; active: boolean }>({
    ids: [],
    cursor: null,
    anchor: null,
    active: false,
  });

  const refreshAddons = () => addonsInstalled().then(setAddons).catch(() => {});
  const persistRow = (r: DownloadRow) => downloadSave(r).catch(() => {});
  const persistGroup = (g: AnimeGroup) => groupSave(g).catch(() => {});

  // Load persisted state once on startup.
  useEffect(() => {
    getSettings()
      .then((s) => {
        if (s.lang === "fr" || s.lang === "en") setLang(s.lang);
      })
      .catch(() => {});
    listFolderTemplates()
      .then(setIconTemplates)
      .catch(() => {});
    stateLoad()
      .then(({ downloads, groups: g }) => {
        const terminal = (s: DownloadRow["status"]) =>
          s === "completed" || s === "failed" || s === "stopped";
        const loaded: DownloadRow[] = downloads.map((d) => ({
          ...d,
          status: terminal(d.status) ? d.status : "stopped",
          progress: d.status === "completed" ? 1 : -1,
          speed: "",
        }));
        setRows(loaded);
        setGroups(g);
        nextId.current = loaded.reduce((m, r) => Math.max(m, r.id), 0) + 1;
        nextGroupId.current = g.reduce((m, x) => Math.max(m, x.id), 0) + 1;
      })
      .catch(() => {});
  }, []);

  useEffect(() => {
    refreshAddons();
    const ps = onProgress((e) =>
      setRows((rs) => rs.map((r) => (r.id === e.id ? applyProgress(r, e) : r))),
    );
    const fs = onFinished((e) =>
      setRows((rs) =>
        rs.map((r) => {
          if (r.id !== e.id) return r;
          const nr = applyFinished(r, e);
          persistRow(nr);
          return nr;
        }),
      ),
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

  // Regenerate an anime folder's icon with a chosen template (right-click in the sidebar).
  const applyIconFor = async (groupId: number, template: string) => {
    let g = groups.find((x) => x.id === groupId);
    const row = rows.find((r) => r.animeId === groupId);
    if (!g || !row) {
      setMessage(t("foldericon.no_folder"));
      return;
    }
    const folder = row.outPath.replace(/[/\\][^/\\]*$/, "");
    let data = g.posterData ?? undefined;
    if (!data) {
      if (!g.posterUrl) {
        setMessage(t("foldericon.no_folder"));
        return;
      }
      try {
        data = await fetchImage(g.posterUrl, g.url);
        g = { ...g, posterData: data };
        setGroups((gs) => gs.map((x) => (x.id === groupId ? g! : x)));
        persistGroup(g);
      } catch (e) {
        setMessage(String(e));
        return;
      }
    }
    setMessage(t("foldericon.generating"));
    try {
      const bin = await applyFolderIcon({ folder, posterData: data, template });
      setMessage(`${t("foldericon.applied")} — ${bin}`);
      const ng = { ...g, iconTemplate: template };
      setGroups((gs) => gs.map((x) => (x.id === groupId ? ng : x)));
      persistGroup(ng);
    } catch (e) {
      setMessage(String(e));
    }
  };

  const onLaunch = async (
    addonId: string,
    anime: Anime,
    numbers: number[],
    players: Record<number, string> = {},
    destDir = "",
    folderTemplate = "",
  ) => {
    setShowAdd(false);
    const baseDir = destDir || (await defaultDownloadDir());
    const animeDir = await joinPath(baseDir, sanitize(anime.title));
    const existing = groups.find((g) => g.url === anime.url);
    let animeId: number;
    if (existing) {
      animeId = existing.id;
    } else {
      animeId = nextGroupId.current++;
      // Store the poster image in the DB so folder-icon generation never needs the network.
      let posterData: string | undefined;
      if (anime.posterUrl) {
        try {
          posterData = await fetchImage(anime.posterUrl, anime.url);
        } catch {
          /* keep the URL; data can be fetched later */
        }
      }
      const group: AnimeGroup = {
        id: animeId,
        title: anime.title,
        url: anime.url,
        posterUrl: anime.posterUrl,
        posterData,
        expanded: true,
      };
      setGroups((gs) => [...gs, group]);
      persistGroup(group);
      if (posterData) {
        const data = posterData;
        getSettings()
          .then((s) => {
            if (s.folderIcons) {
              const tpl = folderTemplate || s.folderTemplate || "none";
              applyFolderIcon({ folder: animeDir, posterData: data, template: tpl })
                .then(() => {
                  setGroups((gs) =>
                    gs.map((x) => (x.id === animeId ? { ...x, iconTemplate: tpl } : x)),
                  );
                  persistGroup({ ...group, iconTemplate: tpl });
                })
                .catch(() => {});
            }
          })
          .catch(() => {});
      }
    }
    for (const n of numbers) {
      const ep = anime.episodes.find((e) => Math.round(e.number) === n);
      if (!ep) continue;
      const id = nextId.current++;
      const filename = sanitize(`${anime.title} - Ep ${pad(n)}.mp4`);
      const outPath = await joinPath(animeDir, filename);
      const player = players[n] ?? "";
      const row: DownloadRow = {
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
        player,
      };
      setRows((rs) => [...rs, row]);
      persistRow(row);
      startDownload({ addonId, id, episodeUrl: ep.url, playerName: player, outPath }).catch((err) =>
        setRows((rs) =>
          rs.map((r) => (r.id === id ? { ...r, status: "failed", error: String(err) } : r)),
        ),
      );
    }
  };

  const toggleGroup = (id: number) =>
    setGroups((gs) =>
      gs.map((g) => {
        if (g.id !== id) return g;
        const ng = { ...g, expanded: !g.expanded };
        persistGroup(ng);
        return ng;
      }),
    );

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
    setCursor(id);
  };

  const handleSelect = (id: number, ctrl: boolean, shift: boolean) => {
    if (shift && anchor != null) {
      const ids = visible.map((r) => r.id);
      const a = ids.indexOf(anchor);
      const b = ids.indexOf(id);
      if (a >= 0 && b >= 0) {
        const [lo, hi] = a < b ? [a, b] : [b, a];
        setSelected(new Set(ids.slice(lo, hi + 1)));
        setCursor(id);
        return;
      }
    }
    if (ctrl) {
      const s = new Set(selected);
      s.has(id) ? s.delete(id) : s.add(id);
      setSelected(s);
      setAnchor(id);
      setCursor(id);
      return;
    }
    selectSingle(id);
  };

  const selectAll = () => setSelected(new Set(visible.map((r) => r.id)));
  const invertSelection = () =>
    setSelected(new Set(visible.filter((r) => !selected.has(r.id)).map((r) => r.id)));

  const onContext = (id: number, x: number, y: number) => {
    if (!selected.has(id)) selectSingle(id);
    setMenu({ x, y });
  };

  const restart = (r: DownloadRow) => {
    startDownload({
      addonId: r.addonId,
      id: r.id,
      episodeUrl: r.pageUrl,
      playerName: r.player ?? "",
      outPath: r.outPath,
    }).catch((err) =>
      setRows((rs) =>
        rs.map((x) => (x.id === r.id ? { ...x, status: "failed", error: String(err) } : x)),
      ),
    );
    setRows((rs) =>
      rs.map((x) => {
        if (x.id !== r.id) return x;
        const nr: DownloadRow = {
          ...x,
          status: "queued",
          progress: -1,
          speed: "",
          error: undefined,
          eta: undefined,
          _tick: undefined,
        };
        persistRow(nr);
        return nr;
      }),
    );
  };

  const markStopped = (match: (r: DownloadRow) => boolean) =>
    setRows((rs) =>
      rs.map((r) => {
        if (!match(r)) return r;
        const nr: DownloadRow = { ...r, status: "stopped", speed: "", eta: undefined };
        persistRow(nr);
        return nr;
      }),
    );

  // Pause: freeze the ffmpeg process in place (resumable), don't restart.
  const stopSelected = () => {
    rows.filter((r) => selected.has(r.id) && isActive(r.status)).forEach((r) => pauseDownload(r.id));
    markStopped((r) => selected.has(r.id) && isActive(r.status));
  };

  // Resume: continue in place; only fully restart if there's nothing to continue.
  const resumeSelected = () => {
    rows
      .filter((r) => selected.has(r.id) && r.status === "stopped")
      .forEach(async (r) => {
        const continued = await resumeDownload(r.id);
        if (continued) {
          setRows((rs) =>
            rs.map((x) => {
              if (x.id !== r.id) return x;
              const nr: DownloadRow = { ...x, status: "downloading" };
              persistRow(nr);
              return nr;
            }),
          );
        } else {
          restart(r);
        }
      });
    rows
      .filter((r) => selected.has(r.id) && r.status === "failed")
      .forEach(restart);
  };

  const removeSelected = () => {
    const ids = rows.filter((r) => selected.has(r.id)).map((r) => r.id);
    rows
      .filter((r) => selected.has(r.id) && (isActive(r.status) || r.status === "stopped"))
      .forEach((r) => cancelDownload(r.id));
    setRows((rs) => rs.filter((r) => !selected.has(r.id)));
    setSelected(new Set());
    if (ids.length) downloadsDelete(ids).catch(() => {});
  };

  const removeCompleted = () => {
    const done = rows.filter((r) => r.status === "completed").map((r) => r.id);
    setRows((rs) => rs.filter((r) => r.status !== "completed"));
    if (done.length) downloadsDelete(done).catch(() => {});
    setMessage(`${done.length} ${t("status.completed_removed")}`);
  };

  const confirmStopAll = () =>
    setConfirm({
      title: t("confirm.stop_all_title"),
      message: t("confirm.stop_all_msg"),
      confirmLabel: t("toolbar.stop_all"),
      onConfirm: () => {
        pauseAll();
        markStopped((r) => isActive(r.status));
      },
    });

  const confirmDeleteAll = () =>
    setConfirm({
      title: t("confirm.delete_all_title"),
      message: t("confirm.delete_all_msg"),
      confirmLabel: t("toolbar.delete_all"),
      onConfirm: () => {
        cancelAll();
        setRows([]);
        setGroups([]);
        setSelected(new Set());
        downloadsClear().catch(() => {});
      },
    });

  navRef.current = {
    ids: visible.map((r) => r.id),
    cursor,
    anchor,
    active: view === "downloads",
  };

  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const tag = (e.target as HTMLElement | null)?.tagName;
      if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT") return;
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "f") {
        e.preventDefault();
        (document.querySelector(".tb-search input") as HTMLInputElement | null)?.focus();
        return;
      }
      const { ids, cursor, anchor, active } = navRef.current;
      if (!active || ids.length === 0) return;

      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === "a") {
        e.preventDefault();
        setSelected(new Set(ids));
        return;
      }
      if (e.key === "ArrowDown" || e.key === "ArrowUp") {
        e.preventDefault();
        const dir = e.key === "ArrowDown" ? 1 : -1;
        const at = cursor != null ? ids.indexOf(cursor) : -1;
        const next = at < 0 ? (dir > 0 ? 0 : ids.length - 1) : Math.min(ids.length - 1, Math.max(0, at + dir));
        const nextId = ids[next];
        setCursor(nextId);
        if (e.shiftKey && anchor != null) {
          const a = ids.indexOf(anchor);
          const [lo, hi] = a < next ? [a, next] : [next, a];
          setSelected(new Set(ids.slice(lo, hi + 1)));
        } else {
          setSelected(new Set([nextId]));
          setAnchor(nextId);
        }
        requestAnimationFrame(() =>
          document.querySelector(`[data-rowid="${nextId}"]`)?.scrollIntoView({ block: "nearest" }),
        );
      }
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  const sel = rows.filter((r) => selected.has(r.id));
  const canStop = sel.some((r) => isActive(r.status));
  const canResume = sel.some((r) => r.status === "stopped" || r.status === "failed");
  const canDelete = sel.length > 0;
  const anyActive = rows.some((r) => isActive(r.status));
  const anyRows = rows.length > 0;

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
          canStop,
          canResume,
          canDelete,
          anyActive,
          anyRows,
          toggleSidebar: () => setSidebarOn((v) => !v),
          sidebarOn,
          toggleSearch: () => setMessage(t("toolbar.search_hint")),
          openAddons: () => setView("addons"),
          setLang: changeLang,
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
        canStop={canStop}
        canResume={canResume}
        canDelete={canDelete}
        anyActive={anyActive}
        anyRows={anyRows}
        onOpenAddons={() => setView(view === "addons" ? "downloads" : "addons")}
        onOpenSettings={() => setShowSettings(true)}
        addonsOpen={view === "addons"}
        soon={soon}
        search={search}
        onSearch={setSearch}
      />
      {view === "addons" ? (
        <div className="main">
          <div className="content scroll">
            <AddonsScreen installed={addons} onChange={refreshAddons} t={t} />
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
                  onAnimeContext={(groupId, x, y) => setIconMenu({ x, y, groupId })}
                  t={t}
                />
              </div>
              <div className="splitter" onMouseDown={startDrag} />
            </>
          )}
          <div className="content">
            <DownloadsTable
              rows={visible}
              selected={selected}
              onSelect={handleSelect}
              onContext={onContext}
              t={t}
            />
          </div>
        </div>
      )}
      <StatusBar rows={rows} message={message} t={t} />

      {iconMenu && (
        <ContextMenu
          x={iconMenu.x}
          y={iconMenu.y}
          onClose={() => setIconMenu(null)}
          items={[
            { key: "_h", label: t("foldericon.change_model"), disabled: true },
            ...iconTemplates.map((tp) => ({
              key: tp.id,
              label:
                (groups.find((g) => g.id === iconMenu.groupId)?.iconTemplate === tp.id
                  ? "✓ "
                  : "") + tp.name,
              onClick: () => applyIconFor(iconMenu.groupId, tp.id),
            })),
          ]}
        />
      )}

      {menu && (
        <ContextMenu
          x={menu.x}
          y={menu.y}
          onClose={() => setMenu(null)}
          items={
            [
              { key: "resume", label: t("toolbar.resume"), onClick: resumeSelected, disabled: !canResume },
              { key: "stop", label: t("toolbar.stop"), onClick: stopSelected, disabled: !canStop },
              { key: "del", label: t("toolbar.delete"), onClick: removeSelected, disabled: !canDelete },
              { key: "s1", sep: true },
              { key: "all", label: t("ctx.select_all"), onClick: selectAll, disabled: !anyRows },
              { key: "inv", label: t("ctx.invert"), onClick: invertSelection, disabled: !anyRows },
            ] as CtxItem[]
          }
        />
      )}
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
      {showSettings && <SettingsDialog onClose={() => setShowSettings(false)} t={t} />}
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
