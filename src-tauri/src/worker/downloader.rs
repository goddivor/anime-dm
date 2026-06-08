use std::path::PathBuf;
use std::process::Stdio;

use regex::Regex;
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;

use super::net::UA;
use crate::model::VideoSource;

pub async fn download<F>(source: VideoSource, out: PathBuf, on_progress: F) -> Result<(), String>
where
    F: Fn(Option<f32>, Option<String>) + Send,
{
    let mut last = String::new();
    for attempt in 1..=2 {
        match run_ffmpeg(&source, &out, &on_progress).await {
            Ok(()) => return Ok(()),
            Err(e) => {
                last = e;
                if attempt < 2 {
                    tokio::time::sleep(std::time::Duration::from_millis(1200)).await;
                }
            }
        }
    }
    Err(last)
}

async fn run_ffmpeg<F>(source: &VideoSource, out: &PathBuf, on_progress: &F) -> Result<(), String>
where
    F: Fn(Option<f32>, Option<String>),
{
    let mut headers = String::new();
    if let Some(r) = &source.referer {
        headers.push_str(&format!("Referer: {r}\r\n"));
    }
    if let Some(o) = &source.origin {
        headers.push_str(&format!("Origin: {o}\r\n"));
    }
    if let Some(c) = &source.cookie {
        headers.push_str(&format!("Cookie: {c}\r\n"));
    }

    let mut cmd = Command::new("ffmpeg");
    cmd.arg("-y").arg("-hide_banner").arg("-user_agent").arg(UA);
    if !headers.is_empty() {
        cmd.arg("-headers").arg(&headers);
    }
    cmd.arg("-i").arg(&source.url).arg("-c").arg("copy");
    if source.url.contains(".m3u8") {
        cmd.arg("-bsf:a").arg("aac_adtstoasc");
    }
    cmd.arg(out)
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::piped());

    let mut child = cmd
        .spawn()
        .map_err(|e| format!("ffmpeg introuvable ou non lançable : {e}"))?;

    let stderr = child.stderr.take().expect("stderr piped");
    let mut reader = BufReader::new(stderr);

    let dur_re = Regex::new(r"Duration:\s*(\d+):(\d+):(\d+(?:\.\d+)?)").unwrap();
    let time_re = Regex::new(r"time=\s*(\d+):(\d+):(\d+(?:\.\d+)?)").unwrap();
    let speed_re = Regex::new(r"speed=\s*([0-9.]+)x").unwrap();

    let mut total: f32 = 0.0;
    let mut tail = String::new();
    let mut buf: Vec<u8> = Vec::new();

    loop {
        buf.clear();
        let n = reader.read_until(b'\r', &mut buf).await.unwrap_or(0);
        if n == 0 {
            break;
        }
        let chunk = String::from_utf8_lossy(&buf);

        tail.push_str(&chunk);
        if tail.len() > 4000 {
            let cut = tail.len() - 4000;
            tail.drain(0..cut);
        }

        if total <= 0.0 {
            if let Some(c) = dur_re.captures(&chunk) {
                total = hms(&c);
            }
        }

        let progress = time_re.captures(&chunk).and_then(|c| {
            let t = hms(&c);
            (total > 0.0).then(|| (t / total).clamp(0.0, 1.0))
        });
        let speed = speed_re.captures(&chunk).map(|c| format!("{}x", &c[1]));

        if progress.is_some() || speed.is_some() {
            on_progress(progress, speed);
        }
    }

    let status = child
        .wait()
        .await
        .map_err(|e| format!("attente du process ffmpeg : {e}"))?;

    if status.success() {
        Ok(())
    } else {
        let detail = tail
            .lines()
            .rev()
            .take(6)
            .collect::<Vec<_>>()
            .into_iter()
            .rev()
            .collect::<Vec<_>>()
            .join("\n");
        Err(format!("ffmpeg a échoué :\n{detail}"))
    }
}

fn hms(c: &regex::Captures) -> f32 {
    let h: f32 = c[1].parse().unwrap_or(0.0);
    let m: f32 = c[2].parse().unwrap_or(0.0);
    let s: f32 = c[3].parse().unwrap_or(0.0);
    h * 3600.0 + m * 60.0 + s
}
