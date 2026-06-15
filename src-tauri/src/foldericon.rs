use std::path::{Path, PathBuf};
use std::process::Command;

use serde::Serialize;
use tauri::{AppHandle, Manager};

/// A folder-icon template: a faithful ImageMagick argument sequence.
/// Placeholders: `{INPUT}` = source poster file, `{ASSETS}` = bundled layers dir.
/// Recipes are ported from RightClickFolderIconTools (base compositing only;
/// the optional rating/genre/logo overlays — which need `.nfo` metadata — are dropped).
struct Template {
    id: &'static str,
    name: &'static str,
    args: &'static [&'static str],
}

const TEMPLATES: &[Template] = &[
    Template {
        id: "none",
        name: "Affiche brute",
        args: &[
            "{INPUT}", "-resize", "512x512", "-background", "none", "-gravity", "center", "-extent",
            "512x512",
        ],
    },
    Template {
        id: "shadow",
        name: "Ombre seule",
        args: &[
            "{INPUT}", "-resize", "490x490", "(", "+clone", "-background", "BLACK", "-shadow",
            "60x5+5+6.5", ")", "+swap", "-background", "none", "-layers", "merge", "-gravity",
            "center", "-extent", "512x512",
        ],
    },
    Template {
        id: "dvdcase-bluray",
        name: "Boîtier Blu-ray",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-scale",
            "340x438!", "-background", "none", "-gravity", "Northwest", "-geometry", "+78+48", ")",
            "-compose", "Over", "-composite", "(", "{ASSETS}/dvdcase-bluray.png", "-resize",
            "512x512!", ")", "-compose", "Over", "-composite", "(", "+clone", "-background",
            "BLACK", "-shadow", "0x2+2+2.5", ")", "+swap", "-background", "none", "-layers",
            "merge", "-extent", "512x512",
        ],
    },
    Template {
        id: "folder-horizontal",
        name: "Dossier horizontal",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-scale",
            "512x512!", "-blur", "0x19", "{ASSETS}/folderhorizontal-top.png", ")", "-compose",
            "over", "-composite", "(", "{ASSETS}/folderhorizontal-topfx.png", "-scale", "512x512!",
            ")", "-compose", "over", "-composite", "(", "{ASSETS}/folderhorizontal-topshadow.png",
            "-scale", "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-scale",
            "495x307!", "-gravity", "Northwest", "-geometry", "+8+141",
            "{ASSETS}/folderhorizontal-main.png", ")", "-compose", "over", "-composite", "(",
            "{ASSETS}/folderhorizontal-mainfx.png", "-scale", "512x512!", ")", "-compose", "over",
            "-composite",
        ],
    },
];

#[derive(Serialize)]
pub struct TemplateInfo {
    pub id: String,
    pub name: String,
}

pub fn template_list() -> Vec<TemplateInfo> {
    TEMPLATES
        .iter()
        .map(|t| TemplateInfo {
            id: t.id.to_string(),
            name: t.name.to_string(),
        })
        .collect()
}

pub fn assets_dir(app: &AppHandle) -> Result<PathBuf, String> {
    if let Ok(base) = app.path().resource_dir() {
        let bundled = base.join("resources").join("folder-templates");
        if bundled.join("images").is_dir() {
            return Ok(bundled);
        }
    }
    // Dev fallback: resources aren't copied into target/ during `tauri dev`.
    let dev = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("resources")
        .join("folder-templates");
    if dev.join("images").is_dir() {
        return Ok(dev);
    }
    Err("ressources des gabarits introuvables".to_string())
}

/// `magick` (ImageMagick v7) or `convert` (v6); `None` if neither is installed.
fn imagemagick() -> Option<&'static str> {
    for bin in ["magick", "convert"] {
        if Command::new(bin)
            .arg("-version")
            .output()
            .map(|o| o.status.success())
            .unwrap_or(false)
        {
            return Some(bin);
        }
    }
    None
}

/// Compose the icon for `template_id` from `poster` into `out`, as ICO when
/// `as_ico` (multi-size) else as a 512px PNG.
fn compose(
    bin: &str,
    assets_images: &Path,
    poster: &Path,
    template_id: &str,
    out: &Path,
    as_ico: bool,
) -> Result<(), String> {
    let tpl = TEMPLATES
        .iter()
        .find(|t| t.id == template_id)
        .ok_or_else(|| format!("gabarit inconnu : {template_id}"))?;

    let input = poster.to_string_lossy();
    let assets = assets_images.to_string_lossy();
    let mut args: Vec<String> = tpl
        .args
        .iter()
        .map(|a| a.replace("{INPUT}", &input).replace("{ASSETS}", &assets))
        .collect();
    if as_ico {
        args.push("-define".into());
        args.push("icon:auto-resize=16,32,48,64,128,256".into());
    }
    args.push(out.to_string_lossy().into_owned());

    let output = Command::new(bin)
        .args(&args)
        .output()
        .map_err(|e| e.to_string())?;
    if !output.status.success() {
        return Err(format!(
            "ImageMagick a échoué : {}",
            String::from_utf8_lossy(&output.stderr).trim()
        ));
    }
    Ok(())
}

