export function humanSize(bytes?: number): string {
  if (bytes === undefined || bytes === null) return "—";
  const units = ["B", "KB", "MB", "GB"];
  let v = bytes;
  let i = 0;
  while (v >= 1024 && i < units.length - 1) {
    v /= 1024;
    i += 1;
  }
  return i === 0 ? `${bytes} B` : `${v.toFixed(2)} ${units[i]}`;
}

export function humanDuration(secs?: number): string {
  if (secs === undefined || secs === null || !isFinite(secs)) return "—";
  const s = Math.max(0, Math.floor(secs));
  if (s >= 3600) return `${Math.floor(s / 3600)} h ${Math.floor((s % 3600) / 60)} min`;
  if (s >= 60) return `${Math.floor(s / 60)} min ${s % 60} s`;
  return `${s} s`;
}

export function fmtDate(ms?: number): string {
  if (!ms) return "—";
  const d = new Date(ms);
  const p = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())} ${p(d.getHours())}:${p(d.getMinutes())}`;
}

export function hostOf(url?: string): string {
  if (!url) return "—";
  try {
    return new URL(url).host;
  } catch {
    return url;
  }
}

export function dirOf(path: string): string {
  const i = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"));
  return i > 0 ? path.slice(0, i) : path;
}

// "1-20", "1,5,8", "1-3,7,10-12" -> liste de numéros (bornée à max)
export function parseSelection(input: string, max: number): number[] {
  const out = new Set<number>();
  for (const part of input.split(",")) {
    const t = part.trim();
    if (!t) continue;
    const range = t.match(/^(\d+)\s*-\s*(\d+)$/);
    if (range) {
      let a = parseInt(range[1], 10);
      let b = parseInt(range[2], 10);
      if (a > b) [a, b] = [b, a];
      for (let n = a; n <= b; n++) if (n >= 1 && n <= max) out.add(n);
    } else if (/^\d+$/.test(t)) {
      const n = parseInt(t, 10);
      if (n >= 1 && n <= max) out.add(n);
    }
  }
  return [...out].sort((a, b) => a - b);
}
