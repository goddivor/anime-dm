use std::collections::BTreeMap;
use std::path::{Path, PathBuf};
use std::process::Stdio;

use tokio::io::{AsyncBufReadExt, AsyncReadExt, BufReader};
use tokio::process::Command;

use super::net::{self, UA};

// Download `url` to `out` with aria2 (parallel, capped to `connections`), using
// ffmpeg only to remux/decrypt HLS. A direct file is fetched multi-connection;
// an `.m3u8` is split into segments fetched in parallel then muxed by ffmpeg.
pub async fn download<F, P>(
    url: String,
    headers: BTreeMap<String, String>,
    out: PathBuf,
    aria2: String,
    ffmpeg: String,
    connections: u8,
    on_progress: F,
    on_pid: P,
) -> Result<(), String>
where
    F: Fn(Option<f32>, Option<String>, Option<u64>) + Send,
    P: Fn(Option<u32>) + Send,
{
    if url.contains(".m3u8") {
        hls(&url, &headers, &out, &aria2, &ffmpeg, connections, &on_progress, &on_pid).await
    } else {
        direct(&url, &headers, &out, &aria2, connections, &on_progress, &on_pid).await
    }
}

// aria2's per-request header flags, with a User-Agent fallback.
fn header_args(headers: &BTreeMap<String, String>) -> Vec<String> {
    let mut out = Vec::new();
    let mut has_ua = false;
    for (k, v) in headers {
        if k.eq_ignore_ascii_case("user-agent") {
            has_ua = true;
            out.push(format!("--user-agent={v}"));
        } else {
            out.push(format!("--header={k}: {v}"));
        }
    }
    if !has_ua {
        out.push(format!("--user-agent={UA}"));
    }
    out
}

async fn direct<F, P>(
    url: &str,
    headers: &BTreeMap<String, String>,
    out: &Path,
    aria2: &str,
    connections: u8,
    on_progress: &F,
    on_pid: &P,
) -> Result<(), String>
where
    F: Fn(Option<f32>, Option<String>, Option<u64>),
    P: Fn(Option<u32>),
{
    let dir = out.parent().ok_or("chemin de sortie invalide")?;
    let name = out
        .file_name()
        .and_then(|n| n.to_str())
        .ok_or("nom de fichier invalide")?;

    let mut cmd = Command::new(aria2);
    cmd.arg(format!("--max-connection-per-server={connections}"))
        .arg(format!("--split={connections}"))
        .arg("--min-split-size=1M")
        .arg("--continue=true")
        .arg("--allow-overwrite=true")
        .arg("--auto-file-renaming=false")
        .arg("--summary-interval=1")
        .arg("--console-log-level=warn")
        .arg("--connect-timeout=15")
        .arg("--timeout=60")
        .arg("--max-tries=2")
        .arg("--retry-wait=2")
        .arg(format!("--dir={}", dir.to_string_lossy()))
        .arg(format!("--out={name}"));
    for a in header_args(headers) {
        cmd.arg(a);
    }
    cmd.arg(url)
        .kill_on_drop(true)
        .stdin(Stdio::null())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("aria2 non lançable : {e}"))?;
    on_pid(child.id());

    let pct_re = regex::Regex::new(r"\((\d+)%\)").unwrap();
    let dl_re = regex::Regex::new(r"DL:\s*([0-9.]+\s*[KMGTi]*B)").unwrap();
    if let Some(stdout) = child.stdout.take() {
        let mut reader = BufReader::new(stdout);
        let mut buf = Vec::new();
        loop {
            buf.clear();
            // aria2 separates progress updates with carriage returns.
            let n = reader.read_until(b'\r', &mut buf).await.unwrap_or(0);
            if n == 0 {
                break;
            }
            let line = String::from_utf8_lossy(&buf);
            let pct = pct_re
                .captures(&line)
                .and_then(|c| c[1].parse::<f32>().ok())
                .map(|p| p / 100.0);
            let speed = dl_re.captures(&line).map(|c| c[1].trim().to_string());
            if pct.is_some() || speed.is_some() {
                on_progress(pct, speed, None);
            }
        }
    }

    let status = child.wait().await.map_err(|e| e.to_string())?;
    on_pid(None);
    if status.success() {
        Ok(())
    } else {
        let mut err = String::new();
        if let Some(mut e) = child.stderr.take() {
            let _ = e.read_to_string(&mut err).await;
        }
        Err(format!("aria2 a échoué : {}", err.trim()))
    }
}

