import { useState } from "react";
import { Download, Loader2 } from "lucide-react";
import type { Anime, InstalledAddon } from "../types";
import type { T } from "../i18n";
import { addonHosters, loadAnime } from "../api";
import { parseSelection } from "../format";
import Poster from "./Poster";

export default function AddDialog({
  addons,
  onClose,
  onLaunch,
  onOpenAddons,
  t,
}: {
  addons: InstalledAddon[];
  onClose: () => void;
  onLaunch: (addonId: string, anime: Anime, numbers: number[], player: string) => void;
  onOpenAddons: () => void;
  t: T;
}) {
  const [addonId, setAddonId] = useState(addons[0]?.id ?? "");
  const [url, setUrl] = useState("");
  const [loading, setLoading] = useState(false);
  const [anime, setAnime] = useState<Anime | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [selection, setSelection] = useState("");
  const [players, setPlayers] = useState<string[]>([]);
  const [player, setPlayer] = useState("");

  if (addons.length === 0) {
    return (
      <div className="modal-backdrop" onMouseDown={onClose}>
        <div className="modal sm" onMouseDown={(e) => e.stopPropagation()}>
          <div className="modal-head">
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
    setPlayers([]);
    setPlayer("");
    try {
      const a = await loadAnime(addonId, u);
      setAnime(a);
      if (a.episodes.length > 0) {
        addonHosters(addonId, a.episodes[0].url)
          .then((hs) => setPlayers(hs.map((h) => h.name)))
          .catch(() => {});
      }
    } catch (e) {
      setError(String(e));
    } finally {
      setLoading(false);
    }
  };

  const numbers = !anime
    ? []
    : selection.trim() === ""
      ? anime.episodes.map((e) => Math.round(e.number))
      : parseSelection(selection, anime.episodes.length);

  return (
    <div className="modal-backdrop" onMouseDown={onClose}>
      <div className="modal" onMouseDown={(e) => e.stopPropagation()}>
        <div className="modal-head">
          <span>{t("dialog.add.title")}</span>
          <button className="icon-btn" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">
          <label className="field-label">{t("dialog.add.source_label")}</label>
          <select value={addonId} onChange={(e) => setAddonId(e.target.value)}>
            {addons.map((a) => (
              <option key={a.id} value={a.id}>
                {a.name} ({a.lang})
              </option>
            ))}
          </select>

          <label className="field-label">{t("dialog.add.link_label")}</label>
          <div className="row">
            <input
              className="grow"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && validate()}
              placeholder="https://…/anime/…"
              autoFocus
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

              <label className="field-label">{t("dialog.add.episodes_label")}</label>
              <input
                value={selection}
                onChange={(e) => setSelection(e.target.value)}
                placeholder="ex : 1-20  ·  1,5,8  ·  vide = tout"
              />

              <label className="field-label">{t("dialog.add.player_label")}</label>
              <select value={player} onChange={(e) => setPlayer(e.target.value)}>
                <option value="">{t("dialog.add.player_auto")}</option>
                {players.map((p) => (
                  <option key={p} value={p}>
                    {p}
                  </option>
                ))}
              </select>

              <div className="modal-foot">
                <button
                  className="btn primary"
                  disabled={numbers.length === 0}
                  onClick={() => onLaunch(addonId, anime, numbers, player)}
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
