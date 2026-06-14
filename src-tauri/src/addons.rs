use std::collections::BTreeMap;
use std::path::Path;

use addon_api::{Metadata, Preference};
use anyhow::Result;
use extism::{Function, Manifest, Plugin, Wasm};
use serde::de::DeserializeOwned;
use serde::Serialize;

/// A loaded WASM source addon (Extism plugin) plus its metadata.
pub struct Addon {
    plugin: Plugin,
    pub metadata: Metadata,
}

impl Addon {
    pub fn load(path: impl AsRef<Path>) -> Result<Self> {
        Self::load_with_config(path, &BTreeMap::new())
    }

    pub fn load_with_config(
        path: impl AsRef<Path>,
        config: &BTreeMap<String, String>,
    ) -> Result<Self> {
        let no_imports: Vec<Function> = Vec::new();
        let mut manifest =
            Manifest::new([Wasm::file(path.as_ref().to_path_buf())]).with_allowed_host("*");
        for (k, v) in config {
            manifest = manifest.with_config_key(k, v);
        }
        let mut plugin = Plugin::new(&manifest, no_imports, true)?;
        let out: Vec<u8> = plugin.call(addon_api::exports::METADATA, b"".as_slice())?;
        let metadata = serde_json::from_slice(&out)?;
        Ok(Self { plugin, metadata })
    }

    /// The settings schema the addon declares (empty if it exports none).
    pub fn preferences(&mut self) -> Vec<Preference> {
        self.call_json(addon_api::exports::PREFERENCES, &())
            .unwrap_or_default()
    }

    pub fn call_json<I: Serialize, O: DeserializeOwned>(
        &mut self,
        name: &str,
        input: &I,
    ) -> Result<O> {
        let bytes = serde_json::to_vec(input)?;
        let out: Vec<u8> = self.plugin.call(name, bytes.as_slice())?;
        Ok(serde_json::from_slice(&out)?)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const VOIRANIME_WASM: &str = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../anime-dm-addons/target/wasm32-unknown-unknown/release/voiranime.wasm"
    );

    #[test]
    #[ignore = "requires a built addon"]
    fn voiranime_config_override() {
        if !std::path::Path::new(VOIRANIME_WASM).exists() {
            eprintln!("addon wasm not built, skipping");
            return;
        }
        let mut addon = Addon::load(VOIRANIME_WASM).expect("load");
        let prefs = addon.preferences();
        eprintln!("preferences: {prefs:?}");
        assert!(prefs.iter().any(|p| p.key == "base_url"));
        assert_eq!(addon.metadata.base_url, "https://voir-anime.to");

        let custom = BTreeMap::from([("base_url".to_string(), "https://mirror.example.org".to_string())]);
        let custom_addon = Addon::load_with_config(VOIRANIME_WASM, &custom).expect("load cfg");
        eprintln!("overridden base_url = {}", custom_addon.metadata.base_url);
        assert_eq!(custom_addon.metadata.base_url, "https://mirror.example.org");
    }

    #[test]
    #[ignore = "requires a built addon + network"]
    fn voiranime_end_to_end() {
        if !std::path::Path::new(VOIRANIME_WASM).exists() {
            eprintln!("addon wasm not built, skipping");
            return;
        }
        let mut addon = Addon::load(VOIRANIME_WASM).expect("load");
        assert_eq!(addon.metadata.id, "fr.voiranime");

        let episodes: Vec<addon_api::Episode> = addon
            .call_json(
                addon_api::exports::EPISODE_LIST,
                &addon_api::UrlInput {
                    url: "https://voir-anime.to/anime/dragon-ball-vf/".to_string(),
                },
            )
            .expect("episode_list");
        eprintln!("metadata={} episodes={}", addon.metadata.name, episodes.len());
        assert!(!episodes.is_empty());

        let ep = &episodes[0];
        let hosters: Vec<addon_api::Hoster> = addon
            .call_json(
                addon_api::exports::HOSTER_LIST,
                &addon_api::UrlInput { url: ep.url.clone() },
            )
            .expect("hoster_list");
        eprintln!(
            "hosters: {:?}",
            hosters.iter().map(|h| h.name.as_str()).collect::<Vec<_>>()
        );

        let mut resolved = 0;
        for h in &hosters {
            let videos: Vec<addon_api::Video> = addon
                .call_json(addon_api::exports::VIDEO_LIST, h)
                .expect("video_list");
            if let Some(v) = videos.first() {
                eprintln!("  {} [{}] -> {}", h.name, v.quality, v.url);
                resolved += 1;
            }
        }
        assert!(resolved > 0, "no HTTP hoster resolved");
    }
}
