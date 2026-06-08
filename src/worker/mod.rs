mod downloader;
pub mod extractors;
pub mod headless;
pub mod net;
pub mod scraper;

use std::path::PathBuf;
use std::sync::mpsc::Sender;
use std::sync::Arc;
use std::time::Duration;

use eframe::egui;
use tokio::runtime::Runtime;
use tokio::sync::Mutex;

use self::headless::Headless;
use crate::model::{Anime, DownloadStatus};

type HeadlessCell = Arc<Mutex<Option<Arc<Headless>>>>;

pub enum WorkerMsg {
    AnimeLoaded(Result<Anime, String>),
    Progress {
        id: u64,
        status: DownloadStatus,
        progress: Option<f32>,
        speed: Option<String>,
        total_secs: f32,
    },
    Finished {
        id: u64,
        result: Result<String, String>,
    },
}

pub struct DownloadJob {
    pub id: u64,
    pub episode_url: String,
    pub player_name: String,
    pub out_path: PathBuf,
}

pub struct Worker {
    rt: Runtime,
    http: reqwest::Client,
    tx: Sender<WorkerMsg>,
    ctx: egui::Context,
    headless: HeadlessCell,
}

impl Worker {
    pub fn new(tx: Sender<WorkerMsg>, ctx: egui::Context) -> anyhow::Result<Self> {
        let rt = tokio::runtime::Builder::new_multi_thread()
            .worker_threads(4)
            .enable_all()
            .build()?;
        let http = net::client()?;
        Ok(Self {
            rt,
            http,
            tx,
            ctx,
            headless: Arc::new(Mutex::new(None)),
        })
    }

    pub fn load_anime(&self, url: String) {
        let http = self.http.clone();
        let tx = self.tx.clone();
        let ctx = self.ctx.clone();
        self.rt.spawn(async move {
            let res = scraper::fetch_anime(&http, &url)
                .await
                .map_err(|e| e.to_string());
            let _ = tx.send(WorkerMsg::AnimeLoaded(res));
            ctx.request_repaint();
        });
    }

    pub fn start_download(&self, job: DownloadJob) {
        let http = self.http.clone();
        let tx = self.tx.clone();
        let ctx = self.ctx.clone();
        let headless = self.headless.clone();
        self.rt.spawn(async move {
            let _ = tx.send(WorkerMsg::Progress {
                id: job.id,
                status: DownloadStatus::Resolving,
                progress: None,
                speed: None,
                total_secs: 0.0,
            });
            ctx.request_repaint();

            let result = run_job(&http, &headless, &job, &tx, &ctx).await;
            let _ = tx.send(WorkerMsg::Finished { id: job.id, result });
            ctx.request_repaint();
        });
    }
}

async fn get_headless(cell: &HeadlessCell) -> Result<Arc<Headless>, String> {
    let mut guard = cell.lock().await;
    if let Some(h) = guard.as_ref() {
        return Ok(h.clone());
    }
    let h = Arc::new(Headless::launch().await.map_err(|e| e.to_string())?);
    *guard = Some(h.clone());
    Ok(h)
}

async fn run_job(
    http: &reqwest::Client,
    headless: &HeadlessCell,
    job: &DownloadJob,
    tx: &Sender<WorkerMsg>,
    ctx: &egui::Context,
) -> Result<String, String> {
    let players = scraper::fetch_players(http, &job.episode_url)
        .await
        .map_err(|e| e.to_string())?;

    let player = players
        .iter()
        .find(|p| p.name.eq_ignore_ascii_case(&job.player_name))
        .or_else(|| players.first())
        .ok_or_else(|| "aucun lecteur disponible sur cet épisode".to_string())?;

    let source = if extractors::is_http_extractable(&player.iframe_url) {
        extractors::resolve(http, &player.iframe_url)
            .await
            .map_err(|e| e.to_string())?
            .into_iter()
            .next()
            .ok_or_else(|| "aucune source vidéo extraite".to_string())?
    } else {
        let hl = get_headless(headless).await?;
        hl.capture(&player.iframe_url, Duration::from_secs(45))
            .await
            .map_err(|e| e.to_string())?
    };

    let _ = tx.send(WorkerMsg::Progress {
        id: job.id,
        status: DownloadStatus::Downloading,
        progress: None,
        speed: None,
        total_secs: 0.0,
    });
    ctx.request_repaint();

    downloader::download(
        source,
        job.out_path.clone(),
        job.id,
        tx.clone(),
        ctx.clone(),
    )
    .await?;
    Ok(job.out_path.to_string_lossy().into_owned())
}
