use anyhow::anyhow;
use regex::Regex;
use reqwest::header::REFERER;

use crate::model::VideoSource;

pub fn matches(url: &str) -> bool {
    super::host_of(url).contains("streamtape")
}

pub async fn extract(http: &reqwest::Client, url: &str) -> anyhow::Result<Vec<VideoSource>> {
    let embed = if url.contains("/e/") {
        url.to_string()
    } else if let Some(id) = url.split('/').nth(4) {
        format!("https://streamtape.com/e/{id}")
    } else {
        url.to_string()
    };

    let html = http
        .get(&embed)
        .header(REFERER, "https://voir-anime.to/")
        .send()
        .await?
        .error_for_status()?
        .text()
        .await?;

    let re = Regex::new(
        r#"robotlink'\)\.innerHTML\s*=\s*'([^']*)'\s*\+\s*\(\s*'([^']*)'\s*\)((?:\.substring\(\d+\))+)"#,
    )
    .unwrap();

    let caps = re.captures(&html).ok_or_else(|| {
        anyhow!("streamtape : motif `robotlink` introuvable (page modifiée ou vidéo supprimée)")
    })?;

    let part_a = &caps[1];
    let part_b = &caps[2];
    let offset: usize = Regex::new(r"substring\((\d+)\)")
        .unwrap()
        .captures_iter(&caps[3])
        .filter_map(|c| c[1].parse::<usize>().ok())
        .sum();

    let trimmed_b = part_b.get(offset..).unwrap_or("");
    let video_url = format!("https:{part_a}{trimmed_b}");

    if !video_url.contains("get_video") {
        return Err(anyhow!(
            "streamtape : URL reconstruite invalide ({video_url})"
        ));
    }

    Ok(vec![VideoSource::with_referer(
        video_url,
        "Streamtape",
        "https://streamtape.com/",
    )])
}
