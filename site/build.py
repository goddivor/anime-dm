#!/usr/bin/env python3
"""Builds the website of Anime Download Manager into site/_site.

French pages live in src/, English pages in src/en/ under the same file names,
so that the language switch finds the twin page. Each page starts with a JSON
comment (<!--meta {...} -->) that names its title, its menu entry and its
banner; the body is wrapped in the shared header and footer of its language.
The version comes from CMakeLists.txt, so the download links follow each
release. Standard library only.
"""
import html
import json
import re
import shutil
import struct
from pathlib import Path

SITE = Path(__file__).resolve().parent
SRC = SITE / "src"
OUT = SITE / "_site"
REPO_URL = "https://github.com/goddivor/anime-dm"
BASE_URL = "https://goddivor.github.io/anime-dm/"

RELEASES = {
    "0.02": {"iso": "2026-10-04", "bytes": 14455925},
    "0.01": {"iso": "2026-09-25", "bytes": 14593381},
}

MONTHS = {
    "fr": ["janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août",
           "septembre", "octobre", "novembre", "décembre"],
    "en": ["January", "February", "March", "April", "May", "June", "July", "August",
           "September", "October", "November", "December"],
}

GITHUB_ICON = ('<svg class="icon-github" viewBox="0 0 512 512" aria-hidden="true"><path fill="currentColor" '
               'fill-rule="evenodd" clip-rule="evenodd" d="M256 6.3C114.6 6.3 0 120.9 0 262.3c0 113.3 73.3 209 '
               '175 242.9 12.8 2.2 17.6-5.4 17.6-12.2 0-6.1-.3-26.2-.3-47.7-64.3 11.8-81-15.7-86.1-30.1-2.9-7.4-15.4'
               '-30.1-26.2-36.2-9-4.8-21.8-16.6-.3-17 20.2-.3 34.6 18.6 39.4 26.2 23 38.7 59.8 27.8 74.6 21.1 2.2-16.6 '
               '9-27.8 16.3-34.2-57-6.4-116.5-28.5-116.5-126.4 0-27.8 9.9-50.9 26.2-68.8-2.6-6.4-11.5-32.6 2.6-67.8 0 0 '
               '21.4-6.7 70.4 26.2 20.5-5.8 42.2-8.6 64-8.6s43.5 2.9 64 8.6c49-33.3 70.4-26.2 70.4-26.2 14.1 35.2 5.1 '
               '61.4 2.6 67.8 16.3 17.9 26.2 40.6 26.2 68.8 0 98.2-59.8 120-116.8 126.4 9.3 8 17.3 23.4 17.3 47.4 0 '
               '34.2-.3 61.8-.3 70.4 0 6.7 4.8 14.7 17.6 12.2C438.7 471.3 512 375.3 512 262.3c0-141.4-114.6-256-256-256"/></svg>')

SUN_ICON = ('<svg class="icon-sun" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/>'
            '<path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>')
MOON_ICON = '<svg class="icon-moon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z"/></svg>'
CHEVRON = '<svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="M6 9l6 6 6-6"/></svg>'

