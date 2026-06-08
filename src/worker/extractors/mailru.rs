use anyhow::{anyhow, Context};
use serde::Deserialize;
use std::time::{SystemTime, UNIX_EPOCH};

use crate::model::VideoSource;

const REFERER_URL: &str = "https://my.mail.ru/";

#[derive(Deserialize)]
struct Meta {
    #[serde(default)]
    videos: Vec<Video>,
}

#[derive(Deserialize)]
struct Video {
    key: String,
    url: String,
}

pub fn matches(url: &str) -> bool {
    super::host_of(url).contains("mail.ru")
}

pub async fn extract(http: &reqwest::Client, embed_url: &str) -> anyhow::Result<Vec<VideoSource>> {
    let id = embed_url
        .split('?')
        .next()
        .unwrap_or(embed_url)
        .trim_end_matches('/')
        .rsplit('/')
        .next()
        .filter(|s| !s.is_empty())
        .ok_or_else(|| anyhow!("mail.ru : identifiant introuvable dans {embed_url}"))?;

    let ts = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    let meta_url = format!(
        "https://my.mail.ru/+/video/meta/{id}?xemail=&ajax_call=1&func_name=&mna=&mnb=&ext=1&_={ts}"
    );

    let meta: Meta = http
        .get(&meta_url)
        .header(reqwest::header::REFERER, embed_url)
        .header("X-Requested-With", "XMLHttpRequest")
        .send()
        .await?
        .error_for_status()
        .context("mail.ru : meta endpoint (vidéo supprimée ?)")?
        .json()
        .await
        .context("mail.ru : JSON meta invalide")?;

    let best = meta
        .videos
        .into_iter()
        .max_by_key(|v| quality_rank(&v.key))
        .ok_or_else(|| anyhow!("mail.ru : aucune piste vidéo dans la réponse meta"))?;

    let url = if best.url.starts_with("//") {
        format!("https:{}", best.url)
    } else {
        best.url
    };

    let cookie = url
        .split("video_key=")
        .nth(1)
        .map(|s| format!("video_key={}", s.split('&').next().unwrap_or(s)));

    Ok(vec![VideoSource {
        url,
        quality: best.key,
        referer: Some(REFERER_URL.to_string()),
        origin: None,
        cookie,
    }])
}

fn quality_rank(key: &str) -> u32 {
    key.chars()
        .take_while(|c| c.is_ascii_digit())
        .collect::<String>()
        .parse()
        .unwrap_or(0)
}
