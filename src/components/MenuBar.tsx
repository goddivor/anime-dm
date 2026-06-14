import { useEffect, useRef, useState, type ReactNode } from "react";
import type { Lang, T } from "../i18n";

type Item = {
  key: string;
  label?: string;
  onClick?: () => void;
  sub?: Item[];
  sep?: boolean;
  check?: boolean;
  shortcut?: string;
  disabled?: boolean;
};

export type MenuActions = {
  onAdd: () => void;
  onResume: () => void;
  onStop: () => void;
  onStopAll: () => void;
  onRemoveSelected: () => void;
  onRemoveCompleted: () => void;
  onDeleteAll: () => void;
  canStop: boolean;
  canResume: boolean;
  canDelete: boolean;
  anyActive: boolean;
  anyRows: boolean;
  toggleSidebar: () => void;
  sidebarOn: boolean;
  toggleSearch: () => void;
  openAddons: () => void;
  setLang: (l: Lang) => void;
  lang: Lang;
  onAbout: () => void;
  onHelp: () => void;
  soon: (label: string) => void;
};

export default function MenuBar({ t, a }: { t: T; a: MenuActions }) {
  const [open, setOpen] = useState<string | null>(null);
  const ref = useRef<HTMLDivElement>(null);

  useEffect(() => {
    const close = (e: MouseEvent) => {
      if (ref.current && !ref.current.contains(e.target as Node)) setOpen(null);
    };
    document.addEventListener("mousedown", close);
    return () => document.removeEventListener("mousedown", close);
  }, []);

  const fmt = (kind: "export" | "import"): Item[] =>
    ["fmt_adm", "fmt_txt", "fmt_json", "fmt_sheet"].map((f) => {
      const key = `menu.tasks.${kind}.${f}`;
      const verb = t(`menu.tasks.${kind}_verb`);
      return { key, label: t(key), onClick: () => a.soon(`${verb} ${t(key)}`) };
    });

  const sortKeys = [
    "sort.date_added",
    "sort.name",
    "sort.size",
    "sort.status",
    "sort.time_left",
    "sort.speed",
    "sort.last_try",
    "sort.location",
    "sort.address",
    "sort.parent_page",
  ];

  const queueItems: Item[] = [
    { key: "main", label: t("menu.download.queue_main"), onClick: () => a.soon(t("menu.download.queue_main")) },
    { key: "sync", label: t("menu.download.queue_sync"), onClick: () => a.soon(t("menu.download.queue_sync")) },
  ];

  const menus: { id: string; title: string; items: Item[] }[] = [
    {
      id: "tasks",
      title: t("menu.tasks.title"),
      items: [
        { key: "add", label: t("menu.tasks.add"), onClick: a.onAdd },
        { key: "manual", label: t("menu.tasks.manual"), onClick: () => a.soon(t("menu.tasks.manual")) },
        { key: "batch", label: t("menu.tasks.batch"), shortcut: "Ctrl+Shift+V", onClick: () => a.soon(t("menu.tasks.batch")) },
        { key: "s1", sep: true },
        { key: "export", label: t("menu.tasks.export"), sub: fmt("export") },
        { key: "import", label: t("menu.tasks.import"), sub: fmt("import") },
        { key: "s2", sep: true },
        { key: "quit", label: t("menu.tasks.quit"), onClick: () => a.soon(t("menu.tasks.quit")) },
      ],
    },
    {
      id: "file",
      title: t("menu.file.title"),
      items: [
        { key: "start", label: t("menu.file.start"), onClick: a.onResume, disabled: !a.canResume },
        { key: "stop", label: t("menu.file.stop"), onClick: a.onStop, disabled: !a.canStop },
        { key: "redl", label: t("menu.file.redownload"), onClick: a.onResume, disabled: !a.canResume },
        { key: "s1", sep: true },
        { key: "remove", label: t("menu.file.remove"), onClick: a.onRemoveSelected, disabled: !a.canDelete },
      ],
    },
    {
      id: "download",
      title: t("menu.download.title"),
      items: [
        { key: "stopall", label: t("menu.download.stop_all"), onClick: a.onStopAll, disabled: !a.anyActive },
        { key: "rmdone", label: t("menu.download.remove_completed"), onClick: a.onRemoveCompleted, disabled: !a.anyRows },
        { key: "delall", label: t("menu.download.delete_all"), onClick: a.onDeleteAll, disabled: !a.anyRows },
        { key: "search", label: t("menu.download.search"), shortcut: "Ctrl+F", onClick: a.toggleSearch },
        { key: "s1", sep: true },
        { key: "sched", label: t("menu.download.schedule"), onClick: () => a.soon(t("menu.download.schedule")) },
        { key: "startq", label: t("menu.download.start_queue"), sub: queueItems },
        { key: "stopq", label: t("menu.download.stop_queue"), sub: queueItems },
        {
          key: "limiter",
          label: t("menu.download.limiter"),
          sub: [
            { key: "en", label: t("menu.download.limiter_enable"), onClick: () => a.soon(t("menu.download.limiter_enable")) },
            { key: "dis", label: t("menu.download.limiter_disable"), onClick: () => a.soon(t("menu.download.limiter_disable")) },
            { key: "set", label: t("menu.download.settings"), onClick: () => a.soon(t("menu.download.settings")) },
          ],
        },
        { key: "booster", label: t("menu.download.booster"), onClick: () => a.soon(t("menu.download.booster")) },
      ],
    },
    {
      id: "view",
      title: t("menu.view.title"),
      items: [
        { key: "addons", label: t("menu.view.addons"), onClick: a.openAddons },
        { key: "s0", sep: true },
        { key: "cats", label: t("menu.view.categories"), check: a.sidebarOn, onClick: a.toggleSidebar },
        { key: "sort", label: t("menu.view.sort"), sub: sortKeys.map((k) => ({ key: k, label: t(k), onClick: () => a.soon(t(k)) })) },
        {
          key: "toolbar",
          label: t("menu.view.toolbar"),
          sub: [
            { key: "cust", label: t("menu.view.toolbar_customize"), onClick: () => a.soon(t("menu.view.toolbar_customize")) },
            { key: "iface", label: t("menu.view.interface"), onClick: () => a.soon(t("menu.view.interface")) },
            { key: "short", label: t("menu.view.shortcuts"), onClick: () => a.soon(t("menu.view.shortcuts")) },
          ],
        },
        { key: "cols", label: t("menu.view.columns"), onClick: () => a.soon(t("menu.view.columns")) },
        {
          key: "mode",
          label: t("menu.view.mode"),
          sub: [
            { key: "dark", label: t("mode.dark"), onClick: () => a.soon(t("mode.dark")) },
            { key: "light", label: t("mode.light"), onClick: () => a.soon(t("mode.light")) },
            { key: "system", label: t("mode.system"), onClick: () => a.soon(t("mode.system")) },
          ],
        },
        {
          key: "font",
          label: t("menu.view.font"),
          sub: [
            { key: "sel", label: t("menu.view.font_select"), onClick: () => a.soon(t("menu.view.font_select")) },
            { key: "reset", label: t("menu.view.font_reset"), onClick: () => a.soon(t("menu.view.font_reset")) },
          ],
        },
        {
          key: "lang",
          label: t("menu.view.language"),
          sub: [
            { key: "en", label: "English", check: a.lang === "en", onClick: () => a.setLang("en") },
            { key: "fr", label: "Français", check: a.lang === "fr", onClick: () => a.setLang("fr") },
          ],
        },
      ],
    },
    {
      id: "help",
      title: t("menu.help.title"),
      items: [
        { key: "help", label: t("menu.help.title"), shortcut: "F1", onClick: a.onHelp },
        { key: "update", label: t("menu.help.update"), onClick: () => a.soon(t("menu.help.update")) },
        {
          key: "about",
          label: t("menu.help.about"),
          sub: [
            { key: "about", label: t("menu.help.about"), onClick: a.onAbout },
            { key: "authors", label: t("menu.help.authors"), onClick: a.onAbout },
            { key: "license", label: t("menu.help.license"), onClick: a.onAbout },
            { key: "credits", label: t("menu.help.credits"), onClick: a.onAbout },
          ],
        },
      ],
    },
  ];

  const renderItems = (items: Item[]): ReactNode =>
    items.map((it) => {
      if (it.sep) return <div key={it.key} className="menu-sep" />;
      if (it.sub)
        return (
          <div key={it.key} className="menu-item has-sub">
            <span className="check">{it.check ? "✓" : ""}</span>
            <span className="mi-label">{it.label}</span>
            <span className="arrow">›</span>
            <div className="submenu">{renderItems(it.sub)}</div>
          </div>
        );
      return (
        <button
          key={it.key}
          className="menu-item"
          disabled={it.disabled}
          onClick={() => {
            setOpen(null);
            it.onClick?.();
          }}
        >
          <span className="check">{it.check ? "✓" : ""}</span>
          <span className="mi-label">{it.label}</span>
          {it.shortcut && <span className="shortcut">{it.shortcut}</span>}
        </button>
      );
    });

  return (
    <div className="menubar" ref={ref}>
      {menus.map((m) => (
        <div key={m.id} className="menu">
          <button
            className={`menu-title ${open === m.id ? "open" : ""}`}
            onClick={() => setOpen(open === m.id ? null : m.id)}
            onMouseEnter={() => open && setOpen(m.id)}
          >
            {m.title}
          </button>
          {open === m.id && <div className="dropdown">{renderItems(m.items)}</div>}
        </div>
      ))}
    </div>
  );
}
