import type { DownloadStatus, Queue } from "./types";

// A queue plan describes how new episodes are enqueued. It lets the same
// enqueue routine serve both a normal download and a paused, scheduled one
// without duplicating the logic (the OOP "strategy" the two modes share).
export type QueuePlan = {
  queue: Queue;
  status: DownloadStatus;
  // Whether the download starts right away. Scheduled items stay paused.
  autostart: boolean;
  // Whether the destination folder must be created up front (scheduled items
  // are not started, so nothing else would create it).
  ensureFolder: boolean;
};

export const DOWNLOAD_PLAN: QueuePlan = {
  queue: "main",
  status: "queued",
  autostart: true,
  ensureFolder: false,
};

export const SCHEDULE_PLAN: QueuePlan = {
  queue: "scheduler",
  status: "stopped",
  autostart: false,
  ensureFolder: true,
};

export const planFor = (schedule: boolean): QueuePlan =>
  schedule ? SCHEDULE_PLAN : DOWNLOAD_PLAN;