async fn hls<F, P>(
    url: &str,
    headers: &BTreeMap<String, String>,
    out: &Path,
    aria2: &str,
    ffmpeg: &str,
    connections: u8,
    on_progress: &F,
    on_pid: &P,
) -> Result<(), String>
where
    F: Fn(Option<f32>, Option<String>, Option<u64>),
    P: Fn(Option<u32>),
{
    let client = net::client().map_err(|e| e.to_string())?;
    let hmap = req_headers(headers);

    // Resolve a master playlist to its best media playlist.
    let (playlist_url, body) = resolve_media_playlist(&client, url, &hmap).await?;
    let segments = parse_segments(&playlist_url, &body);
    if segments.is_empty() {
        return Err("aucun segment dans la playlist HLS".to_string());
    }
    let key = parse_key(&playlist_url, &body);

    let work = out.with_extension("aria2-tmp");
    let _ = tokio::fs::remove_dir_all(&work).await;
    tokio::fs::create_dir_all(&work)
        .await
        .map_err(|e| e.to_string())?;

    // Fetch the AES-128 key locally so ffmpeg decrypts without needing headers.
    if let Some((kurl, _)) = &key {
        let bytes = client
            .get(kurl)
            .headers(hmap.clone())
            .send()
            .await
            .map_err(|e| e.to_string())?
            .bytes()
            .await
            .map_err(|e| e.to_string())?;
        tokio::fs::write(work.join("key.bin"), &bytes)
            .await
            .map_err(|e| e.to_string())?;
    }

    // Build the aria2 input file (one URL + per-entry out= name).
    let mut input = String::new();
    for (i, (seg_url, _)) in segments.iter().enumerate() {
        input.push_str(seg_url);
        input.push('\n');
        input.push_str(&format!("  dir={}\n", work.to_string_lossy()));
        input.push_str(&format!("  out=seg{i:05}.ts\n"));
    }
    let input_path = work.join("input.txt");
    tokio::fs::write(&input_path, &input)
        .await
        .map_err(|e| e.to_string())?;

    let mut cmd = Command::new(aria2);
    cmd.arg(format!("--input-file={}", input_path.to_string_lossy()))
        .arg(format!("--max-concurrent-downloads={connections}"))
        .arg("--max-connection-per-server=1")
        .arg("--continue=true")
        .arg("--allow-overwrite=true")
        .arg("--auto-file-renaming=false")
        .arg("--console-log-level=warn")
        .arg("--connect-timeout=15")
        .arg("--timeout=60")
        .arg("--max-tries=2")
        .arg("--retry-wait=2");
    for a in header_args(headers) {
        cmd.arg(a);
    }
    cmd.kill_on_drop(true)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("aria2 non lançable : {e}"))?;
    on_pid(child.id());

    let total = segments.len();
    let mut last_bytes = 0u64;
    let status = loop {
        tokio::time::sleep(std::time::Duration::from_millis(500)).await;
        let (done, bytes) = segment_stats(&work).await;
        // Bytes downloaded over the 0.5s window -> a MB/s speed string.
        let speed = format!("{:.1} MB/s", (bytes.saturating_sub(last_bytes)) as f64 / 1e6 / 0.5);
        last_bytes = bytes;
        // Segment fetch is ~95% of the work; the ffmpeg remux is the last 5%.
        on_progress(
            Some((done as f32 / total as f32) * 0.95),
            Some(speed),
            Some(bytes),
        );
        if let Ok(Some(s)) = child.try_wait() {
            break s;
        }
    };
    on_pid(None);
    if !status.success() {
        let mut err = String::new();
        if let Some(mut e) = child.stderr.take() {
            let _ = e.read_to_string(&mut err).await;
        }
        let _ = tokio::fs::remove_dir_all(&work).await;
        return Err(format!("aria2 (segments) a échoué : {}", err.trim()));
    }

    // Rewrite the playlist to point at the local segments + key, then remux.
    let local = rewrite_playlist(&body, key.as_ref());
    let local_path = work.join("index.m3u8");
    tokio::fs::write(&local_path, &local)
        .await
        .map_err(|e| e.to_string())?;

    let mut fc = Command::new(ffmpeg);
    fc.arg("-y")
        .arg("-hide_banner")
        .arg("-allowed_extensions")
        .arg("ALL")
        .arg("-protocol_whitelist")
        .arg("file,crypto,data")
        .arg("-i")
        .arg(&local_path)
        .arg("-c")
        .arg("copy")
        .arg(out)
        .kill_on_drop(true)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::piped());
    let mut fch = fc.spawn().map_err(|e| format!("ffmpeg non lançable : {e}"))?;
    on_pid(fch.id());
    let fstatus = fch.wait().await.map_err(|e| e.to_string())?;
    on_pid(None);
    let final_size = tokio::fs::metadata(out).await.ok().map(|m| m.len());
    on_progress(Some(1.0), None, final_size);

    let mut ferr = String::new();
    if let Some(mut e) = fch.stderr.take() {
        let _ = e.read_to_string(&mut ferr).await;
    }
    let _ = tokio::fs::remove_dir_all(&work).await;
    if fstatus.success() {
        Ok(())
    } else {
        Err(format!("remux ffmpeg a échoué : {}", ferr.trim()))
    }
}

fn req_headers(headers: &BTreeMap<String, String>) -> reqwest::header::HeaderMap {
    let mut map = reqwest::header::HeaderMap::new();
    for (k, v) in headers {
        if let (Ok(name), Ok(val)) = (
            reqwest::header::HeaderName::from_bytes(k.as_bytes()),
            reqwest::header::HeaderValue::from_str(v),
        ) {
            map.insert(name, val);
        }
    }
    map
}

