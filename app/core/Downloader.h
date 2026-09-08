#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include "core/Download.h"

class AddonStore;
class Http;

// Runs the downloads the way IDM does: a video is fetched over several HTTP
// connections at once, each writing its own part in a temporary folder; the
// largest remaining part is halved whenever a connection frees up; the
// positions are saved several times a minute so a stop resumes in place; the
// parts are assembled into the final file at the end.
//
// A playlist (HLS) is treated the same way, its segments being the parts.
//
// Every report goes to the attached window as a posted message whose LPARAM
// owns a DownloadEvent the receiver must delete.
class Downloader {
public:
    Downloader(Http& http, const AddonStore& store);
    ~Downloader();

    Downloader(const Downloader&) = delete;
    Downloader& operator=(const Downloader&) = delete;

    void Attach(HWND window, UINT message);

    // How many connections one video may open, and how many videos run at once.
    static constexpr int kConnections = 8;
    static constexpr int kMaxRunning = 3;

    // Queues an item; it starts as soon as a slot frees up.
    void Start(const DownloadTask& task);

    // Stops an item and keeps its parts, so a later Start resumes it.
    void Pause(uint64_t id);

    // Stops an item and drops its parts.
    void Cancel(uint64_t id);

    void PauseAll();
    void CancelAll();

    // Whether the item is queued or running.
    bool Holds(uint64_t id);

private:
    struct Job;

    void Pump();
    void Run(std::shared_ptr<Job> job);
    void Reap();
    void Post(const DownloadEvent& event) const;
    void Settle(std::shared_ptr<Job> job);

    Http& http_;
    const AddonStore& store_;
    std::atomic<HWND> window_{nullptr};
    std::atomic<UINT> message_{0};

    std::mutex mutex_;
    std::deque<DownloadTask> pending_;
    std::map<uint64_t, std::shared_ptr<Job>> running_;
    std::vector<std::thread> finished_;
};
