#pragma once

#include <cstdint>
#include <ctime>
#include <string>

// Where a download stands in its life.
enum class DownloadStatus {
    Queued,       // waiting for a slot
    Resolving,    // asking the source for a video
    Downloading,  // transferring the parts
    Assembling,   // joining the parts into the final file
    Completed,
    Failed,
    Stopped,      // paused by the user, parts kept on disk
};

// Why a download failed, for the interface to translate.
enum class DownloadError {
    None,
    Source,    // the source library could not be loaded or answered an error
    NoPlayer,  // the episode lists no player
    NoVideo,   // no player yielded a video that could be fetched
    Network,   // a transfer kept failing
    Playlist,  // the HLS playlist could not be read
    Key,       // the HLS key could not be fetched or applied
    Disk,      // a file could not be written
};

// One episode of the queue, as the interface keeps and persists it.
struct DownloadItem {
    uint64_t id = 0;
    std::string addonId;
    std::string animeTitle;
    std::string animeUrl;
    double episodeNumber = 0.0;
    std::string pageUrl;
    std::string player;  // empty lets the source decide
    bool movie = false;  // a film rather than a numbered episode
    std::wstring outPath;
    DownloadStatus status = DownloadStatus::Queued;
    uint64_t done = 0;
    uint64_t total = 0;      // zero when unknown
    double fraction = -1.0;  // -1 when unknown, otherwise 0..1
    double speed = 0.0;      // bytes per second, only while downloading
    std::string address;     // host the video comes from
    DownloadError error = DownloadError::None;
    std::string detail;  // what the engine adds to the error, when it can
    std::time_t addedAt = 0;
    std::time_t lastTry = 0;
};

// One anime of the queue, as the categories panel shows it.
struct AnimeGroup {
    std::string url;  // the page of the anime, which identifies it
    std::string title;
    std::string posterUrl;
    bool expanded = true;
};

// What the engine needs to run one item.
struct DownloadTask {
    uint64_t id = 0;
    std::string addonId;
    std::string pageUrl;
    std::string player;
    std::wstring outPath;
};

// What the engine reports back, posted to the interface thread.
struct DownloadEvent {
    uint64_t id = 0;
    DownloadStatus status = DownloadStatus::Queued;
    uint64_t done = 0;
    uint64_t total = 0;
    double fraction = -1.0;
    double speed = 0.0;
    std::string address;
    DownloadError error = DownloadError::None;
    std::string detail;
    std::wstring outPath;  // set when the engine settled on another name
};

// The file name of an item, without its folder.
std::wstring FileNameOf(const std::wstring& path);

// The number of an episode as it appears in names: 001, 012, 12.5.
std::wstring EpisodeLabel(double number);

// Turns any text into a name Windows accepts: illegal characters replaced,
// runs of whitespace (line breaks included) collapsed, ends trimmed.
std::wstring SafeFileName(const std::wstring& text);

// Applies SafeFileName to every segment of a path but its drive.
std::wstring SafePath(const std::wstring& path);

// Whether the engine still has, or may have, a hand on an item.
inline bool IsActive(DownloadStatus status) {
    return status == DownloadStatus::Queued || status == DownloadStatus::Resolving ||
           status == DownloadStatus::Downloading || status == DownloadStatus::Assembling;
}
