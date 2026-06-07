//! Types du domaine métier : ce que l'on scrape et ce que l'on télécharge.

/// Un animé avec sa liste d'épisodes, tel qu'extrait d'une page `/anime/{slug}/`.
#[derive(Clone, Debug)]
pub struct Anime {
    pub title: String,
    /// Page source de l'animé (réservé : action « ouvrir la page », reprise de session…).
    #[allow(dead_code)]
    pub url: String,
    pub episodes: Vec<Episode>,
}

/// Un épisode pointant vers sa page de lecture.
#[derive(Clone, Debug)]
pub struct Episode {
    /// Numéro d'épisode (float pour gérer d'éventuels « 6.5 »).
    pub number: f32,
    pub name: String,
    pub url: String,
}

/// Un lecteur disponible sur une page d'épisode (ex. « LECTEUR myTV » -> iframe vidmoly).
#[derive(Clone, Debug)]
pub struct Player {
    pub name: String,
    pub iframe_url: String,
}

/// Une source vidéo finale, prête à être passée à ffmpeg.
#[derive(Clone, Debug)]
pub struct VideoSource {
    pub url: String,
    /// Qualité/étiquette de la source (réservé : futur sélecteur de qualité dans l'UI).
    #[allow(dead_code)]
    pub quality: String,
    /// En-têtes exigés par l'hébergeur pour autoriser la lecture du flux.
    pub referer: Option<String>,
    pub origin: Option<String>,
    /// Cookie éventuel à rejouer (ex. `video_key` pour mail.ru).
    pub cookie: Option<String>,
}

impl VideoSource {
    /// Source simple avec juste une URL et un Referer (cas le plus courant).
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

/// État d'un téléchargement dans l'UI.
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

    pub fn label(self) -> &'static str {
        match self {
            DownloadStatus::Queued => "En attente",
            DownloadStatus::Resolving => "Résolution",
            DownloadStatus::Downloading => "Téléchargement",
            DownloadStatus::Completed => "Terminé",
            DownloadStatus::Failed => "Échec",
        }
    }
}

/// Ligne de la table de téléchargements (état vivant côté UI).
#[derive(Clone, Debug)]
pub struct DownloadItem {
    pub id: u64,
    pub filename: String,
    pub status: DownloadStatus,
    /// Progression 0.0..1.0, ou négatif si encore inconnue (durée non détectée).
    pub progress: f32,
    pub speed: String,
    pub total_secs: f32,
    pub error: Option<String>,
}
