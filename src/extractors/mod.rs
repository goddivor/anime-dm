//! Extracteurs : transforment l'URL d'iframe d'un hébergeur en URL(s) vidéo finale(s).
//!
//! Un fichier = un hébergeur (à la manière du projet Kotlin de référence). Chaque module
//! expose `matches(url)` et `extract(http, url)`. `resolve` se contente de dispatcher.
//!
//! ## Deux voies d'extraction
//!
//! - **HTTP direct** (ce dossier) — l'hébergeur expose sa source dans le HTML/JSON :
//!     - [`vidmoly`]    (LECTEUR myTV)  -> playlist HLS `.m3u8`
//!     - [`streamtape`] (LECTEUR Stape) -> MP4 direct (`get_video`)
//!     - [`mailru`]     (LECTEUR FHD1)  -> MP4 full-HD via l'endpoint `/+/video/meta/`
//!
//! - **Navigateur headless** (cf. `crate::headless`) — pour les hébergeurs dont la source est
//!   générée en JavaScript (VOE, et lecteurs « Byse » MOON/SB). Le worker y bascule
//!   automatiquement quand [`is_http_extractable`] est faux.

mod mailru;
mod streamtape;
mod vidmoly;

use anyhow::bail;

use crate::model::VideoSource;

/// Vrai si l'hébergeur est extractible en HTTP direct (sinon : navigateur headless).
pub fn is_http_extractable(iframe_url: &str) -> bool {
    vidmoly::matches(iframe_url) || streamtape::matches(iframe_url) || mailru::matches(iframe_url)
}

/// Extraction HTTP directe : dispatche vers l'extracteur de l'hébergeur reconnu.
pub async fn resolve(http: &reqwest::Client, iframe_url: &str) -> anyhow::Result<Vec<VideoSource>> {
    if vidmoly::matches(iframe_url) {
        vidmoly::extract(http, iframe_url).await
    } else if streamtape::matches(iframe_url) {
        streamtape::extract(http, iframe_url).await
    } else if mailru::matches(iframe_url) {
        mailru::extract(http, iframe_url).await
    } else {
        bail!("resolve() appelé sur un hôte non-HTTP — devrait passer par le headless")
    }
}

/// Hôte (domaine) d'une URL, en minuscules. Utilitaire partagé par les sous-modules.
pub(crate) fn host_of(url: &str) -> String {
    url.split('/').nth(2).unwrap_or(url).to_lowercase()
}
