use anyhow::Context;
use reqwest::header::REFERER;

pub const UA: &str = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 \
     (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

pub const BASE: &str = "https://voir-anime.to";

pub fn client() -> reqwest::Result<reqwest::Client> {
    reqwest::Client::builder().user_agent(UA).build()
}

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
