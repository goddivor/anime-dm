#[derive(Clone, Debug)]
pub struct Anime {
    pub title: String,
    #[allow(dead_code)]
    pub url: String,
    pub episodes: Vec<Episode>,
}

#[derive(Clone, Debug)]
pub struct Episode {
    pub number: f32,
    pub name: String,
    pub url: String,
}

#[derive(Clone, Debug)]
pub struct Player {
    pub name: String,
    pub iframe_url: String,
}

#[derive(Clone, Debug)]
pub struct VideoSource {
    pub url: String,
    #[allow(dead_code)]
    pub quality: String,
    pub referer: Option<String>,
    pub origin: Option<String>,
    pub cookie: Option<String>,
}

impl VideoSource {
    pub fn with_referer(url: impl Into<String>, quality: impl Into<String>, referer: &str) -> Self {
        Self {
            url: url.into(),
            quality: quality.into(),
            referer: Some(referer.to_string()),
            origin: None,
            cookie: None,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DownloadStatus {
    Queued,
    Resolving,
    Downloading,
    Completed,
    Failed,
}

impl DownloadStatus {
    pub fn icon(self) -> &'static str {
        match self {
            DownloadStatus::Queued => "⏳",
            DownloadStatus::Resolving => "🔎",
            DownloadStatus::Downloading => "⬇",
            DownloadStatus::Completed => "✔",
            DownloadStatus::Failed => "✖",
        }
    }
}

#[derive(Clone, Debug)]
pub struct DownloadItem {
    pub id: u64,
    pub filename: String,
    pub status: DownloadStatus,
    pub progress: f32,
    pub speed: String,
    pub total_secs: f32,
    pub error: Option<String>,
}