UI = {
    "fr": {
        "nav": [("home", "Accueil", "index.html"), ("news", "Nouveautés", "nouveautes.html"),
                ("features", "Fonctionnalités", "fonctionnalites.html"), ("sources", "Sources", "sources.html"),
                ("contact", "Contact", "contact.html"), ("download", "Télécharger", "telecharger.html"),
                ("support", "Support", "support/index.html")],
        "support": [("Questions fréquentes", "support/faq.html"), ("Utiliser ADM", "support/utiliser-adm.html"),
                    ("Intégration au navigateur", "support/extension.html"),
                    ("Planificateur", "support/planificateur.html"),
                    ("Segmentation dynamique", "support/segmentation.html"),
                    ("Barre d'outils", "support/barre-outils.html")],
        "get": "Obtenir", "download": "Télécharger", "changelog": "Changelog",
        "theme": "Changer de thème", "github": "Code source sur GitHub", "menu": "Menu",
        "nav_label": "Navigation principale", "skip": "Aller au contenu",
        "other_lang": ("EN", "English", "en"),
        "back": "Retour au support", "search_ph": "Que cherchez-vous ?", "search_label": "Rechercher dans l'aide",
        "search_btn": "Rechercher",
        "footer": "© 2026 goddivor et les contributeurs. Gratuit et ouvert.",
        "privacy": "Confidentialité", "notice": "Avertissement", "source": "Code source",
        "tools": ["Ajout URL", "Reprendre", "Arrêter", "Tout arrêter", "Supprimer", "Tout supprimer",
                  "Options", "Planifier", "Addons", "Rechercher"],
        "toolbar_label": "Barre d'outils {pack}, survolez les boutons",
        "locale": "fr_FR", "size_unit": "Mo", "decimal": ",",
    },
    "en": {
        "nav": [("home", "Home", "index.html"), ("news", "What's new", "nouveautes.html"),
                ("features", "Features", "fonctionnalites.html"), ("sources", "Sources", "sources.html"),
                ("contact", "Contact", "contact.html"), ("download", "Download", "telecharger.html"),
                ("support", "Support", "support/index.html")],
        "support": [("FAQ", "support/faq.html"), ("Using ADM", "support/utiliser-adm.html"),
                    ("Browser integration", "support/extension.html"),
                    ("Scheduler", "support/planificateur.html"),
                    ("Dynamic segmentation", "support/segmentation.html"),
                    ("Toolbar", "support/barre-outils.html")],
        "get": "Get", "download": "Download", "changelog": "Changelog",
        "theme": "Switch theme", "github": "Source code on GitHub", "menu": "Menu",
        "nav_label": "Main navigation", "skip": "Skip to content",
        "other_lang": ("FR", "Français", "fr"),
        "back": "Back to support", "search_ph": "What are you looking for?", "search_label": "Search the help",
        "search_btn": "Search",
        "footer": "© 2026 goddivor and contributors. Free and open.",
        "privacy": "Privacy", "notice": "Disclaimer", "source": "Source code",
        "tools": ["Add URL", "Resume", "Stop", "Stop all", "Delete", "Delete all",
                  "Options", "Scheduler", "Addons", "Search"],
        "toolbar_label": "{pack} toolbar, hover the buttons",
        "locale": "en_US", "size_unit": "MB", "decimal": ".",
    },
}

TOOL_KEYS = ["add", "resume", "stop", "stop-all", "remove", "remove-all", "options", "schedule", "addons", "search"]


def version():
    text = (SITE.parent / "CMakeLists.txt").read_text(encoding="utf-8")
    return re.search(r'set\(ADM_VERSION "([0-9.]+)"\)', text).group(1)


def long_date(iso, lang):
    y, m, d = (int(x) for x in iso.split("-"))
    if lang == "fr":
        return f"{'1er' if d == 1 else d} {MONTHS['fr'][m - 1]} {y}"
    return f"{MONTHS['en'][m - 1]} {d}, {y}"


def size_text(n, lang):
    ui = UI[lang]
    return f"{n / 1_000_000:.1f}".replace(".", ui["decimal"]) + " " + ui["size_unit"]


def webp_size(path):
    data = path.read_bytes()[:40]
    chunk = data[12:16]
    if chunk == b"VP8X":
        return 1 + int.from_bytes(data[24:27], "little"), 1 + int.from_bytes(data[27:30], "little")
    if chunk == b"VP8 ":
        w, h = struct.unpack("<HH", data[26:30])
        return w & 0x3FFF, h & 0x3FFF
    if chunk == b"VP8L":
        bits = int.from_bytes(data[21:25], "little")
        return (bits & 0x3FFF) + 1, ((bits >> 14) & 0x3FFF) + 1
    raise ValueError(path)


def image_size(path):
    if path.suffix == ".webp":
        return webp_size(path)
    return struct.unpack(">II", path.read_bytes()[16:24])


