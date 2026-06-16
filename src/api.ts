import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { downloadDir, join } from "@tauri-apps/api/path";
import { open } from "@tauri-apps/plugin-dialog";
import { openUrl } from "@tauri-apps/plugin-opener";
import type {
  Anime,
  AnimeGroup,
  DownloadRecord,
  DownloadRow,
  Hoster,
  InstalledAddon,
  Preference,
  RepoInfo,
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
export const getSettings = () =>
  invoke<{
    repos: string[];
    lang: string;
    folderIcons: boolean;
    folderTemplate: string;
    lastDir: string;
    skipDeleteConfirm: boolean;
  }>("get_settings");
export const setLangPref = (lang: string) => invoke<void>("set_lang", { lang });
export const setLastDir = (dir: string) => invoke<void>("set_last_dir", { dir });
export const setSkipDeleteConfirm = (skip: boolean) =>
  invoke<void>("set_skip_delete_confirm", { skip });
export const deleteDiskFiles = (paths: string[]) => invoke<void>("delete_files", { paths });
export const createDir = (path: string) => invoke<void>("create_dir", { path });
export const setFolderIcons = (enabled: boolean, template: string) =>
  invoke<void>("set_folder_icons", { enabled, template });
export const listFolderTemplates = () =>
  invoke<{ id: string; name: string }[]>("list_folder_templates");
export const applyFolderIcon = (p: { folder: string; posterData: string; template: string }) =>
  invoke<string>("apply_folder_icon", p);
export const addRepo = (url: string) => invoke<void>("add_repo", { url });
export const removeRepo = (url: string) => invoke<void>("remove_repo", { url });
export const listRepos = () => invoke<RepoInfo[]>("list_repos");
export const setRepoDisabled = (url: string, disabled: boolean) =>
  invoke<void>("set_repo_disabled", { url, disabled });
export const openExternal = (url: string) => openUrl(url);
export const openFile = (path: string) => invoke<void>("open_file", { path });
export const openFolder = (path: string) => invoke<void>("open_folder", { path });
export const openWith = (path: string, withApp: string | null) =>
  invoke<void>("open_with", { path, with: withApp });
export const listApps = (path: string) =>
  invoke<{ name: string; id: string }[]>("list_apps", { path });
export const openEpisodes = (paths: string[]) => invoke<void>("open_episodes", { paths });
export const deleteAnimeFiles = (paths: string[]) => invoke<void>("delete_anime_files", { paths });

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

export const listHosters = (addonId: string, url: string) =>
  invoke<Hoster[]>("list_hosters", { addonId, url });

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
export const groupDelete = (id: number) => invoke<void>("group_delete", { id });

export async function defaultOutPath(filename: string): Promise<string> {
  const dir = await downloadDir();
  return join(dir, filename);
}

export const defaultDownloadDir = () => downloadDir();
export const joinPath = (dir: string, filename: string) => join(dir, filename);
export const pickDirectory = (defaultPath?: string) =>
  open({ directory: true, defaultPath }) as Promise<string | null>;
export const pickApplication = () =>
  open({ directory: false, multiple: false }) as Promise<string | null>;
