use serde::Serialize;

#[derive(Clone, Debug, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Anime {
    pub title: String,
    pub url: String,
    pub poster_url: Option<String>,
    pub episodes: Vec<Episode>,
}

#[derive(Clone, Debug, Serialize)]
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