def img_tag(root, name, alt, cls="", scale=1.0):
    path = next((SITE / "assets/img").glob(name + ".*"))
    w, h = image_size(path)
    w, h = round(w * scale), round(h * scale)
    attrs = f' class="{cls}"' if cls else ""
    return (f'<img src="{root}assets/img/{path.name}" width="{w}" height="{h}" '
            f'alt="{html.escape(alt)}"{attrs} loading="lazy" decoding="async">')


def browser_frame(url, inner):
    return ('<figure class="browser"><div class="browser-bar" aria-hidden="true">'
            '<span class="dots"><i></i><i></i><i></i></span>'
            f'<span class="address">{html.escape(url)}</span></div>{inner}</figure>')


def expand(body, root, lang, ctx):
    """Replaces the shortcuts of the page sources."""
    ui = UI[lang]

    def img(m):
        name, cls, alt = m.group(1), m.group(2) or "", m.group(3)
        return img_tag(root, name, alt, cls, 0.5 if "half" in cls.split() else 1.0)

    def browser(m):
        return browser_frame(m.group(2), img_tag(root, m.group(1), m.group(3)))

    def sequence(m):
        name, count, url, alt = m.group(1), int(m.group(2)), m.group(3), m.group(4)
        frames = "".join(img_tag(root, f"{name}-{i}", alt if i == 0 else "") for i in range(count))
        return browser_frame(url, f'<div class="sequence" data-holds="{m.group(5)}">{frames}</div>')

    def partial(m):
        own = SRC / "_partials" / lang / (m.group(1) + ".html")
        shared = SRC / "_partials" / (m.group(1) + ".html")
        return (own if own.exists() else shared).read_text(encoding="utf-8")

    def toolbar(m):
        pack, tone = m.group(1), m.group(2)
        tools = "".join(f'<span class="tool" data-sprite="{pack}/{key}"><span class="sprite"></span>{label}</span>'
                        for key, label in zip(TOOL_KEYS, ui["tools"]))
        label = ui["toolbar_label"].format(pack=pack)
        return f'<div class="toolbar-demo {tone}" role="group" aria-label="{html.escape(label)}">{tools}</div>'

    body = re.sub(r"\{\{partial ([\w-]+)\}\}", partial, body)
    body = re.sub(r"\{\{toolbar ([\w-]+) (dark|light)\}\}", toolbar, body)
    body = re.sub(r'\{\{img ([\w-]+)(?: ([\w -]+?))? "(.*?)"\}\}', img, body)
    body = re.sub(r'\{\{sequence ([\w-]+) (\d+) "(.*?)" "(.*?)" ([\d,]+)\}\}', sequence, body)
    body = re.sub(r'\{\{browser ([\w-]+) "(.*?)" "(.*?)"\}\}', browser, body)
    for key, value in ctx.items():
        body = body.replace("{{" + key + "}}", value)
    return body


NBSP = " "
NNBSP = " "


def french_spaces(text):
    """Puts the non-breaking spaces of French typography in text outside tags."""
    out, skip = [], 0
    for part in re.split(r"(<[^>]+>)", text):
        if part.startswith("<"):
            tag = re.match(r"</?(\w+)", part)
            if tag and tag.group(1).lower() in ("script", "style", "code", "pre", "svg"):
                skip += -1 if part.startswith("</") else (0 if part.endswith("/>") else 1)
            out.append(part)
            continue
        if skip <= 0 and part.strip():
            part = re.sub(r" ([;!?])", NNBSP + r"\1", part)
            part = re.sub(r" :(\s|$)", NBSP + r":\1", part)
            part = part.replace("« ", "«" + NBSP).replace(" »", NBSP + "»")
        out.append(part)
    return "".join(out)


