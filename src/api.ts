import { invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";
import { downloadDir, join } from "@tauri-apps/api/path";
import type { Anime } from "./types";

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

export const loadAnime = (url: string) => invoke<Anime>("load_anime", { url });

export const fetchImage = (url: string) => invoke<string>("fetch_image", { url });

export const startDownload = (p: {
  id: number;
  episodeUrl: string;
  playerName: string;
  outPath: string;
}) => invoke<void>("start_download", p);

export const onProgress = (cb: (e: ProgressEvent) => void): Promise<UnlistenFn> =>
  listen<ProgressEvent>("download://progress", (e) => cb(e.payload));

export const onFinished = (cb: (e: FinishedEvent) => void): Promise<UnlistenFn> =>
  listen<FinishedEvent>("download://finished", (e) => cb(e.payload));

export async function defaultOutPath(filename: string): Promise<string> {
  const dir = await downloadDir();
  return join(dir, filename);
}
