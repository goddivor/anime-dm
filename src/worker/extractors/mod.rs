mod mailru;
mod streamtape;
mod vidmoly;

use anyhow::bail;

use crate::model::VideoSource;

pub fn is_http_extractable(iframe_url: &str) -> bool {
    vidmoly::matches(iframe_url) || streamtape::matches(iframe_url) || mailru::matches(iframe_url)
}

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

pub(crate) fn host_of(url: &str) -> String {
    url.split('/').nth(2).unwrap_or(url).to_lowercase()
}