def header(root, home, lang, active, twin):
    ui = UI[lang]
    items = []
    for key, label, href in ui["nav"]:
        cls = ' class="active" aria-current="page"' if key == active else ""
        items.append(f'<li><a href="{home}{href}"{cls}>{label}</a></li>')
    support = "".join(f'<li><a href="{home}{h}">{l}</a></li>' for l, h in ui["support"])
    code, name, other = ui["other_lang"]
    return f"""<header class="site-header">
  <div class="header-inner">
    <a class="brand" href="{home}index.html">
      <img src="{root}assets/img/logo-64.png" width="44" height="44" alt="">
      <span>Anime Download Manager</span>
    </a>
    <button class="menu-toggle" aria-expanded="false" aria-controls="site-nav" aria-label="{ui['menu']}">
      <span></span><span></span><span></span>
    </button>
    <nav id="site-nav" class="site-nav" aria-label="{ui['nav_label']}">
      <ul class="nav-links">{''.join(items)}</ul>
      <ul class="support-menu" aria-label="Support">{support}</ul>
      <div class="nav-actions">
        <div class="dropdown">
          <button class="get-button" aria-expanded="false" aria-haspopup="true">{ui['get']} v{{{{version}}}}{CHEVRON}</button>
          <div class="dropdown-menu" role="menu">
            <a role="menuitem" href="{{{{download_url}}}}">{ui['download']}</a>
            <a role="menuitem" href="{home}changelog.html">{ui['changelog']}</a>
          </div>
        </div>
        <span class="sep" aria-hidden="true"></span>
        <a class="lang-switch" href="{twin}" hreflang="{other}" lang="{other}" title="{name}">{code}</a>
        <span class="sep" aria-hidden="true"></span>
        <button class="theme-toggle" type="button" aria-label="{ui['theme']}" title="{ui['theme']}">{SUN_ICON}{MOON_ICON}</button>
        <span class="sep" aria-hidden="true"></span>
        <a class="github-link" href="{REPO_URL}" aria-label="{ui['github']}" title="{ui['github']}">{GITHUB_ICON}</a>
      </div>
    </nav>
  </div>
</header>"""


def hero(home, lang, meta):
    h = meta.get("hero")
    if not h:
        return ""
    ui = UI[lang]
    parts = [f'<section class="hero hero-{h.get("variant", "default")}"><div class="hero-inner">']
    if h.get("badge"):
        parts.append(f'<p class="hero-badge">{h["badge"]}</p>')
    back = (f'<a class="hero-back" href="{home}support/index.html" aria-label="{ui["back"]}">←</a> '
            if h.get("back") else "")
    parts.append(f'<h1>{back}{h["title"]}</h1>')
    if h.get("subtitle"):
        parts.append(f'<p class="hero-sub">{h["subtitle"]}</p>')
    if h.get("search"):
        parts.append(f"""<form class="hero-search" action="{home}support/faq.html" role="search">
  <input type="search" name="q" placeholder="{ui['search_ph']}" aria-label="{ui['search_label']}">
  <button class="btn btn-solid" type="submit">{ui['search_btn']}</button>
</form>""")
    if h.get("cta"):
        label, href = h["cta"]
        parts.append(f'<a class="btn btn-{h.get("cta_style", "solid")} btn-large" href="{href}">{label}</a>')
    if h.get("note"):
        parts.append(f'<p class="hero-note">{h["note"]}</p>')
    parts.append("</div>")
    if h.get("art"):
        parts.append(f'<div class="hero-art" aria-hidden="true">{h["art"]}</div>')
    parts.append("</section>")
    return "\n".join(parts)


def footer(root, home, lang):
    ui = UI[lang]
    return f"""<footer class="site-footer">
  <div class="footer-inner">
    <a class="footer-brand" href="{home}index.html"><img src="{root}assets/img/logo-64.png" width="32" height="32" alt=""> ADM</a>
    <p>{ui['footer']}</p>
    <ul>
      <li><a href="{home}confidentialite.html">{ui['privacy']}</a></li>
      <li><a href="{home}avertissement.html">{ui['notice']}</a></li>
      <li><a href="{REPO_URL}">{ui['source']}</a></li>
    </ul>
  </div>
</footer>"""


THEME_BOOT = ("<script>(function(){try{var t=localStorage.getItem('adm-theme');}catch(e){}"
              "if(t!=='light'&&t!=='dark'){t=matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light';}"
              "document.documentElement.setAttribute('data-theme',t);})();</script>")


