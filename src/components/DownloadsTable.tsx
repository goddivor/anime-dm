import { useMemo, useState } from "react";
import {
  type ColumnDef,
  type SortingState,
  flexRender,
  getCoreRowModel,
  getSortedRowModel,
  useReactTable,
} from "@tanstack/react-table";
import { CheckCircle2, Clock, Download, Search, XCircle } from "lucide-react";
import type { DownloadRow, DownloadStatus } from "../types";
import type { T } from "../i18n";
import { fmtDate, humanDuration, humanSize, dirOf, hostOf } from "../format";

function StatusIcon({ s }: { s: DownloadStatus }) {
  const cls = "status-ic";
  switch (s) {
    case "queued":
      return <Clock size={14} className={cls} />;
    case "resolving":
      return <Search size={14} className={cls} />;
    case "downloading":
      return <Download size={14} className={cls} />;
    case "completed":
      return <CheckCircle2 size={14} className={`${cls} ok`} />;
    case "failed":
      return <XCircle size={14} className={`${cls} err`} />;
  }
}

function statusLabel(t: T, s: DownloadStatus): string {
  return t(`status.${s}`);
}

function ProgressCell({ d, t }: { d: DownloadRow; t: T }) {
  if (d.status === "downloading") {
    if (d.progress < 0) return <span className="muted">{t("status.starting")}</span>;
    const pct = Math.round(d.progress * 100);
    return (
      <div className="progress">
        <div className="progress-bar" style={{ width: `${pct}%` }} />
        <span className="progress-txt">{pct}%</span>
      </div>
    );
  }
  return <span className={d.status === "failed" ? "err" : ""}>{statusLabel(t, d.status)}</span>;
}

export default function DownloadsTable({
  rows,
  selectedId,
  onSelect,
  t,
}: {
  rows: DownloadRow[];
  selectedId: number | null;
  onSelect: (id: number) => void;
  t: T;
}) {
  const [sorting, setSorting] = useState<SortingState>([]);

  const columns = useMemo<ColumnDef<DownloadRow>[]>(
    () => [
      {
        id: "file",
        header: t("table.file"),
        accessorKey: "filename",
        size: 260,
        minSize: 140,
        cell: ({ row }) => (
          <span className="file-cell" title={row.original.error ?? row.original.filename}>
            <StatusIcon s={row.original.status} />
            {row.original.filename}
          </span>
        ),
      },
      {
        id: "size",
        header: t("table.size"),
        size: 90,
        accessorFn: (d) => d.sizeBytes ?? -1,
        cell: ({ row }) => humanSize(row.original.sizeBytes),
      },
      {
        id: "status",
        header: t("table.status"),
        size: 130,
        minSize: 90,
        accessorFn: (d) => d.progress,
        cell: ({ row }) => <ProgressCell d={row.original} t={t} />,
      },
      {
        id: "eta",
        header: t("table.time_left"),
        size: 100,
        accessorFn: (d) => d.eta ?? Infinity,
        cell: ({ row }) =>
          row.original.status === "downloading" ? humanDuration(row.original.eta) : "—",
      },
      {
        id: "speed",
        header: t("table.speed"),
        size: 120,
        accessorKey: "speed",
        cell: ({ row }) => row.original.speed || "—",
      },
      {
        id: "lastTry",
        header: t("table.last_try"),
        size: 130,
        accessorFn: (d) => d.lastTry ?? 0,
        cell: ({ row }) => fmtDate(row.original.lastTry),
      },
      {
        id: "added",
        header: t("table.date_added"),
        size: 130,
        accessorKey: "addedAt",
        cell: ({ row }) => fmtDate(row.original.addedAt),
      },
      {
        id: "location",
        header: t("table.location"),
        size: 180,
        minSize: 120,
        accessorFn: (d) => dirOf(d.outPath),
        cell: ({ row }) => <span title={row.original.outPath}>{dirOf(row.original.outPath)}</span>,
      },
      {
        id: "address",
        header: t("table.address"),
        size: 180,
        minSize: 120,
        accessorFn: (d) => hostOf(d.address),
        cell: ({ row }) =>
          row.original.address ? (
            <span title={row.original.address}>{hostOf(row.original.address)}</span>
          ) : (
            "—"
          ),
      },
      {
        id: "parent",
        header: t("table.parent_page"),
        size: 240,
        minSize: 140,
        accessorKey: "pageUrl",
        cell: ({ row }) => (
          <span title={row.original.pageUrl}>{row.original.pageUrl}</span>
        ),
      },
    ],
    [t],
  );

  const table = useReactTable({
    data: rows,
    columns,
    state: { sorting },
    onSortingChange: setSorting,
    getCoreRowModel: getCoreRowModel(),
    getSortedRowModel: getSortedRowModel(),
    columnResizeMode: "onChange",
    enableColumnResizing: true,
  });

  const total = table.getTotalSize();

  return (
    <div className="table-scroll">
      <table className="dl-table" style={{ width: total }}>
        <thead>
          {table.getHeaderGroups().map((hg) => (
            <tr key={hg.id}>
              {hg.headers.map((h) => (
                <th key={h.id} style={{ width: h.getSize() }} onClick={h.column.getToggleSortingHandler()}>
                  <span className="th-label">
                    {flexRender(h.column.columnDef.header, h.getContext())}
                    {{ asc: " ▲", desc: " ▼" }[h.column.getIsSorted() as string] ?? ""}
                  </span>
                  <div
                    className={`resizer ${h.column.getIsResizing() ? "resizing" : ""}`}
                    onMouseDown={h.getResizeHandler()}
                    onTouchStart={h.getResizeHandler()}
                    onClick={(e) => e.stopPropagation()}
                  />
                </th>
              ))}
            </tr>
          ))}
        </thead>
        <tbody>
          {table.getRowModel().rows.map((row) => (
            <tr
              key={row.id}
              className={row.original.id === selectedId ? "sel" : ""}
              onClick={() => onSelect(row.original.id)}
            >
              {row.getVisibleCells().map((cell) => (
                <td key={cell.id} style={{ width: cell.column.getSize() }}>
                  {flexRender(cell.column.columnDef.cell, cell.getContext())}
                </td>
              ))}
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
