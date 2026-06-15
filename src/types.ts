export type Episode = { number: number; name: string; url: string };

export type Hoster = { url: string; name: string };

export type Anime = {
  title: string;
  url: string;
  posterUrl?: string | null;
  episodes: Episode[];
};

export type DownloadStatus =
  | "queued"
  | "resolving"
  | "downloading"
  | "completed"
  | "failed"
  | "stopped";

export type Queue = "main" | "sync";

export type DownloadRow = {
  id: number;
  addonId: string;
  animeId: number;
  animeTitle: string;
  episodeNumber: number;
  filename: string;
  pageUrl: string;
  queue: Queue;
  status: DownloadStatus;
  progress: number; // -1 = inconnu, sinon 0..1
  speed: string;
  sizeBytes?: number;
  addedAt: number;
  lastTry?: number;
  outPath: string;
  address?: string;
  eta?: number; // secondes restantes
  error?: string;
  player?: string; // lecteur choisi (vide = préféré de l'extension)
  _tick?: { at: number; p: number };
};

export type AnimeGroup = {
  id: number;
  title: string;
  url: string;
  posterUrl?: string | null;
  posterData?: string | null; // affiche stockée (data URL) pour l'icône hors-ligne
  iconTemplate?: string | null; // modèle d'icône actuellement appliqué
  expanded: boolean;
};

/// Persisted subset of a DownloadRow (what SQLite stores).
export type DownloadRecord = {
  id: number;
  addonId: string;
  animeId: number;
  animeTitle: string;
  episodeNumber: number;
  filename: string;
  pageUrl: string;
  queue: Queue;
  status: DownloadStatus;
  sizeBytes?: number;
  addedAt: number;
  lastTry?: number;
  outPath: string;
  address?: string;
  error?: string;
};

export type Filter =
  | { kind: "all" }
  | { kind: "anime"; id: number }
  | { kind: "queue"; queue: Queue };

export type InstalledAddon = {
  id: string;
  name: string;
  lang: string;
  version: string;
  nsfw: boolean;
  iconPath?: string | null;
};

export type RepoInfo = {
  url: string;
  name: string;
  website?: string | null;
  iconUrl?: string | null;
  disabled: boolean;
};

export type StoreEntry = {
  id: string;
  name: string;
  lang: string;
  version: string;
  nsfw: boolean;
  wasm: string;
  icon?: string | null;
  repoUrl: string;
  iconUrl?: string | null;
  installed: boolean;
};

export type PreferenceKind = "text" | "select" | "bool";

export type Preference = {
  key: string;
  title: string;
  summary?: string | null;
  default: string;
  type: PreferenceKind;
  options: string[];
};