def page(rel, lang, raw, ctx_base):
    m = re.match(r"\s*<!--meta\s+(\{.*?\})\s*-->", raw, re.S)
    meta = json.loads(m.group(1))
    body = raw[m.end():]
    ui = UI[lang]
    out_rel = rel if lang == "fr" else "en/" + rel
    if rel == "404.html":
        root = "/anime-dm/"
    else:
        root = "../" * out_rel.count("/")
    home = root + ("en/" if lang == "en" else "")
    other = ui["other_lang"][2]
    twin_rel = rel if other == "fr" else "en/" + rel
    twin_exists = (SRC / rel).exists() if other == "fr" else (SRC / "en" / rel).exists()
    twin = root + (twin_rel if twin_exists else ("en/index.html" if other == "en" else "index.html"))
    ctx = dict(ctx_base[lang], root=root, home=home)
    title = meta["title"]
    full_title = title if meta.get("bare_title") else f"{title} | Anime Download Manager"
    desc = meta.get("description", "")
    canonical = BASE_URL + ("" if out_rel == "index.html" else out_rel)
    fr_url = BASE_URL + ("" if rel == "index.html" else rel)
    en_url = BASE_URL + "en/" + ("" if rel == "index.html" else rel)
    doc = f"""<!doctype html>
<html lang="{lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(full_title)}</title>
<meta name="description" content="{html.escape(desc)}">
<link rel="canonical" href="{canonical}">
<link rel="alternate" hreflang="fr" href="{fr_url}">
<link rel="alternate" hreflang="en" href="{en_url}">
<link rel="alternate" hreflang="x-default" href="{fr_url}">
<meta property="og:type" content="website">
<meta property="og:title" content="{html.escape(full_title)}">
<meta property="og:description" content="{html.escape(desc)}">
<meta property="og:image" content="{BASE_URL}assets/img/main-dark.webp">
<meta property="og:locale" content="{ui['locale']}">
<meta name="color-scheme" content="light dark">
{THEME_BOOT}
<link rel="icon" type="image/png" sizes="32x32" href="{root}assets/img/favicon-32.png">
<link rel="apple-touch-icon" href="{root}assets/img/apple-touch-icon.png">
<link rel="stylesheet" href="{root}assets/css/site.css">
</head>
<body class="page-{meta.get('nav', 'none')}">
<a class="skip-link" href="#contenu">{ui['skip']}</a>
{expand(header(root, home, lang, meta.get('nav'), twin), root, lang, ctx)}
<main id="contenu">
{expand(hero(home, lang, meta), root, lang, ctx)}
{expand(body, root, lang, ctx)}
</main>
{footer(root, home, lang)}
<script src="{root}assets/js/site.js" defer></script>
</body>
</html>
"""
    return french_spaces(doc) if lang == "fr" else doc


def main():
    v = version()
    info = RELEASES.get(v, {"iso": "", "bytes": 0})
    ctx = {}
    for lang in UI:
        ctx[lang] = {
            "version": v,
            "download_url": f"{REPO_URL}/releases/download/win-v{v}/AnimeDownloadManager-{v}-setup.exe",
            "release_url": f"{REPO_URL}/releases/tag/win-v{v}",
            "releases_url": f"{REPO_URL}/releases",
            "repo_url": REPO_URL,
            "issues_url": f"{REPO_URL}/issues",
            "release_date": long_date(info["iso"], lang) if info["iso"] else "",
            "release_size": size_text(info["bytes"], lang) if info["bytes"] else "",
        }
    if OUT.exists():
        shutil.rmtree(OUT)
    shutil.copytree(SITE / "assets", OUT / "assets")
    count = 0
    for src in sorted(SRC.rglob("*.html")):
        rel = src.relative_to(SRC).as_posix()
        if rel.startswith("_") or "/_" in rel:
            continue
        lang = "en" if rel.startswith("en/") else "fr"
        page_rel = rel[3:] if lang == "en" else rel
        target = OUT / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(page(page_rel, lang, src.read_text(encoding="utf-8"), ctx), encoding="utf-8")
        count += 1
    (OUT / ".nojekyll").write_text("")
    print(f"{count} pages, version {v} -> {OUT}")


if __name__ == "__main__":
    main()