// Count of fully downloaded segments (a `.ts` with no `.aria2` control file)
// and the total bytes currently on disk across all `.ts` segments.
async fn segment_stats(work: &Path) -> (usize, u64) {
    let Ok(mut rd) = tokio::fs::read_dir(work).await else {
        return (0, 0);
    };
    let mut done = 0;
    let mut bytes = 0u64;
    while let Ok(Some(entry)) = rd.next_entry().await {
        let name = entry.file_name();
        let name = name.to_string_lossy();
        if name.starts_with("seg") && name.ends_with(".ts") {
            if let Ok(m) = entry.metadata().await {
                bytes += m.len();
            }
            if !work.join(format!("{name}.aria2")).exists() {
                done += 1;
            }
        }
    }
    (done, bytes)
}

// If `text` is a master playlist, follow its highest-bandwidth variant.
async fn resolve_media_playlist(
    client: &reqwest::Client,
    url: &str,
    hmap: &reqwest::header::HeaderMap,
) -> Result<(String, String), String> {
    let body = client
        .get(url)
        .headers(hmap.clone())
        .send()
        .await
        .map_err(|e| e.to_string())?
        .text()
        .await
        .map_err(|e| e.to_string())?;
    if !body.contains("#EXT-X-STREAM-INF") {
        return Ok((url.to_string(), body));
    }
    let mut best: Option<(u64, String)> = None;
    let lines: Vec<&str> = body.lines().collect();
    for (i, line) in lines.iter().enumerate() {
        if line.starts_with("#EXT-X-STREAM-INF") {
            let bw = line
                .split("BANDWIDTH=")
                .nth(1)
                .and_then(|s| s.split([',', ' ']).next())
                .and_then(|s| s.parse::<u64>().ok())
                .unwrap_or(0);
            if let Some(uri) = lines.get(i + 1) {
                let uri = uri.trim();
                if !uri.is_empty() && !uri.starts_with('#') {
                    if best.as_ref().map(|(b, _)| bw >= *b).unwrap_or(true) {
                        best = Some((bw, resolve(url, uri)));
                    }
                }
            }
        }
    }
    let media = best.ok_or("aucune variante dans le master HLS")?.1;
    let body = client
        .get(&media)
        .headers(hmap.clone())
        .send()
        .await
        .map_err(|e| e.to_string())?
        .text()
        .await
        .map_err(|e| e.to_string())?;
    Ok((media, body))
}

// Absolute segment URLs (in order) from a media playlist.
fn parse_segments(playlist_url: &str, body: &str) -> Vec<(String, String)> {
    body.lines()
        .map(str::trim)
        .filter(|l| !l.is_empty() && !l.starts_with('#'))
        .map(|uri| (resolve(playlist_url, uri), uri.to_string()))
        .collect()
}

// The AES-128 key URL from an EXT-X-KEY line, if the stream is encrypted.
fn parse_key(playlist_url: &str, body: &str) -> Option<(String, String)> {
    let line = body
        .lines()
        .find(|l| l.starts_with("#EXT-X-KEY") && l.contains("METHOD=AES-128"))?;
    let uri = line.split("URI=\"").nth(1)?.split('"').next()?;
    Some((resolve(playlist_url, uri), line.to_string()))
}

// Rewrite a media playlist so every segment + the key point at local files.
fn rewrite_playlist(body: &str, key: Option<&(String, String)>) -> String {
    let mut out = String::new();
    let mut idx = 0;
    for line in body.lines() {
        let trimmed = line.trim();
        if trimmed.starts_with("#EXT-X-KEY") && key.is_some() {
            if let Some(start) = line.find("URI=\"") {
                let rest = &line[start + 5..];
                if let Some(end) = rest.find('"') {
                    out.push_str(&line[..start + 5]);
                    out.push_str("key.bin");
                    out.push_str(&rest[end..]);
                    out.push('\n');
                    continue;
                }
            }
            out.push_str(line);
            out.push('\n');
        } else if !trimmed.is_empty() && !trimmed.starts_with('#') {
            out.push_str(&format!("seg{idx:05}.ts\n"));
            idx += 1;
        } else {
            out.push_str(line);
            out.push('\n');
        }
    }
    out
}

// Resolve a possibly-relative URI against the playlist URL.
fn resolve(base: &str, uri: &str) -> String {
    if uri.starts_with("http://") || uri.starts_with("https://") {
        return uri.to_string();
    }
    if let Some(rest) = uri.strip_prefix("//") {
        let scheme = base.split("://").next().unwrap_or("https");
        return format!("{scheme}://{rest}");
    }
    let origin = {
        let mut it = base.splitn(4, '/');
        let scheme = it.next().unwrap_or("https:");
        it.next();
        let host = it.next().unwrap_or("");
        format!("{scheme}//{host}")
    };
    if uri.starts_with('/') {
        return format!("{origin}{uri}");
    }
    let dir = base
        .split('?')
        .next()
        .unwrap_or(base)
        .rsplit_once('/')
        .map(|(d, _)| d)
        .unwrap_or(&origin);
    format!("{dir}/{uri}")
}
