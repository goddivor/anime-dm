use std::path::Path;

use addon_api::Metadata;
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
        let no_imports: Vec<Function> = Vec::new();
        let manifest = Manifest::new([Wasm::file(path.as_ref().to_path_buf())]);
        let mut plugin = Plugin::new(&manifest, no_imports, true)?;
        let out: Vec<u8> = plugin.call(addon_api::exports::METADATA, b"".as_slice())?;
        let metadata = serde_json::from_slice(&out)?;
        Ok(Self { plugin, metadata })
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