/// Download the poster, compose the styled icon and set it on `folder`.
pub fn generate_and_apply(
    assets_dir: &Path,
    folder: &Path,
    poster_bytes: &[u8],
    template_id: &str,
) -> Result<(), String> {
    let bin = imagemagick()
        .ok_or("ImageMagick introuvable — installez « imagemagick » (commande magick/convert).")?;
    let assets_images = assets_dir.join("images");

    std::fs::create_dir_all(folder).map_err(|e| e.to_string())?;
    let src = folder.join(".icon-src.tmp");
    std::fs::write(&src, poster_bytes).map_err(|e| e.to_string())?;

    let result = set_for_os(bin, &assets_images, folder, &src, template_id);
    let _ = std::fs::remove_file(&src);
    result
}

#[cfg(target_os = "linux")]
fn set_for_os(
    bin: &str,
    assets_images: &Path,
    folder: &Path,
    src: &Path,
    template_id: &str,
) -> Result<(), String> {
    let png = folder.join(".folder.png");
    compose(bin, assets_images, src, template_id, &png, false)?;

    let mut applied = false;
    let uri = format!("file://{}", png.display());
    if Command::new("gio")
        .args([
            "set",
            &folder.to_string_lossy(),
            "metadata::custom-icon",
            &uri,
        ])
        .status()
        .map(|s| s.success())
        .unwrap_or(false)
    {
        applied = true;
    }
    let dot = folder.join(".directory");
    if std::fs::write(
        &dot,
        format!("[Desktop Entry]\nIcon={}\n", png.display()),
    )
    .is_ok()
    {
        applied = true;
    }
    if applied {
        Ok(())
    } else {
        Err("aucune méthode d'icône n'a fonctionné (gio absent ?)".to_string())
    }
}

#[cfg(target_os = "windows")]
fn set_for_os(
    bin: &str,
    assets_images: &Path,
    folder: &Path,
    src: &Path,
    template_id: &str,
) -> Result<(), String> {
    let ico = folder.join("folder.ico");
    compose(bin, assets_images, src, template_id, &ico, true)?;

    let desktop_ini = folder.join("desktop.ini");
    let ini = format!(
        "[.ShellClassInfo]\r\nIconResource={},0\r\n[ViewState]\r\nMode=\r\nVid=\r\nFolderType=Generic\r\n",
        ico.display()
    );
    std::fs::write(&desktop_ini, ini).map_err(|e| e.to_string())?;

    let _ = Command::new("attrib").args(["+H", "+S", &desktop_ini.to_string_lossy()]).status();
    let _ = Command::new("attrib").args(["+H", &ico.to_string_lossy()]).status();
    let _ = Command::new("attrib").args(["+R", &folder.to_string_lossy()]).status();
    Ok(())
}

#[cfg(target_os = "macos")]
fn set_for_os(
    bin: &str,
    assets_images: &Path,
    folder: &Path,
    src: &Path,
    template_id: &str,
) -> Result<(), String> {
    let png = folder.join(".folder.png");
    compose(bin, assets_images, src, template_id, &png, false)?;

    let script = format!(
        "ObjC.import('Cocoa');\nvar image = $.NSImage.alloc.initWithContentsOfFile({:?});\nif (image.isNil()) {{ throw new Error('invalid image'); }}\nvar ok = $.NSWorkspace.sharedWorkspace.setIconForFileOptions(image, {:?}, 0);\nif (!ok) {{ throw new Error('NSWorkspace returned false'); }}",
        png.to_string_lossy(),
        folder.to_string_lossy()
    );
    use std::io::Write;
    let mut child = Command::new("osascript")
        .args(["-l", "JavaScript", "-"])
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::null())
        .stderr(std::process::Stdio::null())
        .spawn()
        .map_err(|e| e.to_string())?;
    if let Some(mut stdin) = child.stdin.take() {
        stdin.write_all(script.as_bytes()).map_err(|e| e.to_string())?;
    }
    let status = child.wait().map_err(|e| e.to_string())?;
    if status.success() {
        Ok(())
    } else {
        Err("osascript a échoué (permission Finder ?)".to_string())
    }
}
