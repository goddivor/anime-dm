// The page behind the extension: it asks the native host which sites the
// installed sources serve, keeps that list, answers the content scripts, and
// hands the addresses to the application.
//
// Chromium exposes the API as `chrome`, Firefox as `browser`; both promise
// where it matters here.
const api = globalThis.browser ?? globalThis.chrome;

const HOST = "com.animedm.host";
const REFRESH_MS = 5 * 60 * 1000;

// Sends one request to the native host and returns its answer, or null when
// the host cannot be reached (not installed, or refused by the browser).
async function askHost(request) {
  try {
    return await api.runtime.sendNativeMessage(HOST, request);
  } catch (error) {
    console.warn("ADM host unreachable:", error);
    return null;
  }
}

// The host of an address, lowercased, without its `www.`.
function hostOf(url) {
  try {
    return new URL(url).hostname.toLowerCase().replace(/^www\./, "");
  } catch {
    return "";
  }
}

// Whether a page host belongs to a site, a subdomain counting as one.
function sameSite(host, site) {
  return host === site || host.endsWith("." + site);
}

// Refreshes the list of sites from the host and stores it.
async function refreshSources() {
  const answer = await askHost({ kind: "sources" });
  if (!answer || !answer.ok) {
    return null;
  }
  const sources = (answer.sources ?? [])
    .map((s) => ({
      id: s.id,
      name: s.name,
      lang: s.lang,
      site: hostOf(s.site),
      animePattern: s.animePattern ?? "",
      episodePattern: s.episodePattern ?? "",
      animeFromEpisode: s.animeFromEpisode ?? "",
    }))
    .filter((s) => s.site);
  await api.storage.local.set({ sources, refreshedAt: Date.now() });
  return sources;
}

// The stored list, refreshed when stale.
async function sources() {
  const stored = await api.storage.local.get(["sources", "refreshedAt"]);
  if (stored.sources && Date.now() - (stored.refreshedAt ?? 0) < REFRESH_MS) {
    return stored.sources;
  }
  return (await refreshSources()) ?? stored.sources ?? [];
}

// A regular expression a source declared, or null when it declared none or
// wrote it wrong.
function patternOf(text) {
  if (!text) {
    return null;
  }
  try {
    return new RegExp(text);
  } catch {
    return null;
  }
}

// What a page is to its source: the anime page to send, the episode wanted
// on it, or nothing when the source knows the site but not this page.
//   { source, anime, episode } or null
async function pageOf(url) {
  const host = hostOf(url);
  if (!host) {
    return null;
  }
  const source = (await sources()).find((s) => sameSite(host, s.site));
  if (!source) {
    return null;
  }
  let target;
  try {
    target = new URL(url);
  } catch {
    return null;
  }
  const path = target.pathname + target.search;
  const anime = patternOf(source.animePattern);
  const episode = patternOf(source.episodePattern);
  if (episode && episode.test(path) && source.animeFromEpisode) {
    const animePath = path.replace(episode, source.animeFromEpisode);
    return { source, anime: target.origin + animePath, episode: target.origin + path };
  }
  if (!anime || anime.test(path)) {
    return { source, anime: target.origin + path, episode: "" };
  }
  return null;
}

// The source that serves a page, or null.
async function sourceFor(url) {
  const page = await pageOf(url);
  return page ? page.source : null;
}

// Marks the toolbar icon of a tab whose site is served by a source.
async function markTab(tabId, url) {
  const source = url ? await sourceFor(url) : null;
  try {
    await api.action.setBadgeText({ tabId, text: source ? "ADM" : "" });
    if (source) {
      await api.action.setBadgeBackgroundColor({ tabId, color: "#0078D7" });
      await api.action.setTitle({ tabId, title: `Anime Download Manager · ${source.name}` });
    } else {
      await api.action.setTitle({ tabId, title: "Anime Download Manager" });
    }
  } catch {
    // A tab that closed under us, or a page the browser keeps for itself.
  }
}

// Hands a page to the application through the host: the anime page, and
// the episode wanted on it when the page named one.
async function send(url) {
  const page = await pageOf(url);
  if (!page) {
    return { ok: false, error: "page" };
  }
  const request = { kind: "add", url: page.anime };
  if (page.episode) {
    request.episode = page.episode;
  }
  const answer = await askHost(request);
  if (!answer) {
    return { ok: false, error: "host" };
  }
  return { ok: !!answer.ok, error: answer.ok ? "" : answer.error ?? "" };
}

api.runtime.onInstalled.addListener(() => {
  refreshSources();
});
api.runtime.onStartup.addListener(() => {
  refreshSources();
});

api.tabs.onUpdated.addListener((tabId, change, tab) => {
  if (change.status === "complete" || change.url) {
    markTab(tabId, tab.url);
  }
});
api.tabs.onActivated.addListener(async ({ tabId }) => {
  try {
    const tab = await api.tabs.get(tabId);
    markTab(tabId, tab.url);
  } catch {
    // Gone already.
  }
});

// A click on the toolbar icon sends the page of the active tab.
api.action.onClicked.addListener(async (tab) => {
  if (!tab.url) {
    return;
  }
  const source = await sourceFor(tab.url);
  if (!source) {
    await refreshSources();
    return;
  }
  await send(tab.url);
});

// What the content scripts ask: whether their page is served, and to send it.
api.runtime.onMessage.addListener((message, sender, reply) => {
  (async () => {
    if (message.kind === "source") {
      const page = await pageOf(message.url);
      reply({ source: page ? page.source : null, episode: page ? !!page.episode : false });
    } else if (message.kind === "add") {
      reply(await send(message.url));
    } else {
      reply({ ok: false });
    }
  })();
  return true;
});
