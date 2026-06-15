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
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize", "340x438^", "-gravity", "center", "-extent", "340x438", "+repage", "-background", "none", "-gravity", "Northwest", "-geometry", "+78+48", ")",
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
            "-scale", "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "495x307^", "-gravity", "center", "-extent", "495x307", "+repage", "-gravity", "Northwest", "-geometry", "+8+141",
            "{ASSETS}/folderhorizontal-main.png", ")", "-compose", "over", "-composite", "(",
            "{ASSETS}/folderhorizontal-mainfx.png", "-scale", "512x512!", ")", "-compose", "over",
            "-composite",
        ],
    },
    Template {
        id: "folder-vertical",
        name: "Dossier vertical",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-scale",
            "512x512!", "-blur", "0x19", "{ASSETS}/foldervertical-side.png", ")", "-compose",
            "over", "-composite", "(", "{ASSETS}/foldervertical-sidefx.png", "-scale", "512x512!",
            ")", "-compose", "over", "-composite", "(", "{ASSETS}/foldervertical-sideshadow.png",
            "-scale", "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "346x490^", "-gravity", "center", "-extent", "346x490", "+repage", "-gravity", "Northwest", "-geometry", "+70+14",
            "{ASSETS}/foldervertical-main.png", ")", "-compose", "over", "-composite", "(",
            "{ASSETS}/foldervertical-mainfx.png", "-scale", "512x512!", ")", "-compose", "over",
            "-composite",
        ],
    },
    Template {
        id: "dvdcase-transparent",
        name: "Boîtier plastique transparent",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize", "336x474^", "-gravity", "center", "-extent", "336x474", "+repage", "-gravity", "Northwest", "-geometry", "+108+14",
            "{ASSETS}/dvdcase-plastic-mask.png", ")", "-compose", "over", "-composite", "(",
            "{ASSETS}/dvdcase-plastic.png", "-resize", "512x512!", ")", "-compose", "Over",
            "-composite",
        ],
    },
    Template {
        id: "dvdbox-dark",
        name: "Boîtier DVD sombre",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(",
            "{ASSETS}/disc-vinyl.png", "-scale", "340x340!", "-background", "none", "-extent",
            "512x512-164-84", "(", "+clone", "-background", "BLACK", "-shadow", "100x1.3+2+2", ")",
            "+swap", "-background", "none", "-layers", "merge", "-extent", "512x512", ")",
            "-compose", "Over", "-composite", "(", "{INPUT}", "-resize", "340x483^", "-gravity", "center", "-extent", "340x483", "+repage", "-background",
            "none", "-gravity", "Northwest", "-geometry", "+7+11", ")", "-compose", "Over",
            "-composite", "(", "{ASSETS}/dvdbox-dark.png", "-resize", "512x512!", ")", "-compose",
            "Over", "-composite",
        ],
    },
    Template {
        id: "dvdbox-light",
        name: "Boîtier DVD clair",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(",
            "{ASSETS}/disc-vinyl.png", "-scale", "340x340!", "-background", "none", "-extent",
            "512x512-164-84", "(", "+clone", "-background", "BLACK", "-shadow", "100x1.3+2+2", ")",
            "+swap", "-background", "none", "-layers", "merge", "-extent", "512x512", ")",
            "-compose", "Over", "-composite", "(", "{INPUT}", "-resize", "340x483^", "-gravity", "center", "-extent", "340x483", "+repage", "-background",
            "none", "-gravity", "Northwest", "-geometry", "+7+11", ")", "-compose", "Over",
            "-composite", "(", "{ASSETS}/dvdbox-light.png", "-resize", "512x512!", ")", "-compose",
            "Over", "-composite",
        ],
    },
    Template {
        id: "windows-11-a",
        name: "Windows 11",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize",
            "2x2!", "-resize", "1000x1000!", "-scale", "390x390!", "-gravity", "Center",
            "-modulate", "105,150", "-brightness-contrast", "-10x0", "-blur", "0x25",
            "-brightness-contrast", "5x20", "-modulate", "95,100", "{ASSETS}/Win11A-Back.png",
            "-scale", "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize",
            "2x2!", "-resize", "1000x1000!", "-scale", "390x390!", "-gravity", "Center",
            "-modulate", "100,150", "-blur", "0x25", "-brightness-contrast", "5x20",
            "-brightness-contrast", "-50x10", "{ASSETS}/Win11A-Back-Gradient.png", "-scale",
            "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage",
            "-gravity", "Northwest", "-geometry", "+5+117", "{ASSETS}/Win11A-Front.png", ")",
            "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage", "-gravity",
            "Northwest", "-geometry", "+5+117", "-brightness-contrast", "-9x10",
            "{ASSETS}/Win11A-Front-BevelShadow.png", ")", "-compose", "over", "-composite", "(",
            "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage", "-gravity", "Northwest", "-geometry", "+5+117",
            "-modulate", "110,110", "-brightness-contrast", "25x10", "{ASSETS}/Win11A-Front-Bevel.png",
            ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage", "-gravity",
            "Northwest", "-geometry", "+5+117", "-brightness-contrast", "20x10", "-modulate",
            "110,110", "{ASSETS}/Win11A-Front-Gradient.png", ")", "-compose", "over", "-composite",
            "(", "{INPUT}", "-resize", "498x320^", "-gravity", "center", "-extent", "498x320", "+repage", "-gravity", "Northwest", "-geometry", "+5+117",
            "-brightness-contrast", "0x10", "-modulate", "94,100",
            "{ASSETS}/Win11A-Front-GradientShadow.png", ")", "-compose", "over", "-composite",
        ],
    },
    Template {
        id: "beorigin",
        name: "BeOrigin (style macOS)",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}",
            "-modulate", "100,150", "-modulate", "80,100", "-brightness-contrast", "0x5",
            "-modulate", "100,130", "-resize", "2x2!", "-resize", "1000x1000!", "-scale",
            "512x512!", "-gravity", "Center", "-blur", "0x25", "-brightness-contrast", "-5x0",
            "-brightness-contrast", "0x27", "-blur", "0x20", "{ASSETS}/BeOriginal-back.png", ")",
            "-compose", "over", "-composite", "(", "{ASSETS}/BeOriginal-BackFx.png", "-scale",
            "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "480x318^", "-gravity", "center", "-extent", "480x318", "+repage",
            "-gravity", "Northwest", "-geometry", "+18+124", "{ASSETS}/BeOriginal-front.png", ")",
            "-compose", "over", "-composite", "(", "{ASSETS}/BeOriginal-FrontFx.png", "-scale",
            "512x512!", ")", "-compose", "over", "-composite",
        ],
    },
    Template {
        id: "discart",
        name: "Disque",
        args: &[
            "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}", "-resize", "485x485^", "-gravity", "center", "-extent", "485x485", "+repage", "-gravity", "center", "{ASSETS}/DiscArt-Main.png", ")", "-compose", "over",
            "-composite", "(", "{ASSETS}/DiscArt-Transparent.png", "-scale", "512x512!", ")",
            "-compose", "over", "-composite", "(", "{ASSETS}/DiscArt-Label.png", "-scale",
            "512x512!", ")", "-compose", "over", "-composite", "(", "{ASSETS}/DiscArt-Logo.png",
            "-scale", "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-scale",
            "900x900!", "-blur", "0x30", "-brightness-contrast", "0x30", "{ASSETS}/DiscArt-Border.png",
            ")", "-compose", "over", "-composite",
        ],
    },
];

