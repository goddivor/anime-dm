import { useEffect, useRef, useState, type MouseEvent as ReactMouseEvent } from "react";
import {
  Download,
  Loader2,
  Search,
  Puzzle,
  Type,
  LayoutGrid,
  Settings,
  FolderOpen,
} from "lucide-react";
import type { Anime, Hoster, InstalledAddon } from "../types";
import type { T } from "../i18n";
import {
  loadAnime,
  addonIcon,
  listHosters,
  addonPreferences,
  defaultDownloadDir,
  pickDirectory,
  getSettings,
  listFolderTemplates,
} from "../api";
import { parseSelection } from "../format";
import Poster from "./Poster";
import ContextMenu, { type CtxItem } from "./ContextMenu";

type EpMode = "text" | "list";
type Menu =
  | { kind: "global"; x: number; y: number }
  | { kind: "episode"; x: number; y: number; idx: number; hosters: Hoster[] | null };
const PREF_PLAYER = "preferred_player";
const AUTO = "Auto";

/// Collapse a sorted list of numbers into a compact selection string ("1-5,8,10-12").
function collapseRanges(nums: number[]): string {
  if (nums.length === 0) return "";
  const parts: string[] = [];
  let start = nums[0];
  let prev = nums[0];
  for (let i = 1; i < nums.length; i++) {
    if (nums[i] === prev + 1) {
      prev = nums[i];
      continue;
    }
    parts.push(start === prev ? `${start}` : `${start}-${prev}`);
    start = prev = nums[i];
  }
  parts.push(start === prev ? `${start}` : `${start}-${prev}`);
  return parts.join(",");
}

