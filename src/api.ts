import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { downloadDir, join } from "@tauri-apps/api/path";
import type { Anime, InstalledAddon, Preference, StoreEntry } from "./types";

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

// --- Settings & store ---
export const getSettings = () => invoke<{ repoUrl: string }>("get_settings");
export const setRepoUrl = (url: string) => invoke<void>("set_repo_url", { url });
export const storeFetch = () => invoke<StoreEntry[]>("store_fetch");
export const storeInstall = (id: string) => invoke<InstalledAddon>("store_install", { id });

// --- Installed addons ---
export const addonsInstalled = () => invoke<InstalledAddon[]>("addons_installed");
export const addonRemove = (id: string) => invoke<void>("addon_remove", { id });
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

export const stopDownload = (id: number) => invoke<void>("stop_download", { id });
export const stopAll = () => invoke<void>("stop_all");

export const onProgress = (cb: (e: ProgressEvent) => void): Promise<UnlistenFn> =>
  listen<ProgressEvent>("download://progress", (e) => cb(e.payload));

export const onFinished = (cb: (e: FinishedEvent) => void): Promise<UnlistenFn> =>
  listen<FinishedEvent>("download://finished", (e) => cb(e.payload));

export async function defaultOutPath(filename: string): Promise<string> {
  const dir = await downloadDir();
  return join(dir, filename);
}