/// Templates needing several ImageMagick passes; `{TMP}` is a shared scratch image
/// written by an earlier pass and read by a later one.
struct MultiTemplate {
    id: &'static str,
    name: &'static str,
    passes: &'static [&'static [&'static str]],
}

const MULTI_TEMPLATES: &[MultiTemplate] = &[
    MultiTemplate {
        id: "windows-11-cover",
        name: "Windows 11 (pochette)",
        passes: &[
            &[
                "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}",
                "-scale", "458x295!", "-gravity", "center", "-geometry", "+1+14",
                "{ASSETS}/Win11Cover-Front.png", ")", "-compose", "over", "-composite",
            ],
            &[
                "{TMP}", "-brightness-contrast", "0x10", "-modulate", "95,70", "-background",
                "white", "-channel", "a", "-alpha", "remove", "-channel", "rgb", "-negate",
                "-alpha", "shape",
            ],
            &[
                "(", "{ASSETS}/Win11Cover.png", "-scale", "512x512!", "-modulate", "50,100",
                "-brightness-contrast", "-20x35", "{TMP}", ")", "-compose", "Over", "-composite",
            ],
        ],
    },
    MultiTemplate {
        id: "dualtab-vertical",
        name: "Double onglet vertical",
        passes: &[
            &[
                "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(", "{INPUT}",
                "-resize", "3x3!", "-resize", "1000x1000!", "-scale", "512x512!", "-modulate",
                "100,130", "-brightness-contrast", "8x13", "-blur", "0x50", "{ASSETS}/DualTabV-Tab2.png",
                ")", "-compose", "over", "-composite", "(", "{ASSETS}/DualTabV-Tab2FX.png", "-scale",
                "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "3x3!",
                "-resize", "1000x1000!", "-scale", "512x512!", "-modulate", "100,130",
                "-brightness-contrast", "8x13", "-blur", "0x50", "{ASSETS}/DualTabV-Tab1.png", ")",
                "-compose", "over", "-composite", "(", "{ASSETS}/DualTabV-Tab1FX.png", "-scale",
                "512x512!", ")", "-compose", "over", "-composite", "(", "{INPUT}", "-resize", "372x482^", "-gravity", "center", "-extent", "372x482", "+repage", "-brightness-contrast", "5x15", "-modulate", "100,110", "-gravity",
                "Northwest", "-geometry", "+51+4", "{ASSETS}/DualTabV-Front.png", ")", "-compose",
                "over", "-composite", "(", "{ASSETS}/DualTabV-FrontFX.png", "-scale", "512x512!", ")",
                "-compose", "over", "-composite",
            ],
            &[
                "(", "-size", "512x512", "xc:none", ")", "-compose", "Over", "(",
                "{ASSETS}/DualTabV-DropShadow.png", "-scale", "512x512!", ")", "-compose", "over",
                "-composite", "(", "{TMP}", "-scale", "512x512!", ")", "-compose", "over",
                "-composite",
            ],
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
        .map(|t| (t.id, t.name))
        .chain(MULTI_TEMPLATES.iter().map(|t| (t.id, t.name)))
        .map(|(id, name)| TemplateInfo {
            id: id.to_string(),
            name: name.to_string(),
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

/// Resolve the ImageMagick binary. Prefer the bundled IM7 `magick` (recipes are
/// authored for v7; the system `convert` v6 composites differently), then fall
/// back to a system `magick`, then `convert`.
#[allow(unused_variables)]
fn imagemagick(assets_dir: &Path) -> Option<String> {
    #[cfg(target_os = "linux")]
    {
        use std::os::unix::fs::PermissionsExt;
        let bundled = assets_dir.join("bin").join("magick");
        if bundled.is_file() {
            // Bundling may strip the executable bit; restore it best-effort.
            let _ = std::fs::set_permissions(&bundled, std::fs::Permissions::from_mode(0o755));
            return Some(bundled.to_string_lossy().into_owned());
        }
    }
    for bin in ["magick", "convert"] {
        if Command::new(bin)
            .arg("-version")
            .output()
            .map(|o| o.status.success())
            .unwrap_or(false)
        {
            return Some(bin.to_string());
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
    let passes: Vec<&[&str]> = if let Some(t) = TEMPLATES.iter().find(|t| t.id == template_id) {
        vec![t.args]
    } else if let Some(m) = MULTI_TEMPLATES.iter().find(|m| m.id == template_id) {
        m.passes.to_vec()
    } else {
        return Err(format!("gabarit inconnu : {template_id}"));
    };

    let input = poster.to_string_lossy();
    let assets = assets_images.to_string_lossy();
    let tmp = out.with_file_name(".icon-mask.tmp.png");
    let tmp_s = tmp.to_string_lossy();
    let last = passes.len() - 1;

    for (i, pass) in passes.iter().enumerate() {
        let target = if i == last { out } else { tmp.as_path() };
        let mut args: Vec<String> = pass
            .iter()
            .map(|a| {
                a.replace("{INPUT}", &input)
                    .replace("{ASSETS}", &assets)
                    .replace("{TMP}", &tmp_s)
            })
            .collect();
        if i == last && as_ico {
            args.push("-define".into());
            args.push("icon:auto-resize=16,32,48,64,128,256".into());
        }
        args.push(target.to_string_lossy().into_owned());

        let output = Command::new(bin)
            .args(&args)
            .env("MAGICK_THREAD_LIMIT", "2")
            .output()
            .map_err(|e| e.to_string())?;
        if !output.status.success() {
            let _ = std::fs::remove_file(&tmp);
            return Err(format!(
                "ImageMagick a échoué : {}",
                String::from_utf8_lossy(&output.stderr).trim()
            ));
        }
    }
    let _ = std::fs::remove_file(&tmp);
    Ok(())
}

/// Download the poster, compose the styled icon and set it on `folder`.
pub fn generate_and_apply(
    assets_dir: &Path,
    folder: &Path,
    poster_bytes: &[u8],
    template_id: &str,
) -> Result<(), String> {
    let bin = imagemagick(assets_dir)
        .ok_or("ImageMagick introuvable — installez « imagemagick » (commande magick/convert).")?;
    let assets_images = assets_dir.join("images");

    std::fs::create_dir_all(folder).map_err(|e| e.to_string())?;
    let src = folder.join(".icon-src.tmp");
    std::fs::write(&src, poster_bytes).map_err(|e| e.to_string())?;

    let result = set_for_os(&bin, &assets_images, folder, &src, template_id);
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
    // A fresh filename each time so the file manager's cached icon is invalidated
    // (re-setting the same path is a no-op that GNOME/Nautilus won't refresh).
    use std::time::{SystemTime, UNIX_EPOCH};
    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis())
        .unwrap_or(0);
    if let Ok(entries) = std::fs::read_dir(folder) {
        for e in entries.flatten() {
            let n = e.file_name();
            let n = n.to_string_lossy();
            if n.starts_with(".folder-icon-") || n == ".folder.png" {
                let _ = std::fs::remove_file(e.path());
            }
        }
    }
    let png = folder.join(format!(".folder-icon-{stamp}.png"));
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
