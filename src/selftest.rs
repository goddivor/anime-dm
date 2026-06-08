pub fn run() -> i32 {
    let url = std::env::args()
        .skip_while(|a| a != "--selftest")
        .nth(1)
        .filter(|a| a.starts_with("http"))
        .unwrap_or_else(|| "https://voir-anime.to/anime/dragon-ball-vf/".to_string());
    let url = url.as_str();
    let rt = tokio::runtime::Runtime::new().expect("tokio runtime");
    rt.block_on(async {
        let http = crate::worker::net::client().unwrap();

        let is_episode = url.trim_end_matches('/').split('/').count() >= 6;
        let episode_url = if is_episode {
            println!("[1/4] Episode URL provided directly");
            url.to_string()
        } else {
            println!("[1/4] fetch_anime({url})");
            let anime = match crate::worker::scraper::fetch_anime(&http, url).await {
                Ok(a) => a,
                Err(e) => {
                    eprintln!("  FAILED: {e:#}");
                    return 1;
                }
            };
            println!("  OK: {} — {} episodes", anime.title, anime.episodes.len());
            let ep1 = &anime.episodes[0];
            println!("  ep[0] = #{} {} -> {}", ep1.number, ep1.name, ep1.url);
            ep1.url.clone()
        };

        println!("[2/4] fetch_players({episode_url})");
        let players = match crate::worker::scraper::fetch_players(&http, &episode_url).await {
            Ok(p) => p,
            Err(e) => {
                eprintln!("  FAILED: {e:#}");
                return 1;
            }
        };
        for p in &players {
            println!("  - {} -> {}", p.name, p.iframe_url);
        }

        let to_test = ["LECTEUR myTV", "LECTEUR Stape", "LECTEUR FHD1"];
        let mut ok = 0;
        for (idx, want) in to_test.iter().enumerate() {
            let Some(player) = players.iter().find(|p| p.name.eq_ignore_ascii_case(want)) else {
                println!("[3/4] {want}: not on this episode (skipped)");
                continue;
            };
            println!("[3/4] resolve {want} -> {}", player.iframe_url);
            let sources = match crate::worker::extractors::resolve(&http, &player.iframe_url).await
            {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("  FAILED: {e:#}");
                    continue;
                }
            };
            let src = &sources[0];
            println!("  OK: {} source(s), first = {}", sources.len(), src.url);

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
            println!("[4/4] ffmpeg 8s -> {out}");
            let mut cmd = std::process::Command::new("ffmpeg");
            cmd.args([
                "-y",
                "-hide_banner",
                "-loglevel",
                "error",
                "-user_agent",
                crate::worker::net::UA,
            ])
            .args(["-headers", &headers])
            .args(["-i", &src.url, "-t", "8", "-c", "copy"]);
            if src.url.contains(".m3u8") {
                cmd.args(["-bsf:a", "aac_adtstoasc"]);
            }
            let status = cmd.arg(&out).status();
            match status {
                Ok(s) if s.success() => {
                    let size = std::fs::metadata(&out).map(|m| m.len()).unwrap_or(0);
                    println!("  OK: {} KB", size / 1024);
                    if size > 0 {
                        ok += 1;
                    } else {
                        eprintln!("  WARNING: empty file");
                    }
                }
                Ok(s) => eprintln!("  ffmpeg FAILED: {s}"),
                Err(e) => eprintln!("  ffmpeg launch FAILED: {e}"),
            }
        }

        println!("\n[headless] launching headless Chrome…");
        let hl = match crate::worker::headless::Headless::launch().await {
            Ok(h) => h,
            Err(e) => {
                eprintln!("  Chrome launch FAILED: {e:#}");
                return if ok > 0 { 0 } else { 1 };
            }
        };
        for want in ["LECTEUR VOE", "LECTEUR MOON", "LECTEUR SB"] {
            let Some(player) = players.iter().find(|p| p.name.eq_ignore_ascii_case(want)) else {
                println!("[headless] {want}: not on this episode (skipped)");
                continue;
            };
            println!("[headless] capture {want} -> {}", player.iframe_url);
            let src = match hl
                .capture(&player.iframe_url, std::time::Duration::from_secs(45))
                .await
            {
                Ok(s) => s,
                Err(e) => {
                    eprintln!("  FAILED: {e:#}");
                    continue;
                }
            };
            println!("  OK: {}  [referer={:?}]", src.url, src.referer);

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
                .args([
                    "-y",
                    "-hide_banner",
                    "-loglevel",
                    "error",
                    "-user_agent",
                    crate::worker::net::UA,
                ])
                .args(["-headers", &headers])
                .args(["-i", &src.url, "-t", "8", "-c", "copy", &out])
                .status();
            match status {
                Ok(s) if s.success() => {
                    let size = std::fs::metadata(&out).map(|m| m.len()).unwrap_or(0);
                    println!("  ffmpeg OK: {} KB", size / 1024);
                    if size > 0 {
                        ok += 1;
                    }
                }
                Ok(s) => eprintln!("  ffmpeg FAILED: {s}"),
                Err(e) => eprintln!("  ffmpeg launch FAILED: {e}"),
            }
        }

        if ok > 0 {
            println!("\n✅ {ok} extractor(s) verified end-to-end (HTTP + headless).");
            0
        } else {
            eprintln!("\n❌ No extractor succeeded.");
            1
        }
    })
}
