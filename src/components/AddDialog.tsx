import { useState } from "react";
import { Download, Loader2 } from "lucide-react";
import type { Anime } from "../types";
import type { T } from "../i18n";
import { loadAnime } from "../api";
import { parseSelection } from "../format";
import Poster from "./Poster";

const PLAYERS: [string, string][] = [
  ["LECTEUR myTV", "rapide"],
  ["LECTEUR FHD1", "rapide"],
  ["LECTEUR Stape", "rapide"],
  ["LECTEUR VOE", "navigateur"],
  ["LECTEUR MOON", "navigateur ?"],
  ["LECTEUR SB", "navigateur ?"],
  ["LECTEUR YU", "navigateur ?"],
];

export default function AddDialog({
  onClose,
  onLaunch,
  t,
}: {
  onClose: () => void;
  onLaunch: (anime: Anime, numbers: number[], player: string) => void;
  t: T;
}) {
  const [url, setUrl] = useState("");
  const [loading, setLoading] = useState(false);
  const [anime, setAnime] = useState<Anime | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [selection, setSelection] = useState("");
  const [player, setPlayer] = useState(PLAYERS[0][0]);

  const validate = async () => {
    const u = url.trim();
    if (!u) return;
    setLoading(true);
    setError(null);
    setAnime(null);
    try {
      const a = await loadAnime(u);
      setAnime(a);
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
          <label className="field-label">{t("dialog.add.link_label")}</label>
          <div className="row">
            <input
              className="grow"
              value={url}
              onChange={(e) => setUrl(e.target.value)}
              onKeyDown={(e) => e.key === "Enter" && validate()}
              placeholder="https://voir-anime.to/anime/dragon-ball-vf/"
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
                <Poster url={anime.posterUrl} className="anime-poster" fallback={null} />
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
                {PLAYERS.map(([p, hint]) => (
                  <option key={p} value={p}>
                    {p} — {hint}
                  </option>
                ))}
              </select>

              <div className="modal-foot">
                <button
                  className="btn primary"
                  disabled={numbers.length === 0}
                  onClick={() => onLaunch(anime, numbers, player)}
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
