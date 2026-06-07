//! Client HTTP partagé et constantes réseau.

use anyhow::Context;
use reqwest::header::REFERER;

/// User-Agent d'un navigateur réel : suffisant pour passer Cloudflare sur voir-anime.to
/// (constaté : pas de challenge JS/Turnstile avec un UA Chrome).
pub const UA: &str =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 \
     (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

/// Domaine actif du site (l'ancien v6.voiranime.com est hors service).
pub const BASE: &str = "https://voir-anime.to";

/// Construit le client réutilisé pour tout le scraping.
pub fn client() -> reqwest::Result<reqwest::Client> {
    reqwest::Client::builder()
        .user_agent(UA)
        .build()
}

/// GET d'une page HTML avec un Referer donné, en remontant les erreurs HTTP.
pub async fn get_html(http: &reqwest::Client, url: &str, referer: &str) -> anyhow::Result<String> {
    let resp = http
        .get(url)
        .header(REFERER, referer)
        .send()
        .await
        .with_context(|| format!("requête vers {url}"))?
        .error_for_status()
        .with_context(|| format!("statut HTTP pour {url}"))?;
    Ok(resp.text().await?)
}
