#!/usr/bin/env python3
"""Builds the website of Anime Download Manager into site/_site.

Each page of src/ starts with a JSON comment (<!--meta {...} -->) that names its
title, its menu entry and its banner; the page body is wrapped in the shared
header and footer. The version comes from CMakeLists.txt, so the download links
follow each release. Standard library only.
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
    "0.02": {"date": "4 octobre 2026", "iso": "2026-10-04", "size": "14,5 Mo"},
    "0.01": {"date": "25 septembre 2026", "iso": "2026-09-25", "size": "14,6 Mo"},
}

NAV = [
    ("home", "Accueil", "index.html"),
    ("news", "Nouveautés", "nouveautes.html"),
    ("features", "Fonctionnalités", "fonctionnalites.html"),
    ("sources", "Sources", "sources.html"),
    ("contact", "Contact", "contact.html"),
    ("download", "Télécharger", "telecharger.html"),
    ("support", "Support", "support/index.html"),
]

SUPPORT_MENU = [
    ("Questions fréquentes", "support/faq.html"),
    ("Utiliser ADM", "support/utiliser-adm.html"),
    ("Intégration au navigateur", "support/extension.html"),
    ("Planificateur", "support/planificateur.html"),
    ("Segmentation dynamique", "support/segmentation.html"),
    ("Barre d'outils", "support/barre-outils.html"),
]


TOOLS = [("add", "Ajout URL"), ("resume", "Reprendre"), ("stop", "Arrêter"), ("stop-all", "Tout arrêter"),
         ("remove", "Supprimer"), ("remove-all", "Tout supprimer"), ("options", "Options"),
         ("schedule", "Planifier"), ("addons", "Addons"), ("search", "Rechercher")]


def version():
    text = (SITE.parent / "CMakeLists.txt").read_text(encoding="utf-8")
    return re.search(r'set\(ADM_VERSION "([0-9.]+)"\)', text).group(1)


def webp_size(path):
    data = path.read_bytes()[:40]
    chunk = data[12:16]
    if chunk == b"VP8X":
        w = 1 + int.from_bytes(data[24:27], "little")
        h = 1 + int.from_bytes(data[27:30], "little")
        return w, h
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
    data = path.read_bytes()[:24]
    return struct.unpack(">II", data[16:24])


def img_tag(root, name, alt, cls="", scale=1.0, eager=False):
    path = next((SITE / "assets/img").glob(name + ".*"))
    w, h = image_size(path)
    w, h = round(w * scale), round(h * scale)
    attrs = f' class="{cls}"' if cls else ""
    load = "" if eager else ' loading="lazy" decoding="async"'
    return (f'<img src="{root}assets/img/{path.name}" width="{w}" height="{h}" '
            f'alt="{html.escape(alt)}"{attrs}{load}>')


def expand(body, root, ctx):
    """Replaces the shortcuts of the page sources."""
    def img(m):
        name, cls, alt = m.group(1), m.group(2) or "", m.group(3)
        scale = 0.5 if "half" in cls.split() else 1.0
        return img_tag(root, name, alt, cls, scale)

    def browser(m):
        name, url, alt = m.group(1), m.group(2), m.group(3)
        return ('<figure class="browser"><div class="browser-bar" aria-hidden="true">'
                '<span class="dots"><i></i><i></i><i></i></span>'
                f'<span class="address">{html.escape(url)}</span></div>'
                f'{img_tag(root, name, alt)}</figure>')

    def sequence(m):
        name, count, url, alt = m.group(1), int(m.group(2)), m.group(3), m.group(4)
        frames = "".join(img_tag(root, f"{name}-{i}", alt if i == 0 else "") for i in range(count))
        return ('<figure class="browser"><div class="browser-bar" aria-hidden="true">'
                '<span class="dots"><i></i><i></i><i></i></span>'
                f'<span class="address">{html.escape(url)}</span></div>'
                f'<div class="sequence" data-holds="{m.group(5)}">{frames}</div></figure>')

    def partial(m):
        return (SRC / "_partials" / (m.group(1) + ".html")).read_text(encoding="utf-8")

    def toolbar(m):
        pack, tone = m.group(1), m.group(2)
        tools = "".join(f'<span class="tool" data-sprite="{pack}/{key}"><span class="sprite"></span>{label}</span>'
                        for key, label in TOOLS)
        return f'<div class="toolbar-demo {tone}" role="group" aria-label="Barre d\'outils {pack}, thème {tone}, survolez les boutons">{tools}</div>'

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


def header(root, active):
    items = []
    for key, label, href in NAV:
        cls = ' class="active" aria-current="page"' if key == active else ""
        items.append(f'<li><a href="{root}{href}"{cls}>{label}</a></li>')
    support = "".join(f'<li><a href="{root}{h}">{l}</a></li>' for l, h in SUPPORT_MENU)
    return f"""<header class="site-header">
  <div class="header-inner">
    <a class="brand" href="{root}index.html">
      <img src="{root}assets/img/logo-64.png" width="44" height="44" alt="">
      <span>Anime Download Manager</span>
    </a>
    <button class="menu-toggle" aria-expanded="false" aria-controls="site-nav" aria-label="Menu">
      <span></span><span></span><span></span>
    </button>
    <nav id="site-nav" class="site-nav" aria-label="Navigation principale">
      <ul class="nav-links">{''.join(items)}</ul>
      <ul class="support-menu" aria-label="Support">{support}</ul>
      <div class="nav-actions">
        <a class="btn btn-solid btn-small" href="{{{{download_url}}}}">Télécharger</a>
        <a class="btn btn-outline btn-small" href="{REPO_URL}">GitHub</a>
      </div>
    </nav>
  </div>
