// Runs on every page. When the site is served by an installed source, a
// floating button appears; one click hands the page to the application, as
// if its address had been pasted into the add window.
(() => {
  const api = globalThis.browser ?? globalThis.chrome;
  if (window.top !== window) {
    return;
  }

  const text = (key) => api.i18n.getMessage(key);

  // The button and its toast live in a shadow root, out of reach of the
  // styles of the page.
  function mount(source) {
    const holder = document.createElement("div");
    holder.id = "adm-holder";
    const root = holder.attachShadow({ mode: "closed" });
    root.innerHTML = `
      <style>
        :host { all: initial; }
        .adm {
          position: fixed; right: 20px; bottom: 20px; z-index: 2147483646;
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
      <button class="adm" type="button" title="${source.name}">
        <img alt="" src="${api.runtime.getURL("icons/adm-32.png")}">
        <span>${text("button")}</span>
      </button>
      <div class="toast" role="status"></div>
    `;
    const button = root.querySelector(".adm");
    const toast = root.querySelector(".toast");
    let hide = 0;

    const say = (message, bad) => {
      toast.textContent = message;
      toast.classList.toggle("bad", !!bad);
      toast.classList.add("show");
      clearTimeout(hide);
      hide = setTimeout(() => toast.classList.remove("show"), 2600);
    };

    button.addEventListener("click", async () => {
      button.classList.add("busy");
      let answer;
      try {
        answer = await api.runtime.sendMessage({ kind: "add", url: location.href });
      } catch {
        answer = null;
      }
      button.classList.remove("busy");
      if (answer && answer.ok) {
        say(text("sent"));
      } else {
        say(text("notRunning"), true);
      }
    });

    (document.body ?? document.documentElement).appendChild(holder);
  }

  api.runtime.sendMessage({ kind: "source", url: location.href }).then((answer) => {
    if (answer && answer.source && !document.getElementById("adm-holder")) {
      mount(answer.source);
    }
  }).catch(() => {});
})();
