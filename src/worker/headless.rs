use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use anyhow::{anyhow, Context};
use chromiumoxide::cdp::browser_protocol::network::{EnableParams, EventRequestWillBeSent};
use chromiumoxide::cdp::browser_protocol::page::AddScriptToEvaluateOnNewDocumentParams;
use chromiumoxide::layout::Point;
use chromiumoxide::{Browser, BrowserConfig, Page};
use futures::StreamExt;

use super::net::UA;
use crate::model::VideoSource;

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

const PLAY_JS: &str = r#"
    document.querySelectorAll('video').forEach(v => { try { v.muted = true; v.play(); } catch (e) {} });
    ['.play','.jw-icon-display','.vjs-big-play-button','button','.plyr__control--overlaid','#player','.play-button']
        .forEach(s => { const b = document.querySelector(s); if (b) { try { b.click(); } catch (e) {} } });
"#;

const CHROME_CANDIDATES: &[&str] = &[
    "/usr/bin/google-chrome",
    "/usr/bin/google-chrome-stable",
    "/usr/bin/chromium",
    "/usr/bin/chromium-browser",
];

pub struct Headless {
    browser: Browser,
    _handler: tokio::task::JoinHandle<()>,
}

impl Headless {
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

        let handle = tokio::spawn(async move { while handler.next().await.is_some() {} });

        Ok(Self {
            browser,
            _handler: handle,
        })
    }

    pub async fn capture(&self, embed_url: &str, timeout: Duration) -> anyhow::Result<VideoSource> {
        let page = self
            .browser
            .new_page("about:blank")
            .await
            .context("ouverture d'un onglet")?;

        page.execute(EnableParams::default())
            .await
            .context("Network.enable")?;

        let _ = page
            .execute(AddScriptToEvaluateOnNewDocumentParams::new(STEALTH_JS))
            .await;

        let mut events = page
            .event_listener::<EventRequestWillBeSent>()
            .await
            .context("écoute des requêtes réseau")?;

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

        page.goto(embed_url)
            .await
            .context("navigation vers l'embed")?;

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
            anyhow!(
                "aucun flux vidéo intercepté en {}s (lecteur protégé ?)",
                timeout.as_secs()
            )
        })?;

        let origin = referer.split('/').take(3).collect::<Vec<_>>().join("/");

        Ok(VideoSource {
            url,
            quality: "headless".to_string(),
            referer: (!referer.is_empty()).then_some(referer),
            origin: (!origin.is_empty()).then_some(origin),
            cookie: None,
        })
    }
}

async fn nudge_play(page: &Page) {
    for (x, y) in [(640.0, 360.0), (640.0, 380.0), (400.0, 300.0)] {
        let _ = page.click(Point::new(x, y)).await;
    }
    let _ = page.evaluate(PLAY_JS).await;
}

fn is_media(url: &str) -> bool {
    let u = url.to_lowercase();
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

fn header_value<S: serde::Serialize>(headers: &S, name: &str) -> Option<String> {
    let value = serde_json::to_value(headers).ok()?;
    let obj = value.as_object()?;
    obj.iter()
        .find(|(k, _)| k.eq_ignore_ascii_case(name))
        .and_then(|(_, v)| v.as_str().map(|s| s.to_string()))
}