</header>"""


def hero(root, meta):
    h = meta.get("hero")
    if not h:
        return ""
    variant = h.get("variant", "default")
    parts = [f'<section class="hero hero-{variant}"><div class="hero-inner">']
    if h.get("badge"):
        parts.append(f'<p class="hero-badge">{h["badge"]}</p>')
    back = f'<a class="hero-back" href="{root}support/index.html" aria-label="Retour au support">←</a> ' if h.get("back") else ""
    parts.append(f'<h1>{back}{h["title"]}</h1>')
    if h.get("subtitle"):
        parts.append(f'<p class="hero-sub">{h["subtitle"]}</p>')
    if h.get("search"):
        parts.append(f"""<form class="hero-search" action="{root}support/faq.html" role="search">
  <input type="search" name="q" placeholder="Que cherchez-vous ?" aria-label="Rechercher dans l'aide">
  <button class="btn btn-solid" type="submit">Rechercher</button>
</form>""")
    if h.get("cta"):
        label, href = h["cta"]
        style = h.get("cta_style", "solid")
        parts.append(f'<a class="btn btn-{style} btn-large" href="{href}">{label}</a>')
    if h.get("note"):
        parts.append(f'<p class="hero-note">{h["note"]}</p>')
    parts.append("</div>")
    if h.get("art"):
        parts.append(f'<div class="hero-art" aria-hidden="true">{h["art"]}</div>')
    parts.append("</section>")
    return "\n".join(parts)


def footer(root):
    return f"""<footer class="site-footer">
  <div class="footer-inner">
    <a class="footer-brand" href="{root}index.html"><img src="{root}assets/img/logo-64.png" width="32" height="32" alt=""> ADM</a>
    <p>© 2026 goddivor et les contributeurs. Gratuit et ouvert.</p>
    <ul>
      <li><a href="{root}confidentialite.html">Confidentialité</a></li>
      <li><a href="{root}avertissement.html">Avertissement</a></li>
      <li><a href="{REPO_URL}">Code source</a></li>
    </ul>
  </div>
</footer>"""


def page(rel, raw, ctx_base):
    m = re.match(r"\s*<!--meta\s+(\{.*?\})\s*-->", raw, re.S)
    meta = json.loads(m.group(1))
    body = raw[m.end():]
    depth = rel.count("/")
    root = "/anime-dm/" if rel == "404.html" else "../" * depth
    ctx = dict(ctx_base, root=root)
    title = meta["title"]
    full_title = title if meta.get("bare_title") else f"{title} | Anime Download Manager"
    desc = meta.get("description", "")
    hero_html = expand(hero(root, meta), root, ctx)
    content = expand(body, root, ctx)
    canonical = BASE_URL + ("" if rel == "index.html" else rel)
    doc = f"""<!doctype html>
<html lang="fr">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{html.escape(full_title)}</title>
<meta name="description" content="{html.escape(desc)}">
<link rel="canonical" href="{canonical}">
<meta property="og:type" content="website">
<meta property="og:title" content="{html.escape(full_title)}">
<meta property="og:description" content="{html.escape(desc)}">
<meta property="og:image" content="{BASE_URL}assets/img/main-dark.webp">
<meta property="og:locale" content="fr_FR">
<meta name="theme-color" content="#0b1a3c">
<link rel="icon" type="image/png" sizes="32x32" href="{root}assets/img/favicon-32.png">
<link rel="apple-touch-icon" href="{root}assets/img/apple-touch-icon.png">
<link rel="stylesheet" href="{root}assets/css/site.css">
</head>
<body class="page-{meta.get('nav', 'none')}">
<a class="skip-link" href="#contenu">Aller au contenu</a>
{expand(header(root, meta.get('nav')), root, ctx)}
<main id="contenu">
{hero_html}
{content}
</main>
{footer(root)}
<script src="{root}assets/js/site.js" defer></script>
</body>
</html>
"""
    return french_spaces(doc)


def main():
    v = version()
    rel_info = RELEASES.get(v, {"date": "", "iso": "", "size": ""})
    ctx = {
        "version": v,
        "download_url": f"{REPO_URL}/releases/download/win-v{v}/AnimeDownloadManager-{v}-setup.exe",
        "release_url": f"{REPO_URL}/releases/tag/win-v{v}",
        "releases_url": f"{REPO_URL}/releases",
        "repo_url": REPO_URL,
        "issues_url": f"{REPO_URL}/issues",
        "release_date": rel_info["date"],
        "release_size": rel_info["size"],
    }
    if OUT.exists():
        shutil.rmtree(OUT)
    shutil.copytree(SITE / "assets", OUT / "assets")
    count = 0
    for src in sorted(SRC.rglob("*.html")):
        rel = src.relative_to(SRC).as_posix()
        if rel.startswith("_"):
            continue
        target = OUT / rel
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(page(rel, src.read_text(encoding="utf-8"), ctx), encoding="utf-8")
        count += 1
    (OUT / ".nojekyll").write_text("")
    print(f"{count} pages, version {v} -> {OUT}")


if __name__ == "__main__":
    main()
