use std::path::{Path, PathBuf};

use eframe::egui;
use serde::Deserialize;

#[derive(Deserialize)]
struct Tbi {
    buttons: Vec<TbiButton>,
}

#[derive(Deserialize)]
struct TbiButton {
    #[serde(default)]
    id: String,
    #[serde(default)]
    icon: String,
    #[serde(default)]
    label: String,
    #[serde(default)]
    menu: Vec<String>,
    #[serde(default)]
    sep: bool,
}

pub struct ToolbarItem {
    pub id: String,
    pub label: String,
    pub menu: Vec<String>,
    pub icon: Option<egui::TextureHandle>,
    pub separator: bool,
}

pub struct Toolbar {
    pub items: Vec<ToolbarItem>,
}

impl Toolbar {
    pub fn load(ctx: &egui::Context) -> Self {
        let dir = assets_dir();
        let items = std::fs::read_to_string(dir.join("toolbar.tbi"))
            .ok()
            .and_then(|s| serde_json::from_str::<Tbi>(&s).ok())
            .map(|tbi| {
                tbi.buttons
                    .into_iter()
                    .map(|b| ToolbarItem {
                        icon: (!b.sep && !b.icon.is_empty())
                            .then(|| load_icon(ctx, &dir.join(&b.icon)))
                            .flatten(),
                        id: b.id,
                        label: b.label,
                        menu: b.menu,
                        separator: b.sep,
                    })
                    .collect()
            })
            .unwrap_or_default();
        Self { items }
    }
}

fn assets_dir() -> PathBuf {
    let cwd = PathBuf::from("assets/toolbar");
    if cwd.join("toolbar.tbi").exists() {
        return cwd;
    }
    if let Some(dir) = std::env::current_exe().ok().and_then(|e| e.parent().map(Path::to_path_buf)) {
        let p = dir.join("assets/toolbar");
        if p.join("toolbar.tbi").exists() {
            return p;
        }
    }
    cwd
}

fn load_icon(ctx: &egui::Context, path: &Path) -> Option<egui::TextureHandle> {
    let img = image::open(path).ok()?.to_rgb8();
    let (w, h) = img.dimensions();
    let mut rgba = Vec::with_capacity((w * h * 4) as usize);
    for p in img.pixels() {
        let [r, g, b] = p.0;
        if r == 255 && g == 0 && b == 255 {
            rgba.extend_from_slice(&[0, 0, 0, 0]);
        } else {
            rgba.extend_from_slice(&[r, g, b, 255]);
        }
    }
    let image = egui::ColorImage::from_rgba_unmultiplied([w as usize, h as usize], &rgba);
    Some(ctx.load_texture(format!("tb:{}", path.display()), image, egui::TextureOptions::LINEAR))
}
