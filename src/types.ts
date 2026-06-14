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
  _tick?: { at: number; p: number };
};

export type AnimeGroup = {
  id: number;
  title: string;
  url: string;
  posterUrl?: string | null;
  expanded: boolean;
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
