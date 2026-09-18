// Runs on every page. When the site is served by an installed source, two
// things appear: a button on the page itself when the page is an anime or an
// episode, and a small panel over any link that leads to one, as the
// pointer passes over it. One click hands the address to the application,
// as if it had been pasted into the add window.
(() => {
  const api = globalThis.browser ?? globalThis.chrome;
  if (window.top !== window) {
    return;
  }

  const text = (key) => api.i18n.getMessage(key);
  const HIDE_DELAY = 220;

  let site = null;
  let panel = { mode: "full", onPage: true, onLinks: true };
  let root = null;
  let toast = null;
  let hideToast = 0;

  // A regular expression a source declared, or null.
  function patternOf(source) {
    if (!source) {
      return null;
    }
    try {
      return new RegExp(source);
    } catch {
      return null;
    }
  }

  // What an address is to the source: "episode", "anime" or null. The site
  // is known to match already; the path decides. A source without patterns
  // takes every page of its site as an anime, but no link.
  function kindOf(href, pageItself) {
    let url;
    try {
      url = new URL(href, location.href);
    } catch {
      return null;
    }
    const host = url.hostname.toLowerCase().replace(/^www\./, "");
    if (host !== site.host && !host.endsWith("." + site.host)) {
      return null;
    }
    const path = url.pathname + url.search;
    if (site.episode && site.episode.test(path)) {
      return "episode";
    }
    if (site.anime && site.anime.test(path)) {
      return "anime";
    }
    if (pageItself && !site.anime && !site.episode) {
      return "anime";
    }
    return null;
  }

  // The label of a panel for a kind of address.
  function labelOf(kind) {
    return text(kind === "episode" ? "buttonEpisode" : "button");
  }

  // Hands an address to the application and tells the user how it went.
  async function send(url, control) {
    control.classList.add("busy");
    let answer;
    try {
      answer = await api.runtime.sendMessage({ kind: "add", url });
    } catch {
      answer = null;
    }
    control.classList.remove("busy");
    if (answer && answer.ok) {
      say(text("sent"));
    } else {
      say(text("notRunning"), true);
    }
  }

  function say(message, bad) {
    toast.textContent = message;
    toast.classList.toggle("bad", !!bad);
    toast.classList.add("show");
    clearTimeout(hideToast);
    hideToast = setTimeout(() => toast.classList.remove("show"), 2600);
  }

  // Builds one panel: the icon, and the label unless the mode is mini.
  function makePanel(kind, extraClass) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "adm " + extraClass + (panel.mode === "mini" ? " mini" : "");
    if (panel.mode === "mini") {
      button.title = labelOf(kind);
    }
    const icon = document.createElement("img");
    icon.alt = "";
    icon.src = api.runtime.getURL("icons/adm-32.png");
    button.appendChild(icon);
    if (panel.mode !== "mini") {
      const label = document.createElement("span");
      label.textContent = labelOf(kind);
      button.appendChild(label);
    }
    return button;
  }

  // The shadow root that holds everything, out of reach of the page styles.
  function mountRoot() {
    const holder = document.createElement("div");
    holder.id = "adm-holder";
    root = holder.attachShadow({ mode: "closed" });
    root.innerHTML = `
      <style>
        :host { all: initial; }
        .adm {
          position: fixed; z-index: 2147483646;
          display: flex; align-items: center; gap: 9px;
          padding: 10px 16px 10px 12px; border-radius: 999px;
          background: #0078D7; color: #fff; border: 0; cursor: pointer;
          font: 600 14px/1 "Segoe UI", system-ui, sans-serif;
          box-shadow: 0 6px 18px rgba(0, 0, 0, .28);
          transition: transform .12s ease, box-shadow .12s ease, opacity .2s ease;
        }
        .adm:hover { transform: translateY(-1px); box-shadow: 0 10px 24px rgba(0, 0, 0, .32); }
        .adm:active { transform: translateY(0); }
        .adm img { width: 20px; height: 20px; border-radius: 4px; }
        .adm.busy { opacity: .7; pointer-events: none; }
        .adm.mini { padding: 7px; gap: 0; }
        .adm.page { right: 20px; bottom: 20px; }
        .adm.link {
          padding: 7px 12px 7px 9px; font-size: 13px;
          opacity: 0; pointer-events: none; transform: translateY(4px);
        }
        .adm.link.mini { padding: 6px; }
        .adm.link img { width: 18px; height: 18px; }
        .adm.link.show { opacity: 1; pointer-events: auto; transform: none; }
        .toast {
          position: fixed; right: 20px; bottom: 72px; z-index: 2147483646;
          padding: 9px 14px; border-radius: 8px; background: #202020; color: #fff;
          font: 13px/1.3 "Segoe UI", system-ui, sans-serif;
          box-shadow: 0 6px 18px rgba(0, 0, 0, .28);
          opacity: 0; transform: translateY(6px); transition: opacity .2s, transform .2s;
        }
        .toast.show { opacity: 1; transform: none; }
        .toast.bad { background: #B42318; }
      </style>
      <div class="toast" role="status"></div>
    `;
    toast = root.querySelector(".toast");
    (document.body ?? document.documentElement).appendChild(holder);
  }

  // The button of the page itself, in its corner.
  function mountPageButton(kind) {
    const button = makePanel(kind, "page");
    button.addEventListener("click", () => send(location.href, button));
    root.appendChild(button);
  }

  // The panel that follows the pointer onto links: one element reused, shown
  // on the corner of the link under the pointer, hidden a moment after the
  // pointer leaves it and the panel both.
  function mountLinkPanel() {
    let button = null;
    let target = null;
    let hideTimer = 0;

    const hide = () => {
      clearTimeout(hideTimer);
      hideTimer = setTimeout(() => {
        if (button) {
          button.classList.remove("show");
        }
        target = null;
      }, HIDE_DELAY);
    };
    const keep = () => clearTimeout(hideTimer);

    const show = (link, kind) => {
      keep();
      if (target === link && button && button.classList.contains("show")) {
        return;
      }
      target = link;
      if (button) {
        button.remove();
      }
      button = makePanel(kind, "link");
      button.addEventListener("mouseenter", keep);
      button.addEventListener("mouseleave", hide);
      button.addEventListener("click", (event) => {
        event.preventDefault();
        event.stopPropagation();
        send(link.href, button);
      });
      root.appendChild(button);

      const box = cardOf(link);
      const margin = 6;
      const width = button.offsetWidth;
      const height = button.offsetHeight;
      let left = box.left + margin;
      let top = box.top + margin;
      if (box.height < height + 2 * margin) {
        top = box.bottom + 2;
      }
      left = Math.max(4, Math.min(left, window.innerWidth - width - 4));
      top = Math.max(4, Math.min(top, window.innerHeight - height - 4));
      button.style.left = left + "px";
      button.style.top = top + "px";
      requestAnimationFrame(() => button.classList.add("show"));
    };

    document.addEventListener("mouseover", (event) => {
      const link = event.target instanceof Element ? event.target.closest("a[href]") : null;
      if (!link) {
        return;
      }
      const kind = kindOf(link.href);
      if (!kind || sameAsPage(link.href)) {
        return;
      }
      show(link, kind);
    }, true);
    document.addEventListener("mouseout", (event) => {
      const link = event.target instanceof Element ? event.target.closest("a[href]") : null;
      if (link && link === target) {
        hide();
      }
    }, true);
    window.addEventListener("scroll", () => {
      if (button) {
        button.classList.remove("show");
      }
      target = null;
    }, { passive: true, capture: true });
  }

  // The box the panel sits on: the poster inside the link (an inline link
  // around an image measures only its line box), else the poster a small
  // link lies over (a play badge, a caption), else the link itself.
  function cardOf(link) {
    const box = link.getBoundingClientRect();
    let best = null;
    for (const image of link.querySelectorAll("img")) {
      const around = image.getBoundingClientRect();
      if (around.width > 0 && (!best || around.height > best.height)) {
        best = around;
      }
    }
    if (best) {
      return best;
    }
    const centre = { x: box.left + box.width / 2, y: box.top + box.height / 2 };
    let node = link.parentElement;
    for (let depth = 0; depth < 3 && node && node !== document.body; ++depth) {
      for (const image of node.querySelectorAll("img")) {
        const around = image.getBoundingClientRect();
        if (around.height > box.height && centre.x >= around.left && centre.x <= around.right &&
            centre.y >= around.top && centre.y <= around.bottom) {
          return around;
        }
      }
      node = node.parentElement;
    }
    return box;
  }

  // Whether an address is the page itself, its own button covering it.
  function sameAsPage(href) {
    const strip = (url) => url.replace(/[#?].*$/, "").replace(/\/+$/, "");
    return strip(href) === strip(location.href);
  }

  api.runtime.sendMessage({ kind: "site", url: location.href }).then((answer) => {
    if (!answer || !answer.site || document.getElementById("adm-holder")) {
      return;
    }
    site = {
      host: answer.site.host,
      anime: patternOf(answer.site.animePattern),
      episode: patternOf(answer.site.episodePattern),
    };
    panel = Object.assign(panel, answer.panel ?? {});
    mountRoot();
    const kind = kindOf(location.href, true);
    if (kind && panel.onPage) {
      mountPageButton(kind);
    }
    if (panel.onLinks && (site.anime || site.episode)) {
      mountLinkPanel();
    }
  }).catch(() => {});
})();
