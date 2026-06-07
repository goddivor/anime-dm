//! Anime Download Manager — gestionnaire de téléchargement type IDM pour sites d'animés.
//! Premier site pris en charge : voir-anime.to (architecture pensée pour en accueillir d'autres).

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod downloader;
mod extractors;
mod gui;
mod headless;
mod model;
mod net;
mod scraper;
mod selection;
mod worker;

use eframe::egui;

fn main() -> eframe::Result<()> {
    // Mode diagnostic sans GUI : valide la chaîne scrape -> extract -> ffmpeg en réseau réel.
    if std::env::args().any(|a| a == "--selftest") {
        std::process::exit(selftest());
    }

    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default()
            .with_inner_size([920.0, 560.0])
            .with_min_inner_size([640.0, 360.0])
            .with_title("Anime Download Manager"),
        ..Default::default()
    };

    eframe::run_native(
        "Anime DM",
        options,
        Box::new(|cc| Ok(Box::new(gui::App::new(cc)))),
    )
}

/// Diagnostic de bout en bout (sans interface) : utilisé pour vérifier la logique réseau.
/// Renvoie le code de sortie process (0 = succès).
fn selftest() -> i32 {
    // URL d'animé optionnelle après --selftest (les épisodes récents ont des liens plus frais).
    let url = std::env::args()
        .skip_while(|a| a != "--selftest")
        .nth(1)
        .filter(|a| a.starts_with("http"))
        .unwrap_or_else(|| "https://voir-anime.to/anime/dragon-ball-vf/".to_string());
    let url = url.as_str();
    let rt = tokio::runtime::Runtime::new().expect("runtime tokio");
    rt.block_on(async {
        let http = net::client().unwrap();

        // Accepte soit une URL d'animé (on prend l'ep 1), soit directement une URL d'épisode.
        let is_episode = url.trim_end_matches('/').split('/').count() >= 6;
        let episode_url = if is_episode {
            println!("[1/4] URL d'épisode fournie directement");
            url.to_string()
        } else {
            println!("[1/4] fetch_anime({url})");
            let anime = match scraper::fetch_anime(&http, url).await {
                Ok(a) => a,
                Err(e) => {
                    eprintln!("  ÉCHEC : {e:#}");
                    return 1;
                }
            };
            println!("  OK : « {} » — {} épisodes", anime.title, anime.episodes.len());
            let ep1 = &anime.episodes[0];
            println!("  ep[0] = #{} {} -> {}", ep1.number, ep1.name, ep1.url);
            ep1.url.clone()
        };

        println!("[2/4] fetch_players({episode_url})");
        let players = match scraper::fetch_players(&http, &episode_url).await {
            Ok(p) => p,
            Err(e) => {
                eprintln!("  ÉCHEC : {e:#}");
                return 1;
            }
        };
        for p in &players {
            println!("  - {} -> {}", p.name, p.iframe_url);
        }

        // On valide chaque extracteur HTTP supporté : resolve + extraction ffmpeg de 8 s.
        let to_test = ["LECTEUR myTV", "LECTEUR Stape", "LECTEUR FHD1"];
        let mut ok = 0;
        for (idx, want) in to_test.iter().enumerate() {
            let Some(player) = players.iter().find(|p| p.name.eq_ignore_ascii_case(want)) else {
                println!("[3/4] {want} : absent de cet épisode (ignoré)");
                continue;
            };
            println!("[3/4] resolve {want} -> {}", player.iframe_url);
            let sources = match extractors::resolve(&http, &player.iframe_url).await {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("  ÉCHEC : {e:#}");
                    continue;
                }
            };
            let src = &sources[0];
            println!("  OK : {} source(s), première = {}", sources.len(), src.url);

            let out = format!("/tmp/anime_dm_selftest_{idx}.mp4");
            let _ = std::fs::remove_file(&out);
            let mut headers = String::new();
            if let Some(r) = &src.referer {
                headers.push_str(&format!("Referer: {r}\r\n"));
            }
            if let Some(o) = &src.origin {
                headers.push_str(&format!("Origin: {o}\r\n"));
            }
            if let Some(c) = &src.cookie {
                headers.push_str(&format!("Cookie: {c}\r\n"));
            }
            println!("[4/4] ffmpeg 8 s -> {out}");
            let mut cmd = std::process::Command::new("ffmpeg");
            cmd.args(["-y", "-hide_banner", "-loglevel", "error", "-user_agent", net::UA])
                .args(["-headers", &headers])
                .args(["-i", &src.url, "-t", "8", "-c", "copy"]);
            if src.url.contains(".m3u8") {
                cmd.args(["-bsf:a", "aac_adtstoasc"]);
            }
            let status = cmd.arg(&out).status();
            match status {
                Ok(s) if s.success() => {
                    let size = std::fs::metadata(&out).map(|m| m.len()).unwrap_or(0);
                    println!("  OK : {} Ko", size / 1024);
                    if size > 0 {
                        ok += 1;
                    } else {
                        eprintln!("  ATTENTION : fichier vide");
                    }
                }
                Ok(s) => eprintln!("  ÉCHEC ffmpeg : {s}"),
                Err(e) => eprintln!("  ÉCHEC lancement ffmpeg : {e}"),
            }
        }

        // --- Étape headless : VOE / MOON / SB (sources générées en JS) ---
        println!("\n[headless] lancement de Chrome headless…");
        let hl = match headless::Headless::launch().await {
            Ok(h) => h,
            Err(e) => {
                eprintln!("  ÉCHEC lancement Chrome : {e:#}");
                return if ok > 0 { 0 } else { 1 };
            }
        };
        for want in ["LECTEUR VOE", "LECTEUR MOON", "LECTEUR SB"] {
            let Some(player) = players.iter().find(|p| p.name.eq_ignore_ascii_case(want)) else {
                println!("[headless] {want} : absent de cet épisode (ignoré)");
                continue;
            };
            println!("[headless] capture {want} -> {}", player.iframe_url);
            let src = match hl
                .capture(&player.iframe_url, std::time::Duration::from_secs(45))
                .await
            {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("  ÉCHEC : {e:#}");
                    continue;
                }
            };
            println!("  OK : {}  [referer={:?}]", src.url, src.referer);

            let out = format!("/tmp/anime_dm_selftest_{}.mp4", want.replace(' ', "_"));
            let _ = std::fs::remove_file(&out);
            let mut headers = String::new();
            if let Some(r) = &src.referer {
                headers.push_str(&format!("Referer: {r}\r\n"));
            }
            if let Some(o) = &src.origin {
                headers.push_str(&format!("Origin: {o}\r\n"));
            }
            if let Some(c) = &src.cookie {
                headers.push_str(&format!("Cookie: {c}\r\n"));
            }
            let status = std::process::Command::new("ffmpeg")
                .args(["-y", "-hide_banner", "-loglevel", "error", "-user_agent", net::UA])
                .args(["-headers", &headers])
                .args(["-i", &src.url, "-t", "8", "-c", "copy", &out])
                .status();
            match status {
                Ok(s) if s.success() => {
                    let size = std::fs::metadata(&out).map(|m| m.len()).unwrap_or(0);
                    println!("  OK ffmpeg : {} Ko", size / 1024);
                    if size > 0 {
                        ok += 1;
                    }
                }
                Ok(s) => eprintln!("  ÉCHEC ffmpeg : {s}"),
                Err(e) => eprintln!("  ÉCHEC lancement ffmpeg : {e}"),
            }
        }

        if ok > 0 {
            println!("\n✅ {ok} extracteur(s) validé(s) de bout en bout (HTTP + headless).");
            0
        } else {
            eprintln!("\n❌ Aucun extracteur n'a abouti.");
            1
        }
    })
}
