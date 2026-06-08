use std::path::{Path, PathBuf};

use eframe::egui;
use serde::Deserialize;

#[derive(Deserialize)]
struct Layout {
    #[serde(default = "default_count")]
    icon_count: usize,
    buttons: Vec<LayoutButton>,
}

fn default_count() -> usize {
    12
}

#[derive(Deserialize)]
struct LayoutButton {
    #[serde(default)]
    id: String,
    #[serde(default)]
    label: String,
    #[serde(default)]
    tip: String,
    #[serde(default)]
    menu: Vec<String>,
    #[serde(default)]
    slot: usize,
    #[serde(default)]
    sep: bool,
}

pub struct Button {
    pub id: String,
    pub label: String,
    pub tip: String,
    pub menu: Vec<String>,
    pub slot: usize,
    pub separator: bool,
}

pub struct Skin {
    pub id: String,
    pub name: String,
    large: PathBuf,
    large_hot: PathBuf,
}

pub struct Toolbar {
    pub buttons: Vec<Button>,
    pub skins: Vec<Skin>,
    pub skin_id: String,
    pub icon_count: usize,
    pub normal: Option<egui::TextureHandle>,
    pub hot: Option<egui::TextureHandle>,
}

impl Toolbar {
    pub fn load(ctx: &egui::Context) -> Self {
        let dir = assets_dir();
        let layout: Layout = std::fs::read_to_string(dir.join("toolbar.tbi"))
            .ok()
            .and_then(|s| serde_json::from_str(&s).ok())
            .unwrap_or(Layout {
                icon_count: 12,
                buttons: vec![],
            });
        let buttons = layout
            .buttons
            .into_iter()
            .map(|b| Button {
                id: b.id,
                label: b.label,
                tip: b.tip,
                menu: b.menu,
                slot: b.slot,
                separator: b.sep,
            })
            .collect();

        let mut skins = scan_skins(&dir.join("skins"));
        skins.sort_by(|a, b| a.name.cmp(&b.name));
        let skin_id = if skins.iter().any(|s| s.id == "blue") {
            "blue".to_string()
        } else {
            skins.first().map(|s| s.id.clone()).unwrap_or_default()
        };

        let mut tb = Toolbar {
            buttons,
            skins,
            skin_id: skin_id.clone(),
            icon_count: layout.icon_count.max(1),
            normal: None,
            hot: None,
        };
        tb.set_skin(ctx, &skin_id);
        tb
    }

    pub fn set_skin(&mut self, ctx: &egui::Context, skin_id: &str) {
        if let Some(skin) = self.skins.iter().find(|s| s.id == skin_id) {
            self.normal = load_strip(ctx, &skin.large);
            self.hot = load_strip(ctx, &skin.large_hot).or_else(|| self.normal.clone());
            self.skin_id = skin_id.to_string();
        }
    }
}

fn scan_skins(dir: &Path) -> Vec<Skin> {
    let mut out = Vec::new();
    let Ok(entries) = std::fs::read_dir(dir) else {
        return out;
    };
    for entry in entries.flatten() {
        let path = entry.path();
        if path.extension().and_then(|e| e.to_str()) != Some("tbi") {
            continue;
        }
        let Ok(text) = std::fs::read_to_string(&path) else {
            continue;
        };
        let mut name = None;
        let mut large = None;
        let mut hot = None;
        for line in text.lines() {
            let line = line.trim();
            if let Some(v) = line.strip_prefix("name=") {
                name = Some(v.trim().to_string());
            } else if let Some(v) = line.strip_prefix("largeHot=") {
                hot = Some(v.trim().replace('\\', "/"));
            } else if let Some(v) = line.strip_prefix("large=") {
                large = Some(v.trim().replace('\\', "/"));
            }
        }
        let id = path
            .file_stem()
            .and_then(|s| s.to_str())
            .unwrap_or("")
            .to_string();
        if let (Some(name), Some(large)) = (name, large) {
            let hot = hot.unwrap_or_else(|| large.clone());
            out.push(Skin {
                id,
                name,
                large: dir.join(large),
                large_hot: dir.join(hot),
            });
        }
    }
    out
}

fn assets_dir() -> PathBuf {
    let cwd = PathBuf::from("assets/toolbar");
    if cwd.join("toolbar.tbi").exists() {
        return cwd;
    }
    if let Some(dir) = std::env::current_exe()
        .ok()
        .and_then(|e| e.parent().map(Path::to_path_buf))
    {
        let p = dir.join("assets/toolbar");
        if p.join("toolbar.tbi").exists() {
            return p;
        }
    }
    cwd
}

fn load_strip(ctx: &egui::Context, path: &Path) -> Option<egui::TextureHandle> {
    let img = image::open(path).ok()?.to_rgb8();
    let (w, h) = img.dimensions();
    let key = img.get_pixel(0, 0).0;
    let mut rgba = Vec::with_capacity((w * h * 4) as usize);
    for p in img.pixels() {
        let c = p.0;
        if c == key {
            rgba.extend_from_slice(&[0, 0, 0, 0]);
        } else {
            rgba.extend_from_slice(&[c[0], c[1], c[2], 255]);
        }
    }
    let image = egui::ColorImage::from_rgba_unmultiplied([w as usize, h as usize], &rgba);
    Some(ctx.load_texture(
        format!("tbstrip:{}", path.display()),
        image,
        egui::TextureOptions::LINEAR,
    ))
}
