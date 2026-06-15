import { useEffect, useRef, useState, type MouseEvent as ReactMouseEvent } from "react";
import { Download, Loader2, Search, Puzzle, Type, LayoutGrid } from "lucide-react";
import type { Anime, InstalledAddon } from "../types";
import type { T } from "../i18n";
import { loadAnime, addonIcon } from "../api";
import { parseSelection } from "../format";
import Poster from "./Poster";

type EpMode = "text" | "list";

export default function AddDialog({
  addons,
  onClose,
  onLaunch,
  onOpenAddons,
  t,
}: {
  addons: InstalledAddon[];
  onClose: () => void;
  onLaunch: (addonId: string, anime: Anime, numbers: number[]) => void;
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
  const [pos, setPos] = useState({ x: 0, y: 0 });
  const drag = useRef<{ sx: number; sy: number; px: number; py: number } | null>(null);

  useEffect(() => {
    addons.forEach((a) => {
      addonIcon(a.id)
        .then((d) => {
          if (d) setIcons((m) => ({ ...m, [a.id]: d }));
        })
        .catch(() => {});
    });
  }, [addons]);

  // Reset the episode selection whenever a new anime is resolved.
  useEffect(() => {
    if (anime) {
      setPicked(new Set(anime.episodes.map((_, i) => i)));
      setAnchor(null);
      setSelection("");
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

  const numbers = !anime
    ? []
    : mode === "list"
      ? [...picked].sort((a, b) => a - b).map((i) => Math.round(anime.episodes[i].number))
      : selection.trim() === ""
        ? anime.episodes.map((e) => Math.round(e.number))
        : parseSelection(selection, anime.episodes.length);

  const active = addons.find((a) => a.id === addonId);
  const q = query.trim().toLowerCase();
  const shown = addons.filter(
    (a) => !q || a.name.toLowerCase().includes(q) || a.lang.toLowerCase().includes(q),
  );

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
          {active && <div className="muted small">{`${active.name} (${active.lang})`}</div>}

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
                    onClick={() => setMode("text")}
                  >
                    <Type size={14} /> {t("dialog.add.episodes_mode_text")}
                  </button>
                  <button
                    className={mode === "list" ? "active" : ""}
                    onClick={() => setMode("list")}
                  >
                    <LayoutGrid size={14} /> {t("dialog.add.episodes_mode_list")}
                  </button>
                </div>
              </div>

              {mode === "text" ? (
                <input
                  value={selection}
                  onChange={(e) => setSelection(e.target.value)}
                  placeholder="ex : 1-20  ·  1,5,8  ·  vide = tout"
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
                        className={"ep-cell" + (picked.has(i) ? " on" : "")}
                        title={e.name}
                        onClick={(ev: ReactMouseEvent) => toggle(i, ev.shiftKey)}
                      >
                        {String(e.number)}
                      </button>
                    ))}
                  </div>
                </>
              )}

              <div className="modal-foot">
                <button
                  className="btn primary"
                  disabled={numbers.length === 0}
                  onClick={() => onLaunch(addonId, anime, numbers)}
                >
                  <Download size={16} /> {t("dialog.add.download_btn")} ({numbers.length})
                </button>
              </div>
            </>
          )}
        </div>
      </div>
    </div>
  );
}
