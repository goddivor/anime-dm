//! Backend navigateur headless : pour les hébergeurs qui génèrent leur source en JavaScript
//! (VOE, mail.ru/FHD1, …), on charge l'embed dans un vrai Chrome, on déclenche la lecture et
//! on **intercepte la requête réseau** du manifeste vidéo (`.m3u8` / `.mpd` / `.mp4`).
//!
//! Leçons du prototypage (cf. historique) :
//!   - NE PAS forcer `Referer: voir-anime.to` → provoque `ERR_BLOCKED_BY_CLIENT`. On laisse
//!     le navigateur naviguer naturellement vers l'iframe.
//!   - Le Referer à rejouer dans ffmpeg est celui *de la requête média capturée*
//!     (ex. `jessicayeahcatch.com` pour VOE, `my.mail.ru` pour FHD1), pas celui de voir-anime.

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use anyhow::{anyhow, Context};
use chromiumoxide::cdp::browser_protocol::network::{EnableParams, EventRequestWillBeSent};
use chromiumoxide::cdp::browser_protocol::page::AddScriptToEvaluateOnNewDocumentParams;
use chromiumoxide::layout::Point;
use chromiumoxide::{Browser, BrowserConfig, Page};
use futures::StreamExt;

use crate::model::VideoSource;
use crate::net::UA;

/// Anti-détection : masque les marqueurs d'automatisation (certains hébergeurs comme mail.ru
/// refusent de charger leur lecteur s'ils détectent un navigateur piloté). Exécuté avant
/// tout script de la page.
const STEALTH_JS: &str = r#"
    Object.defineProperty(navigator, 'webdriver', { get: () => undefined });
    Object.defineProperty(navigator, 'languages', { get: () => ['fr-FR','fr','en-US','en'] });
    Object.defineProperty(navigator, 'plugins', { get: () => [1,2,3,4,5] });
    window.chrome = window.chrome || { runtime: {} };
    const _q = window.navigator.permissions && window.navigator.permissions.query;
    if (_q) window.navigator.permissions.query = (p) =>
        p && p.name === 'notifications'
            ? Promise.resolve({ state: Notification.permission })
            : _q(p);
"#;

/// Script injecté pour forcer la lecture (les hébergeurs ne chargent le flux qu'au play).
const PLAY_JS: &str = r#"
    document.querySelectorAll('video').forEach(v => { try { v.muted = true; v.play(); } catch (e) {} });
    ['.play','.jw-icon-display','.vjs-big-play-button','button','.plyr__control--overlaid','#player','.play-button']
        .forEach(s => { const b = document.querySelector(s); if (b) { try { b.click(); } catch (e) {} } });
"#;

/// Chemins de binaires Chrome essayés dans l'ordre.
const CHROME_CANDIDATES: &[&str] = &[
    "/usr/bin/google-chrome",
    "/usr/bin/google-chrome-stable",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
];

/// Navigateur headless partagé, lancé une seule fois et réutilisé (un onglet par capture).
pub struct Headless {
    browser: Browser,
    _handler: tokio::task::JoinHandle<()>,
}

impl Headless {
    /// Lance Chrome en headless. Coûteux : à appeler une fois puis réutiliser.
    pub async fn launch() -> anyhow::Result<Self> {
        let chrome = CHROME_CANDIDATES
            .iter()
            .find(|p| std::path::Path::new(p).exists())
            .ok_or_else(|| anyhow!("aucun binaire Chrome trouvé ({CHROME_CANDIDATES:?})"))?;

        let config = BrowserConfig::builder()
            .chrome_executable(chrome)
            .new_headless_mode()
            .no_sandbox()
            .window_size(1280, 720)
            // UA réel : évite le marqueur « HeadlessChrome » détectable par les hébergeurs.
            .arg(format!("--user-agent={UA}"))
            .arg("--mute-audio")
            .arg("--autoplay-policy=no-user-gesture-required")
            .arg("--disable-blink-features=AutomationControlled")
            .arg("--disable-dev-shm-usage")
            .arg("--no-first-run")
            .build()
            .map_err(|e| anyhow!("config Chrome : {e}"))?;

        let (browser, mut handler) = Browser::launch(config)
            .await
            .context("lancement de Chrome headless")?;

        // Le Handler doit être pollé en continu pour faire vivre la connexion CDP.
        let handle = tokio::spawn(async move { while handler.next().await.is_some() {} });

        Ok(Self {
            browser,
            _handler: handle,
        })
    }

