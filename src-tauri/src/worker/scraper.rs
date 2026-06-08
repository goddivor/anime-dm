use anyhow::{anyhow, Context};
use regex::Regex;
use scraper::{Html, Selector};
use std::collections::BTreeMap;

use super::net::{get_html, BASE};
use crate::model::{Anime, Episode, Player};

pub async fn fetch_anime(http: &reqwest::Client, url: &str) -> anyhow::Result<Anime> {
    let html = get_html(http, url, BASE).await?;

    let doc = Html::parse_document(&html);

    let title = {
        let sel = Selector::parse(".post-title h1, .post-title h3").unwrap();
        doc.select(&sel)
            .next()
            .map(|e| e.text().collect::<String>().trim().to_string())
            .filter(|s| !s.is_empty())
            .unwrap_or_else(|| "Animé".to_string())
    };

    let poster_url = {
        let sel = Selector::parse(".summary_image img").unwrap();
        doc.select(&sel)
            .next()
            .and_then(|e| e.value().attr("src"))
            .map(|s| {
                if let Some(rest) = s.strip_prefix("//") {
                    format!("https:{rest}")
                } else {
                    s.to_string()
                }
            })
    };

    let li_sel = Selector::parse("li.wp-manga-chapter").unwrap();
    let a_sel = Selector::parse("a").unwrap();
    let num_re = Regex::new(r"\d+(?:\.\d+)?").unwrap();

    let mut episodes = Vec::new();
    for li in doc.select(&li_sel) {
        let Some(a) = li.select(&a_sel).next() else {
            continue;
        };
        let Some(href) = a.value().attr("href") else {
            continue;
        };
        let raw = a.text().collect::<String>().trim().to_string();

        let number = num_re
            .find_iter(&raw)
            .last()
            .and_then(|m| m.as_str().parse::<f32>().ok())
            .unwrap_or(0.0);

        let name = if number > 0.0 {
            format!("Épisode {}", number as i64)
        } else {
            raw.clone()
        };

        episodes.push(Episode {
            number,
            name,
            url: href.to_string(),
        });
    }

    if episodes.is_empty() {
        return Err(anyhow!(
            "Aucun épisode trouvé (sélecteur `li.wp-manga-chapter`). \
             URL d'animé valide ? ({url})"
        ));
    }

    let mut by_num: BTreeMap<i64, Episode> = BTreeMap::new();
    for ep in episodes {
        by_num
            .entry((ep.number * 10.0).round() as i64)
            .or_insert(ep);
    }
    let episodes: Vec<Episode> = by_num.into_values().collect();

    Ok(Anime {
        title,
        url: url.to_string(),
        poster_url,
        episodes,
    })
}

pub async fn fetch_players(
    http: &reqwest::Client,
    episode_url: &str,
) -> anyhow::Result<Vec<Player>> {
    let html = get_html(http, episode_url, BASE).await?;

    let marker = "thisChapterSources";
    let start = html
        .find(marker)
        .ok_or_else(|| anyhow!("`thisChapterSources` introuvable sur la page d'épisode"))?;
    let brace = html[start..]
        .find('{')
        .map(|i| start + i)
        .ok_or_else(|| anyhow!("objet JSON de `thisChapterSources` introuvable"))?;

    let json = balanced_object(&html[brace..])
        .ok_or_else(|| anyhow!("accolades non équilibrées dans `thisChapterSources`"))?;

    let map: BTreeMap<String, String> =
        serde_json::from_str(json).context("désérialisation de `thisChapterSources`")?;

    let src_re = Regex::new(r#"src=["']([^"']+)["']"#).unwrap();
    let mut players = Vec::new();
    for (name, iframe_html) in map {
        if let Some(c) = src_re.captures(&iframe_html) {
            let mut iframe_url = c[1].to_string();
            if iframe_url.starts_with("//") {
                iframe_url = format!("https:{iframe_url}");
            }
            players.push(Player { name, iframe_url });
        }
    }

    if players.is_empty() {
        return Err(anyhow!("aucune iframe de lecteur exploitable"));
    }
    Ok(players)
}

fn balanced_object(s: &str) -> Option<&str> {
    let bytes = s.as_bytes();
    if bytes.first() != Some(&b'{') {
        return None;
    }
    let mut depth = 0i32;
    let mut in_str = false;
    let mut escaped = false;
    for (i, &b) in bytes.iter().enumerate() {
        if in_str {
            if escaped {
                escaped = false;
            } else if b == b'\\' {
                escaped = true;
            } else if b == b'"' {
                in_str = false;
            }
            continue;
        }
        match b {
            b'"' => in_str = true,
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(&s[..=i]);
                }
            }
            _ => {}
        }
    }
    None
}
