use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

use addon_api::{Metadata, Preference};
use anyhow::{anyhow, Result};
use extism::{Function, Manifest, Plugin, Wasm};
use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};

/// A loaded WASM source addon (Extism plugin) plus its metadata.
pub struct Addon {
    plugin: Plugin,
    pub metadata: Metadata,
}

impl Addon {
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

/// An addon installed on disk (one folder per addon under the addons dir).
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct InstalledAddon {
    pub id: String,
    pub name: String,
    pub lang: String,
    pub version: String,
    #[serde(default)]
    pub nsfw: bool,
    #[serde(default)]
    pub icon_path: Option<String>,
}

/// One entry of a remote store index (the JSON the user points the app to).
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct StoreEntry {
    pub id: String,
    pub name: String,
    pub lang: String,
    pub version: String,
    #[serde(default)]
    pub nsfw: bool,
    pub wasm: String,
    #[serde(default)]
    pub icon: Option<String>,
    /// Filled by the host: which repo index this entry came from.
    #[serde(default)]
    pub repo_url: String,
    /// Filled by the host: absolute icon URL.
    #[serde(default)]
    pub icon_url: Option<String>,
    #[serde(default)]
    pub installed: bool,
}

#[derive(Deserialize)]
pub struct StoreIndex {
    pub addons: Vec<StoreEntry>,
}

/// `<app_data>/addons`, created on demand.
pub fn addons_dir(base: &Path) -> Result<PathBuf> {
    let dir = base.join("addons");
    std::fs::create_dir_all(&dir)?;
    Ok(dir)
}

fn id_dir(dir: &Path, id: &str) -> PathBuf {
    dir.join(id)
}

pub fn wasm_path(dir: &Path, id: &str) -> PathBuf {
    id_dir(dir, id).join("addon.wasm")
}

pub fn icon_path(dir: &Path, id: &str) -> PathBuf {
    id_dir(dir, id).join("icon.png")
}

pub fn read_meta(dir: &Path, id: &str) -> Result<InstalledAddon> {
    let text = std::fs::read_to_string(id_dir(dir, id).join("meta.json"))?;
    Ok(serde_json::from_str(&text)?)
}

pub fn installed(dir: &Path) -> Vec<InstalledAddon> {
    let Ok(entries) = std::fs::read_dir(dir) else {
        return Vec::new();
    };
    entries
        .flatten()
        .filter(|e| e.path().is_dir())
        .filter_map(|e| e.file_name().to_str().and_then(|id| read_meta(dir, id).ok()))
        .collect()
}

pub fn read_config(dir: &Path, id: &str) -> BTreeMap<String, String> {
    std::fs::read_to_string(id_dir(dir, id).join("config.json"))
        .ok()
        .and_then(|t| serde_json::from_str(&t).ok())
        .unwrap_or_default()
}

pub fn write_config(dir: &Path, id: &str, config: &BTreeMap<String, String>) -> Result<()> {
    let path = id_dir(dir, id);
    std::fs::create_dir_all(&path)?;
    std::fs::write(path.join("config.json"), serde_json::to_vec_pretty(config)?)?;
    Ok(())
}

/// Write a downloaded addon (wasm + optional icon) and its metadata to disk.
pub fn install(
    dir: &Path,
    entry: &StoreEntry,
    wasm: &[u8],
    icon: Option<&[u8]>,
) -> Result<InstalledAddon> {
    let path = id_dir(dir, &entry.id);
    std::fs::create_dir_all(&path)?;
    std::fs::write(path.join("addon.wasm"), wasm)?;

    let icon_path = match icon {
        Some(bytes) => {
            let p = path.join("icon.png");
            std::fs::write(&p, bytes)?;
            Some(p.to_string_lossy().into_owned())
        }
        None => None,
    };

    let meta = InstalledAddon {
        id: entry.id.clone(),
        name: entry.name.clone(),
        lang: entry.lang.clone(),
        version: entry.version.clone(),
        nsfw: entry.nsfw,
        icon_path,
    };
    std::fs::write(path.join("meta.json"), serde_json::to_vec_pretty(&meta)?)?;
    Ok(meta)
}

pub fn remove(dir: &Path, id: &str) -> Result<()> {
    let path = id_dir(dir, id);
    if path.exists() {
        std::fs::remove_dir_all(&path)?;
    }
    Ok(())
}

/// Load an installed addon with its saved user config applied.
pub fn open(dir: &Path, id: &str) -> Result<Addon> {
    let wasm = wasm_path(dir, id);
    if !wasm.exists() {
        return Err(anyhow!("addon `{id}` non installé"));
    }
    Addon::load_with_config(wasm, &read_config(dir, id))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Generic, source-agnostic smoke test: point `ADDON_WASM` at any built addon
    /// and `ADDON_ANIME_URL` at one of its anime pages. Skipped when unset, so the
    /// app stays free of any source-specific fixture.
    #[test]
    #[ignore = "set ADDON_WASM (+ optional ADDON_ANIME_URL) to run"]
    fn addon_contract_smoke() {
        let Ok(wasm) = std::env::var("ADDON_WASM") else {
            eprintln!("ADDON_WASM unset, skipping");
            return;
        };

        let mut addon = Addon::load_with_config(&wasm, &BTreeMap::new()).expect("load");
        eprintln!("loaded addon: {} ({})", addon.metadata.name, addon.metadata.id);
        assert!(!addon.metadata.id.is_empty());

        let prefs = addon.preferences();
        eprintln!("preferences: {prefs:?}");

        let Ok(url) = std::env::var("ADDON_ANIME_URL") else {
            return;
        };
        let episodes: Vec<addon_api::Episode> = addon
            .call_json(addon_api::exports::EPISODE_LIST, &addon_api::UrlInput { url })
            .expect("episode_list");
        eprintln!("episodes: {}", episodes.len());
        assert!(!episodes.is_empty());

        let hosters: Vec<addon_api::Hoster> = addon
            .call_json(
                addon_api::exports::HOSTER_LIST,
                &addon_api::UrlInput { url: episodes[0].url.clone() },
            )
            .expect("hoster_list");
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
        assert!(resolved > 0, "no hoster resolved");
    }
}
