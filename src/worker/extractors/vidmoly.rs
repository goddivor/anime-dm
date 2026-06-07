//! Hébergeur **vidmoly** (LECTEUR myTV) — la page embed contient
//! `sources: [{ file: 'https://…/master.m3u8…' }]` (apostrophes simples).

use anyhow::anyhow;
use regex::Regex;
use reqwest::header::{ORIGIN, REFERER};
use std::collections::HashSet;

use crate::model::VideoSource;

const REFERER_URL: &str = "https://vidmoly.biz/";
const ORIGIN_URL: &str = "https://vidmoly.biz";

pub fn matches(url: &str) -> bool {
    super::host_of(url).contains("vidmoly")
}

pub async fn extract(http: &reqwest::Client, url: &str) -> anyhow::Result<Vec<VideoSource>> {
    let html = http
        .get(url)
        .header(REFERER, REFERER_URL)
        .header(ORIGIN, ORIGIN_URL)
        .send()
        .await?
        .error_for_status()?
        .text()
        .await?;

    // Accepte guillemets simples ou doubles autour de l'URL HLS.
    let re = Regex::new(r#"file\s*:\s*["']([^"']+\.m3u8[^"']*)["']"#).unwrap();
    let mut seen = HashSet::new();
    let mut out = Vec::new();
    for c in re.captures_iter(&html) {
        let u = c[1].to_string();
        if seen.insert(u.clone()) {
            out.push(VideoSource {
                origin: Some(ORIGIN_URL.to_string()),
                ..VideoSource::with_referer(u, "auto", REFERER_URL)
            });
        }
    }

    if out.is_empty() {
        return Err(anyhow!(
            "vidmoly : aucune source `.m3u8` trouvée (page embed modifiée ou lien expiré)"
        ));
    }
    Ok(out)
}