    /// Charge l'embed, déclenche la lecture et renvoie la première source média interceptée.
    pub async fn capture(&self, embed_url: &str, timeout: Duration) -> anyhow::Result<VideoSource> {
        let page = self
            .browser
            .new_page("about:blank")
            .await
            .context("ouverture d'un onglet")?;

        // Activer le domaine Network AVANT navigation pour capturer toutes les requêtes.
        page.execute(EnableParams::default())
            .await
            .context("Network.enable")?;

        // Injecter l'anti-détection avant le chargement des scripts de la page.
        let _ = page
            .execute(AddScriptToEvaluateOnNewDocumentParams::new(STEALTH_JS))
            .await;

        let mut events = page
            .event_listener::<EventRequestWillBeSent>()
            .await
            .context("écoute des requêtes réseau")?;

        // (url, referer) des requêtes média repérées.
        let found: Arc<Mutex<Vec<(String, String)>>> = Arc::new(Mutex::new(Vec::new()));
        let sink = found.clone();
        let collector = tokio::spawn(async move {
            while let Some(ev) = events.next().await {
                let url = ev.request.url.clone();
                if is_media(&url) {
                    let referer = header_value(&ev.request.headers, "referer").unwrap_or_default();
                    sink.lock().unwrap().push((url, referer));
                }
            }
        });

        page.goto(embed_url).await.context("navigation vers l'embed")?;

        // Laisse le lecteur s'initialiser, puis déclenche la lecture (vrai clic + play JS).
        tokio::time::sleep(Duration::from_millis(2800)).await;
        nudge_play(&page).await;

        let deadline = Instant::now() + timeout;
        let picked = loop {
            if let Some(best) = pick_best(&found.lock().unwrap()) {
                break Some(best);
            }
            if Instant::now() >= deadline {
                break None;
            }
            tokio::time::sleep(Duration::from_millis(700)).await;
            nudge_play(&page).await;
        };

        collector.abort();
        let _ = page.close().await;

        let (url, referer) = picked.ok_or_else(|| {
            anyhow!("aucun flux vidéo intercepté en {}s (lecteur protégé ?)", timeout.as_secs())
        })?;

        let origin = referer
            .split('/')
            .take(3)
            .collect::<Vec<_>>()
            .join("/");

        Ok(VideoSource {
            url,
            quality: "headless".to_string(),
            referer: (!referer.is_empty()).then_some(referer),
            origin: (!origin.is_empty()).then_some(origin),
            cookie: None,
        })
    }
}

/// Déclenche la lecture : vrais clics CDP (geste utilisateur) au centre + boutons via JS.
/// Beaucoup de lecteurs (mail.ru, « Byse »…) n'attachent leur source qu'au premier clic réel.
async fn nudge_play(page: &Page) {
    for (x, y) in [(640.0, 360.0), (640.0, 380.0), (400.0, 300.0)] {
        let _ = page.click(Point::new(x, y)).await;
    }
    let _ = page.evaluate(PLAY_JS).await;
}

/// Une URL qui ressemble à un manifeste/fichier vidéo exploitable (et pas à un segment ou une pub).
fn is_media(url: &str) -> bool {
    let u = url.to_lowercase();
    // Exclut les segments (téléchargés par ffmpeg lui-même) et les pubs.
    if u.contains(".m4s")
        || u.contains("vinit.mp4")
        || u.contains("ainit.mp4")
        || u.contains("mradx")
        || u.contains(".gif")
    {
        return false;
    }
    u.contains(".m3u8") || u.contains(".mpd") || u.contains(".mp4")
}

/// Choisit la meilleure source parmi les requêtes captées : on préfère un manifeste maître.
fn pick_best(found: &[(String, String)]) -> Option<(String, String)> {
    let score = |u: &str| -> i32 {
        let u = u.to_lowercase();
        if u.contains("master.m3u8") {
            100
        } else if u.contains("stream.mpd") || u.contains(".mpd") {
            90
        } else if u.contains(".m3u8") {
            80
        } else if u.contains(".mp4") {
            50
        } else {
            0
        }
    };
    found
        .iter()
        .max_by_key(|(u, _)| score(u))
        .filter(|(u, _)| score(u) > 0)
        .cloned()
}

/// Lit un en-tête (insensible à la casse) depuis l'objet `Headers` (newtype JSON) du CDP.
fn header_value<S: serde::Serialize>(headers: &S, name: &str) -> Option<String> {
    let value = serde_json::to_value(headers).ok()?;
    let obj = value.as_object()?;
    obj.iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(name))
        .and_then(|(_, v)| v.as_str().map(|s| s.to_string()))
}
