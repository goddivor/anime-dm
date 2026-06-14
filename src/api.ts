import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { downloadDir, join } from "@tauri-apps/api/path";
import type {
  Anime,
  AnimeGroup,
  DownloadRecord,
  DownloadRow,
  InstalledAddon,
  Preference,
  StoreEntry,
} from "./types";

export type ProgressEvent = {
  id: number;
  status: string;
  progress: number | null;
  speed: string | null;
  sizeBytes: number | null;
  address: string | null;
};

export type FinishedEvent = {
  id: number;
  ok: boolean;
  error: string | null;
  path: string | null;
};

// --- Settings & repos ---
export const getSettings = () => invoke<{ repos: string[] }>("get_settings");
export const addRepo = (url: string) => invoke<void>("add_repo", { url });
export const removeRepo = (url: string) => invoke<void>("remove_repo", { url });

// --- Store ---
export const storeFetch = () => invoke<StoreEntry[]>("store_fetch");
export const storeInstall = (repoUrl: string, id: string) =>
  invoke<InstalledAddon>("store_install", { repoUrl, id });

// --- Installed addons ---
export const addonsInstalled = () => invoke<InstalledAddon[]>("addons_installed");
export const addonRemove = (id: string) => invoke<void>("addon_remove", { id });
export const addonIcon = (id: string) => invoke<string | null>("addon_icon", { id });
export const addonPreferences = (id: string) => invoke<Preference[]>("addon_preferences", { id });
export const addonGetConfig = (id: string) =>
  invoke<Record<string, string>>("addon_get_config", { id });
export const addonSetConfig = (id: string, config: Record<string, string>) =>
  invoke<void>("addon_set_config", { id, config });

// --- Source operations (through an addon) ---
export const loadAnime = (addonId: string, url: string) =>
  invoke<Anime>("load_anime", { addonId, url });

export const startDownload = (p: {
  addonId: string;
  id: number;
  episodeUrl: string;
  playerName: string;
  outPath: string;
}) => invoke<void>("start_download", p);

export const fetchImage = (url: string, referer?: string) =>
  invoke<string>("fetch_image", { url, referer });

export const pauseDownload = (id: number) => invoke<void>("pause_download", { id });
export const pauseAll = () => invoke<void>("pause_all");
export const resumeDownload = (id: number) => invoke<boolean>("resume_download", { id });
export const cancelDownload = (id: number) => invoke<void>("cancel_download", { id });
export const cancelAll = () => invoke<void>("cancel_all");

export const onProgress = (cb: (e: ProgressEvent) => void): Promise<UnlistenFn> =>
  listen<ProgressEvent>("download://progress", (e) => cb(e.payload));

export const onFinished = (cb: (e: FinishedEvent) => void): Promise<UnlistenFn> =>
  listen<FinishedEvent>("download://finished", (e) => cb(e.payload));

// --- Persistence (SQLite) ---
export const stateLoad = () =>
  invoke<{ downloads: DownloadRecord[]; groups: AnimeGroup[] }>("state_load");
export const downloadSave = (record: DownloadRow) => invoke<void>("download_save", { record });
export const downloadsDelete = (ids: number[]) => invoke<void>("downloads_delete", { ids });
export const downloadsClear = () => invoke<void>("downloads_clear");
export const groupSave = (record: AnimeGroup) => invoke<void>("group_save", { record });

export async function defaultOutPath(filename: string): Promise<string> {
  const dir = await downloadDir();
  return join(dir, filename);
}