export default function AddDialog({
  addons,
  onClose,
  onLaunch,
  onOpenAddons,
  t,
}: {
  addons: InstalledAddon[];
  onClose: () => void;
  onLaunch: (
    addonId: string,
    anime: Anime,
    numbers: number[],
    players: Record<number, string>,
    destDir: string,
    folderTemplate: string,
  ) => void;
  onOpenAddons: () => void;
  t: T;
}) {
  const [addonId, setAddonId] = useState(addons[0]?.id ?? "");
  const [icons, setIcons] = useState<Record<string, string>>({});
  const [searchOpen, setSearchOpen] = useState(false);
  const [query, setQuery] = useState("");
  const [url, setUrl] = useState("");
  const [loading, setLoading] = useState(false);
  const [anime, setAnime] = useState<Anime | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [mode, setMode] = useState<EpMode>("text");
  const [selection, setSelection] = useState("");
  const [picked, setPicked] = useState<Set<number>>(new Set());
  const [anchor, setAnchor] = useState<number | null>(null);
  const [playerOptions, setPlayerOptions] = useState<string[]>([]);
  const [globalPlayer, setGlobalPlayer] = useState(AUTO);
  const [playerByEp, setPlayerByEp] = useState<Record<number, string>>({});
  const [menu, setMenu] = useState<Menu | null>(null);
  const [destDir, setDestDir] = useState("");
  const [folderIconsOn, setFolderIconsOn] = useState(false);
  const [iconTemplates, setIconTemplates] = useState<{ id: string; name: string }[]>([]);
  const [folderTemplate, setFolderTemplate] = useState("");
  const [pos, setPos] = useState({ x: 0, y: 0 });
  const drag = useRef<{ sx: number; sy: number; px: number; py: number } | null>(null);

  useEffect(() => {
    defaultDownloadDir()
      .then(setDestDir)
      .catch(() => {});
    getSettings()
      .then((s) => setFolderIconsOn(s.folderIcons))
      .catch(() => {});
    listFolderTemplates()
      .then(setIconTemplates)
      .catch(() => {});
  }, []);

  const pickDest = async () => {
    const d = await pickDirectory(destDir || undefined);
    if (d) setDestDir(d);
  };

  useEffect(() => {
    addons.forEach((a) => {
      addonIcon(a.id)
        .then((d) => {
          if (d) setIcons((m) => ({ ...m, [a.id]: d }));
        })
        .catch(() => {});
    });
  }, [addons]);

  // Player options come from the addon's "preferred_player" preference.
  useEffect(() => {
    setGlobalPlayer(AUTO);
    if (!addonId) return;
    addonPreferences(addonId)
      .then((prefs) => {
        const p = prefs.find((x) => x.key === PREF_PLAYER);
        setPlayerOptions(p?.options ?? []);
      })
      .catch(() => setPlayerOptions([]));
  }, [addonId]);

  // Reset the episode selection whenever a new anime is resolved.
  useEffect(() => {
    if (anime) {
      setPicked(new Set(anime.episodes.map((_, i) => i)));
      setAnchor(null);
      setSelection(
        collapseRanges(anime.episodes.map((e) => Math.round(e.number)).sort((a, b) => a - b)),
      );
      setPlayerByEp({});
    }
  }, [anime]);

  // Drag the modal by its header.
  const onHeadDown = (e: ReactMouseEvent) => {
    if ((e.target as HTMLElement).closest("button")) return;
    drag.current = { sx: e.clientX, sy: e.clientY, px: pos.x, py: pos.y };
    const move = (ev: MouseEvent) => {
      if (!drag.current) return;
      setPos({
        x: drag.current.px + ev.clientX - drag.current.sx,
        y: drag.current.py + ev.clientY - drag.current.sy,
      });
    };
    const up = () => {
      drag.current = null;
      window.removeEventListener("mousemove", move);
      window.removeEventListener("mouseup", up);
    };
    window.addEventListener("mousemove", move);
    window.addEventListener("mouseup", up);
  };

  const dragStyle = { transform: `translate(${pos.x}px, ${pos.y}px)` };

  if (addons.length === 0) {
    return (
      <div className="modal-backdrop" onMouseDown={onClose}>
        <div className="modal sm" style={dragStyle} onMouseDown={(e) => e.stopPropagation()}>
          <div className="modal-head drag" onMouseDown={onHeadDown}>
            <span>{t("dialog.add.title")}</span>
            <button className="icon-btn" onClick={onClose}>
              ✕
            </button>
          </div>
          <div className="modal-body">
            <div className="muted">{t("dialog.add.no_addon")}</div>
            <div className="modal-foot">
              <button className="btn primary" onClick={onOpenAddons}>
                {t("dialog.add.open_store")}
              </button>
            </div>
          </div>
        </div>
      </div>
    );
  }

  const validate = async () => {
    const u = url.trim();
    if (!u || !addonId) return;
    setLoading(true);
    setError(null);
    setAnime(null);
    try {
      setAnime(await loadAnime(addonId, u));
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  };

  const toggle = (i: number, shift: boolean) => {
    setPicked((prev) => {
      const next = new Set(prev);
      if (shift && anchor !== null) {
        const [a, b] = anchor < i ? [anchor, i] : [i, anchor];
        for (let k = a; k <= b; k++) next.add(k);
      } else if (next.has(i)) {
        next.delete(i);
      } else {
        next.add(i);
      }
      return next;
    });
    setAnchor(i);
  };

  // Open the menu instantly with a loading state, then fill it once resolved.
  const onCellContext = (e: ReactMouseEvent, idx: number) => {
    e.preventDefault();
    if (!anime) return;
    setMenu({ kind: "episode", x: e.clientX, y: e.clientY, idx, hosters: null });
    listHosters(addonId, anime.episodes[idx].url)
      .then((hosters) =>
        setMenu((m) => (m && m.kind === "episode" && m.idx === idx ? { ...m, hosters } : m)),
      )
      .catch(() =>
        setMenu((m) => (m && m.kind === "episode" && m.idx === idx ? { ...m, hosters: [] } : m)),
      );
  };

  const openGlobalMenu = (e: ReactMouseEvent) => {
    const r = (e.currentTarget as HTMLElement).getBoundingClientRect();
    setMenu({ kind: "global", x: r.left, y: r.bottom + 4 });
  };

  // Keep the text and visual selections in sync when switching mode.
  const switchMode = (m: EpMode) => {
    if (m === mode || !anime) {
      setMode(m);
      return;
    }
    if (m === "list") {
      const nums =
        selection.trim() === "" ? [] : parseSelection(selection, anime.episodes.length);
      const set = new Set(nums);
      setPicked(
        new Set(
          anime.episodes
            .map((_, i) => i)
            .filter((i) => set.has(Math.round(anime.episodes[i].number))),
        ),
      );
    } else {
      const nums = [...picked].map((i) => Math.round(anime.episodes[i].number)).sort((a, b) => a - b);
      setSelection(collapseRanges(nums));
    }
    setMode(m);
  };

  const effPlayer = (i: number) => playerByEp[i] ?? (globalPlayer !== AUTO ? globalPlayer : "");

  const numbers = !anime
    ? []
    : mode === "list"
      ? [...picked].sort((a, b) => a - b).map((i) => Math.round(anime.episodes[i].number))
      : selection.trim() === ""
        ? []
        : parseSelection(selection, anime.episodes.length);

  const launch = () => {
    if (!anime) return;
    const players: Record<number, string> = {};
    anime.episodes.forEach((e, i) => {
      const p = effPlayer(i);
      if (p) players[Math.round(e.number)] = p;
    });
    onLaunch(addonId, anime, numbers, players, destDir, folderTemplate);
  };

  const q = query.trim().toLowerCase();
  const shown = addons.filter(
    (a) => !q || a.name.toLowerCase().includes(q) || a.lang.toLowerCase().includes(q),
  );

  const menuItems: CtxItem[] = !menu
    ? []
    : menu.kind === "global"
      ? playerOptions.map((p) => ({
          key: p,
          label: (p === globalPlayer ? "● " : "") + (p === AUTO ? t("dialog.add.player_auto") : p),
          onClick: () => setGlobalPlayer(p),
        }))
      : menu.hosters === null
        ? [{ key: "_loading", label: t("dialog.add.loading_players"), disabled: true }]
        : menu.hosters.length === 0
          ? [{ key: "_none", label: t("dialog.add.no_player"), disabled: true }]
          : (() => {
              const idx = menu.idx;
              const cur =
                playerByEp[idx] ?? (globalPlayer !== AUTO ? globalPlayer : menu.hosters[0]?.name);
              return menu.hosters.map((h) => ({
                key: h.name,
                label: (h.name === cur ? "● " : "") + h.name,
                onClick: () => setPlayerByEp((p) => ({ ...p, [idx]: h.name })),
              }));
            })();

  return (
    <div className="modal-backdrop" onMouseDown={onClose}>
      <div className="modal" style={dragStyle} onMouseDown={(e) => e.stopPropagation()}>
        <div className="modal-head drag" onMouseDown={onHeadDown}>
          <span>{t("dialog.add.title")}</span>
          <button className="icon-btn" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">
          <label className="field-label">{t("dialog.add.source_label")}</label>
          <div className="src-row">
            <div className="src-strip">
              {shown.map((a) => (
                <button
                  key={a.id}
                  className={"src-icon" + (a.id === addonId ? " active" : "")}
                  title={`${a.name} (${a.lang})`}
                  onClick={() => setAddonId(a.id)}
                >
                  {icons[a.id] ? <img src={icons[a.id]} alt="" /> : <Puzzle size={20} />}
                </button>
              ))}
            </div>
            <button
              className={"icon-btn" + (searchOpen ? " active" : "")}
              title={t("dialog.add.source_search")}
              onClick={() => setSearchOpen((v) => !v)}
            >
              <Search size={18} />
            </button>
          </div>
          {searchOpen && (
            <input
              autoFocus
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder={t("dialog.add.source_search")}
            />
          )}
          {playerOptions.length > 0 && (
            <div className="row">
              <button className="icon-btn" title={t("dialog.add.player_label")} onClick={openGlobalMenu}>
                <Settings size={18} />
              </button>
              <span className="muted small">
                {t("dialog.add.player_label")} {globalPlayer}
              </span>
            </div>
          )}

          <label className="field-label">{t("dialog.add.link_label")}</label>
          <div className="row">
            <input
              className="grow"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && validate()}
              placeholder="https://…/anime/…"
            />
            <button className="btn" onClick={validate}>
              {t("dialog.add.confirm")}
            </button>
          </div>

          {loading && (
            <div className="row muted">
              <Loader2 size={16} className="spin" /> {t("dialog.add.fetching")}
            </div>
          )}
          {error && <div className="err-box">{error}</div>}

          {anime && (
            <>
              <div className="sep" />
              <div className="anime-head">
                <Poster
                  url={anime.posterUrl}
                  referer={anime.url}
                  className="anime-poster"
                  fallback={null}
                  zoomable
                />
                <div>
                  <div className="anime-title">{anime.title}</div>
                  <div className="muted">
                    {anime.episodes.length} {t("dialog.add.episodes_count")}
                  </div>
                </div>
              </div>

              <div className="ep-toolbar">
                <label className="field-label">{t("dialog.add.episodes_label")}</label>
                <div className="seg">
                  <button
                    className={mode === "text" ? "active" : ""}
                    onClick={() => switchMode("text")}
                  >
                    <Type size={14} /> {t("dialog.add.episodes_mode_text")}
                  </button>
                  <button
                    className={mode === "list" ? "active" : ""}
                    onClick={() => switchMode("list")}
                  >
                    <LayoutGrid size={14} /> {t("dialog.add.episodes_mode_list")}
                  </button>
                </div>
              </div>

              {mode === "text" ? (
                <input
                  value={selection}
                  onChange={(e) => setSelection(e.target.value)}
                  placeholder="ex : 1-20  ·  1,5,8"
                />
              ) : (
                <>
                  <div className="row">
                    <button
                      className="btn"
                      onClick={() => setPicked(new Set(anime.episodes.map((_, i) => i)))}
                    >
                      {t("dialog.add.select_all")}
                    </button>
                    <button className="btn" onClick={() => setPicked(new Set())}>
                      {t("dialog.add.select_none")}
                    </button>
                    <span className="grow" />
                    <span className="muted">
                      {picked.size} / {anime.episodes.length}
                    </span>
                  </div>
                  <div className="ep-grid">
                    {anime.episodes.map((e, i) => (
                      <button
                        key={i}
                        className={
                          "ep-cell" + (picked.has(i) ? " on" : "") + (playerByEp[i] ? " custom" : "")
                        }
                        title={playerByEp[i] ? `${e.name} — ${playerByEp[i]}` : e.name}
                        onClick={(ev: ReactMouseEvent) => toggle(i, ev.shiftKey)}
                        onContextMenu={(ev: ReactMouseEvent) => onCellContext(ev, i)}
                      >
                        {Math.round(e.number) <= 0 && /\bfilm\b|\bmovie\b/i.test(e.name)
                          ? t("table.movie")
                          : String(e.number)}
                      </button>
                    ))}
                  </div>
                  <div className="muted small">{t("dialog.add.episode_player_hint")}</div>
                </>
              )}

              {folderIconsOn && iconTemplates.length > 0 && (
                <div className="row">
                  <FolderOpen size={14} className="muted" />
                  <span className="muted small">{t("dialog.add.folder_icon")}</span>
                  <select
                    className="grow"
                    value={folderTemplate}
                    onChange={(e) => setFolderTemplate(e.target.value)}
                  >
                    <option value="">{t("dialog.add.folder_icon_default")}</option>
                    {iconTemplates.map((tp) => (
                      <option key={tp.id} value={tp.id}>
                        {tp.name}
                      </option>
                    ))}
                  </select>
                </div>
              )}

              <div className="modal-foot dl-foot">
                <button
                  className="btn dest-btn"
                  title={`${t("dialog.add.dest_label")} : ${destDir}`}
                  onClick={pickDest}
                >
                  <FolderOpen size={16} />
                  <span className="dest-path">{destDir || t("dialog.add.dest_label")}</span>
                </button>
                <button className="btn primary" disabled={numbers.length === 0} onClick={launch}>
                  <Download size={16} /> {t("dialog.add.download_btn")} ({numbers.length})
                </button>
              </div>
            </>
          )}
        </div>
      </div>
      {menu && (
        <>
          <div
            className="ctx-overlay"
            onMouseDown={(e) => {
              e.stopPropagation();
              setMenu(null);
            }}
            onContextMenu={(e) => {
              e.preventDefault();
              e.stopPropagation();
              setMenu(null);
            }}
          />
          <ContextMenu x={menu.x} y={menu.y} items={menuItems} onClose={() => setMenu(null)} />
        </>
      )}
    </div>
  );
}
