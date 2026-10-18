// Anime Download Manager: the few behaviours of the website.
(function () {
  "use strict";

  var reduced = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  // Opens and closes the menu on narrow screens.
  function setupMenu() {
    var toggle = document.querySelector(".menu-toggle");
    var nav = document.getElementById("site-nav");
    if (!toggle || !nav) return;
    toggle.addEventListener("click", function () {
      var open = nav.classList.toggle("open");
      toggle.setAttribute("aria-expanded", open ? "true" : "false");
    });
  }

  // Plays a toolbar sprite towards its last frame on hover, and back on leave,
  // the way the application does.
  function setupSprites() {
    var packs = {};
    var tools = document.querySelectorAll("[data-sprite]");
    if (!tools.length) return;
    var root = document.querySelector('link[rel="stylesheet"]').getAttribute("href").replace("assets/css/site.css", "");

    function load(pack) {
      if (!packs[pack]) {
        packs[pack] = fetch(root + "assets/img/sprites/" + pack + ".json").then(function (r) { return r.json(); });
      }
      return packs[pack];
    }

    tools.forEach(function (tool) {
      var parts = tool.getAttribute("data-sprite").split("/");
      var pack = parts[0], name = parts[1];
      var face = tool.querySelector(".sprite");
      face.style.backgroundImage = "url(" + root + "assets/img/sprites/" + pack + "-" + name + ".png)";
      var frame = 0, timer = null;

      function show(n) { frame = n; face.style.backgroundPosition = (-36 * n) + "px 0"; }

      function run(target, meta) {
        clearTimeout(timer);
        if (frame === target) return;
        var step = target > frame ? 1 : -1;
        var next = frame + step;
        var wait = reduced ? 0 : (meta.durations[next] || 80);
        timer = setTimeout(function () { show(next); run(target, meta); }, wait);
      }

      tool.addEventListener("mouseenter", function () {
        load(pack).then(function (all) { var m = all[name]; if (!m) return; show(Math.min(frame + 1, m.frames - 1)); run(m.frames - 1, m); });
      });
      tool.addEventListener("mouseleave", function () {
        load(pack).then(function (all) { var m = all[name]; if (m) run(0, m); });
      });
    });
  }

  // Cycles the frames of a .sequence figure, like an animated picture.
  function setupSequences() {
    document.querySelectorAll(".sequence").forEach(function (seq) {
      var frames = seq.querySelectorAll("img");
      if (frames.length < 2 || reduced) return;
      var holds = (seq.getAttribute("data-holds") || "").split(",").map(Number);
      var i = 0, timer = null, visible = false;
      function tick() {
        frames[i].style.opacity = i === 0 ? "1" : "0";
        i = (i + 1) % frames.length;
        frames[i].style.opacity = "1";
        timer = setTimeout(tick, holds[i] || 900);
      }
      new IntersectionObserver(function (entries) {
        entries.forEach(function (e) {
          if (e.isIntersecting && !visible) { visible = true; timer = setTimeout(tick, holds[0] || 900); }
          if (!e.isIntersecting && visible) { visible = false; clearTimeout(timer); }
        });
      }).observe(seq);
    });
  }

  // Filters the questions of the FAQ by the words typed, and by ?q= in the address.
  function setupFaq() {
    var input = document.getElementById("faq-filter");
    if (!input) return;
    var items = document.querySelectorAll(".faq details");
    var empty = document.querySelector(".faq-empty");
    function norm(s) { return s.toLowerCase().normalize("NFD").replace(/[̀-ͯ]/g, ""); }
    function apply() {
      var words = norm(input.value).split(/\s+/).filter(Boolean);
      var shown = 0;
      items.forEach(function (d) {
        var text = norm(d.textContent);
        var match = words.every(function (w) { return text.indexOf(w) !== -1; });
        d.hidden = !match;
        if (match) shown++;
        if (words.length && match) d.open = true;
      });
      document.querySelectorAll(".faq h2").forEach(function (h) {
        var group = h.nextElementSibling;
        var any = group && group.querySelector("details:not([hidden])");
        h.hidden = !any;
      });
      if (empty) empty.style.display = shown ? "none" : "block";
    }
    var q = new URLSearchParams(location.search).get("q");
    if (q) input.value = q;
    input.addEventListener("input", apply);
    apply();
  }

  // Turns the support form into a prefilled GitHub issue.
  function setupContact() {
    var form = document.getElementById("contact-form");
    if (!form) return;
    form.addEventListener("submit", function (ev) {
      ev.preventDefault();
      var f = form.elements;
      var labels = (form.getAttribute("data-labels") || "Topic|ADM version|Windows").split("|");
      var colon = document.documentElement.lang === "fr" ? " :" : ":";
      var body = "**" + labels[0] + colon + "** " + f.topic.value +
        "\n**" + labels[1] + colon + "** " + (f.version.value || "?") +
        "\n**" + labels[2] + colon + "** " + (f.windows.value || "?") + "\n\n" + f.details.value;
      var url = form.getAttribute("action") + "?title=" + encodeURIComponent(f.title.value) +
        "&body=" + encodeURIComponent(body);
      window.open(url, "_blank", "noopener");
    });
  }

  // Switches between the light and dark themes, and remembers the choice.
  function setupTheme() {
    var button = document.querySelector(".theme-toggle");
    if (!button) return;
    button.addEventListener("click", function () {
      var next = document.documentElement.getAttribute("data-theme") === "dark" ? "light" : "dark";
      document.documentElement.setAttribute("data-theme", next);
      try { localStorage.setItem("adm-theme", next); } catch (e) { return; }
    });
  }

  // Opens the Get menu on click, and closes it on a click outside or Escape.
  function setupDropdown() {
    var drop = document.querySelector(".dropdown");
    if (!drop) return;
    var button = drop.querySelector(".get-button");
    function set(open) {
      drop.classList.toggle("open", open);
      button.setAttribute("aria-expanded", open ? "true" : "false");
    }
    button.addEventListener("click", function (ev) { ev.stopPropagation(); set(!drop.classList.contains("open")); });
    document.addEventListener("click", function (ev) { if (!drop.contains(ev.target)) set(false); });
    document.addEventListener("keydown", function (ev) { if (ev.key === "Escape") set(false); });
  }

  setupTheme();
  setupDropdown();
  setupMenu();
  setupSprites();
  setupSequences();
  setupFaq();
  setupContact();
})();
