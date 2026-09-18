<div align="center">

<img src="icons/adm-128.png" alt="Anime Download Manager" width="140" />

# Anime Download Manager Extension

**Download anime in one click** — the browser side of Anime Download Manager.
On any site served by an installed source, a button hands the page to the app.

[![JavaScript](https://img.shields.io/badge/JavaScript-F7DF1E?logo=javascript&logoColor=black&style=flat)](https://developer.mozilla.org/en-US/docs/Web/JavaScript)
[![Manifest V3](https://img.shields.io/badge/Manifest-V3-4285F4?style=flat)](https://developer.chrome.com/docs/extensions/develop/migrate/what-is-mv3)
[![Chrome](https://img.shields.io/badge/Chrome-4285F4?logo=googlechrome&logoColor=white&style=flat)](https://www.google.com/chrome/)
[![Edge](https://img.shields.io/badge/Edge-0078D7?logo=microsoftedge&logoColor=white&style=flat)](https://www.microsoft.com/edge)
[![Brave](https://img.shields.io/badge/Brave-FB542B?logo=brave&logoColor=white&style=flat)](https://brave.com/)
[![Opera](https://img.shields.io/badge/Opera-FF1B2D?logo=opera&logoColor=white&style=flat)](https://www.opera.com/)
[![Firefox](https://img.shields.io/badge/Firefox-FF7139?logo=firefoxbrowser&logoColor=white&style=flat)](https://www.mozilla.org/firefox/)

</div>

## What it does

- Each source tells which addresses are anime pages and which are episode pages. On such a page, a **Download with ADM** button sits in the corner; on an episode page it reads **Download this episode with ADM** and the app preselects that one episode. One click sends the page to Anime Download Manager, which opens its add window with the address filled in and reads the page, exactly as if the link had been pasted.
- Anywhere on the site (the home page, a listing, the episode list of an anime), the same panel appears over any link that leads to an anime or an episode as the pointer passes over it: a poster on the home page, an episode number in a list. Clicking it sends that link.
- The panel is set up in the app, under Options, General, *Edit*: full (icon and caption) or mini (icon alone), shown on the page, over the links, or both.
- The toolbar icon carries an **ADM** badge on those sites; clicking it sends the current tab.
- Nothing goes over the network. The extension talks to the app through the native messaging host `adm-host.exe`, which the app registers with every browser when it starts (manifests under `%APPDATA%\anime-dm\host`, keys under `HKCU`).

One code base serves Chromium and Firefox: the manifest declares both kinds of background page and each browser ignores the other one.

## Install in developer mode

Start the app at least once first, so that the host is registered.

- **Chrome, Edge, Brave, Opera, Vivaldi**: open the extensions page (`chrome://extensions`, `edge://extensions`, `brave://extensions`), turn on developer mode, choose *Load unpacked* and pick this folder. The id is fixed by the `key` of the manifest (`kajalpjiomebkclalgjggcgjeiibkfcg`); that is the id the host allows.
- **Firefox**: open `about:debugging#/runtime/this-firefox`, choose *Load Temporary Add-on* and pick `manifest.json`. A temporary add-on goes away when Firefox closes, until the extension is signed.

## Files

- `manifest.json` — permissions `nativeMessaging`, `storage` and `tabs`; a content script on every page, which does nothing until the site is recognised.
- `background.js` — asks the host for the list of sites, their page patterns and the panel settings (`{"kind":"sources"}`), keeps them for a minute, answers the content scripts, sends the addresses (`{"kind":"add","url":…,"episode":…}`) and sets the badge.
- `content.js` — the button of the page and the panel over the links, inside a shadow root so the styles of the page cannot reach them.
- `_locales` — the captions, in English and in French.
