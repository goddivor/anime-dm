import { useEffect, useRef, useState } from "react";
import type { Lang, T } from "../i18n";

type Item = { key: string; label: string; onClick?: () => void; sep?: boolean; check?: boolean };

export type MenuActions = {
  onAdd: () => void;
  onRemoveSelected: () => void;
  onRemoveCompleted: () => void;
  toggleSidebar: () => void;
  sidebarOn: boolean;
  toggleSearch: () => void;
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

  const menus: { id: string; title: string; items: Item[] }[] = [
    {
      id: "tasks",
      title: t("menu.tasks.title"),
      items: [
        { key: "add", label: t("menu.tasks.add"), onClick: a.onAdd },
        { key: "manual", label: t("menu.tasks.manual"), onClick: () => a.soon(t("menu.tasks.manual")) },
        { key: "s1", label: "", sep: true },
        { key: "quit", label: t("menu.tasks.quit"), onClick: () => a.soon(t("menu.tasks.quit")) },
      ],
    },
    {
      id: "file",
      title: t("menu.file.title"),
      items: [
        { key: "start", label: t("menu.file.start"), onClick: () => a.soon(t("menu.file.start")) },
        { key: "stop", label: t("menu.file.stop"), onClick: () => a.soon(t("menu.file.stop")) },
        { key: "s1", label: "", sep: true },
        { key: "remove", label: t("menu.file.remove"), onClick: a.onRemoveSelected },
      ],
    },
    {
      id: "download",
      title: t("menu.download.title"),
      items: [
        { key: "pause", label: t("menu.download.pause_all"), onClick: () => a.soon(t("menu.download.pause_all")) },
        { key: "rmdone", label: t("menu.download.remove_completed"), onClick: a.onRemoveCompleted },
        { key: "search", label: t("menu.download.search"), onClick: a.toggleSearch },
      ],
    },
    {
      id: "view",
      title: t("menu.view.title"),
      items: [
        { key: "cats", label: t("menu.view.categories"), onClick: a.toggleSidebar, check: a.sidebarOn },
        { key: "s1", label: "", sep: true },
        { key: "fr", label: "Français", onClick: () => a.setLang("fr"), check: a.lang === "fr" },
        { key: "en", label: "English", onClick: () => a.setLang("en"), check: a.lang === "en" },
      ],
    },
    {
      id: "help",
      title: t("menu.help.title"),
      items: [
        { key: "help", label: t("menu.help.title"), onClick: a.onHelp },
        { key: "about", label: t("menu.help.about"), onClick: a.onAbout },
      ],
    },
  ];

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
          {open === m.id && (
            <div className="dropdown">
              {m.items.map((it) =>
                it.sep ? (
                  <div key={it.key} className="menu-sep" />
                ) : (
                  <button
                    key={it.key}
                    className="menu-item"
                    onClick={() => {
                      setOpen(null);
                      it.onClick?.();
                    }}
                  >
                    <span className="check">{it.check ? "✓" : ""}</span>
                    {it.label}
                  </button>
                ),
              )}
            </div>
          )}
        </div>
      ))}
    </div>
  );
}
