use std::path::Path;

use rusqlite::{params, Connection};
use serde::{Deserialize, Serialize};

/// A persisted download row (mirrors the frontend DownloadRow, minus transient fields).
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct DownloadRecord {
    pub id: u64,
    pub addon_id: String,
    pub anime_id: u64,
    pub anime_title: String,
    pub episode_number: f32,
    pub filename: String,
    pub page_url: String,
    pub queue: String,
    pub status: String,
    #[serde(default)]
    pub size_bytes: Option<u64>,
    pub added_at: i64,
    #[serde(default)]
    pub last_try: Option<i64>,
    pub out_path: String,
    #[serde(default)]
    pub address: Option<String>,
    #[serde(default)]
    pub error: Option<String>,
    #[serde(default)]
    pub is_movie: bool,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct GroupRecord {
    pub id: u64,
    pub title: String,
    pub url: String,
    #[serde(default)]
    pub poster_url: Option<String>,
    #[serde(default)]
    pub poster_data: Option<String>,
    #[serde(default)]
    pub icon_template: Option<String>,
    pub expanded: bool,
}

pub fn open(path: &Path) -> rusqlite::Result<Connection> {
    let conn = Connection::open(path)?;
    conn.execute_batch(
        "CREATE TABLE IF NOT EXISTS anime_groups (
            id INTEGER PRIMARY KEY,
            title TEXT NOT NULL,
            url TEXT NOT NULL,
            poster_url TEXT,
            poster_data TEXT,
            icon_template TEXT,
            expanded INTEGER NOT NULL DEFAULT 1
        );
        CREATE TABLE IF NOT EXISTS downloads (
            id INTEGER PRIMARY KEY,
            addon_id TEXT NOT NULL,
            anime_id INTEGER NOT NULL,
            anime_title TEXT NOT NULL,
            episode_number REAL NOT NULL,
            filename TEXT NOT NULL,
            page_url TEXT NOT NULL,
            queue TEXT NOT NULL,
            status TEXT NOT NULL,
            size_bytes INTEGER,
            added_at INTEGER NOT NULL,
            last_try INTEGER,
            out_path TEXT NOT NULL,
            address TEXT,
            error TEXT,
            is_movie INTEGER
        );",
    )?;
    // Migrations for databases created before these columns existed.
    let _ = conn.execute("ALTER TABLE anime_groups ADD COLUMN poster_data TEXT", []);
    let _ = conn.execute("ALTER TABLE anime_groups ADD COLUMN icon_template TEXT", []);
    let _ = conn.execute("ALTER TABLE downloads ADD COLUMN is_movie INTEGER", []);
    Ok(conn)
}

pub fn load_downloads(conn: &Connection) -> rusqlite::Result<Vec<DownloadRecord>> {
    let mut stmt = conn.prepare(
        "SELECT id, addon_id, anime_id, anime_title, episode_number, filename, page_url, queue,
                status, size_bytes, added_at, last_try, out_path, address, error, is_movie
         FROM downloads ORDER BY id",
    )?;
    let rows = stmt
        .query_map([], |r| {
            Ok(DownloadRecord {
                id: r.get(0)?,
                addon_id: r.get(1)?,
                anime_id: r.get(2)?,
                anime_title: r.get(3)?,
                episode_number: r.get(4)?,
                filename: r.get(5)?,
                page_url: r.get(6)?,
                queue: r.get(7)?,
                status: r.get(8)?,
                size_bytes: r.get(9)?,
                added_at: r.get(10)?,
                last_try: r.get(11)?,
                out_path: r.get(12)?,
                address: r.get(13)?,
                error: r.get(14)?,
                is_movie: r.get::<_, Option<i64>>(15)?.unwrap_or(0) != 0,
            })
        })?
        .collect::<rusqlite::Result<Vec<_>>>()?;
    Ok(rows)
}

pub fn upsert_download(conn: &Connection, d: &DownloadRecord) -> rusqlite::Result<()> {
    conn.execute(
        "INSERT OR REPLACE INTO downloads
            (id, addon_id, anime_id, anime_title, episode_number, filename, page_url, queue,
             status, size_bytes, added_at, last_try, out_path, address, error, is_movie)
         VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16)",
        params![
            d.id,
            d.addon_id,
            d.anime_id,
            d.anime_title,
            d.episode_number,
            d.filename,
            d.page_url,
            d.queue,
            d.status,
            d.size_bytes,
            d.added_at,
            d.last_try,
            d.out_path,
            d.address,
            d.error,
            d.is_movie as i64,
        ],
    )?;
    Ok(())
}

pub fn delete_downloads(conn: &Connection, ids: &[u64]) -> rusqlite::Result<()> {
    for id in ids {
        conn.execute("DELETE FROM downloads WHERE id = ?1", params![id])?;
    }
    Ok(())
}

pub fn clear_downloads(conn: &Connection) -> rusqlite::Result<()> {
    conn.execute_batch("DELETE FROM downloads; DELETE FROM anime_groups;")?;
    Ok(())
}

pub fn load_groups(conn: &Connection) -> rusqlite::Result<Vec<GroupRecord>> {
    let mut stmt = conn.prepare(
        "SELECT id, title, url, poster_url, poster_data, icon_template, expanded
         FROM anime_groups ORDER BY id",
    )?;
    let rows = stmt
        .query_map([], |r| {
            Ok(GroupRecord {
                id: r.get(0)?,
                title: r.get(1)?,
                url: r.get(2)?,
                poster_url: r.get(3)?,
                poster_data: r.get(4)?,
                icon_template: r.get(5)?,
                expanded: r.get::<_, i64>(6)? != 0,
            })
        })?
        .collect::<rusqlite::Result<Vec<_>>>()?;
    Ok(rows)
}

pub fn upsert_group(conn: &Connection, g: &GroupRecord) -> rusqlite::Result<()> {
    conn.execute(
        "INSERT OR REPLACE INTO anime_groups
             (id, title, url, poster_url, poster_data, icon_template, expanded)
         VALUES (?1,?2,?3,?4,?5,?6,?7)",
        params![
            g.id,
            g.title,
            g.url,
            g.poster_url,
            g.poster_data,
            g.icon_template,
            g.expanded as i64
        ],
    )?;
    Ok(())
}

/// Delete a single anime group by id.
pub fn delete_group(conn: &Connection, id: i64) -> rusqlite::Result<()> {
    conn.execute("DELETE FROM anime_groups WHERE id = ?1", params![id])?;
    Ok(())
}

/// Drop groups that no download references anymore.
pub fn prune_groups(conn: &Connection) -> rusqlite::Result<()> {
    conn.execute(
        "DELETE FROM anime_groups WHERE id NOT IN (SELECT DISTINCT anime_id FROM downloads)",
        [],
    )?;
    Ok(())
}
