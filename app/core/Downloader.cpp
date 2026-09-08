#include "core/Downloader.h"

#include <algorithm>

#include "core/Addon.h"
#include "core/AddonStore.h"
#include "core/Http.h"
#include "core/Paths.h"
#include "core/Transfer.h"

// One item the engine holds, with the flags its threads watch.
struct Downloader::Job {
    DownloadTask task;
    std::atomic<bool> stop{false};
    std::atomic<bool> discard{false};
    std::thread thread;
};

namespace {

// Compares two player names the way the sources spell them.
bool SameName(const std::string& a, const std::string& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

// The host of a URL, for the address column.
std::string HostOf(const std::string& url) {
    size_t scheme = url.find("://");
    size_t start = scheme == std::string::npos ? 0 : scheme + 3;
    size_t end = url.find('/', start);
    return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
}

// Whether a video URL names a playlist by its path.
bool IsPlaylistUrl(const std::string& url) {
    size_t query = url.find('?');
    std::string path = url.substr(0, query);
    return path.size() >= 5 && path.compare(path.size() - 5, 5, ".m3u8") == 0;
}

// Reads the videos a source returned into transfer sources, direct files
// first because they are the ones byte ranges accelerate.
std::vector<TransferSource> SourcesOf(const nlohmann::json& videos) {
    std::vector<TransferSource> sources;
    if (!videos.is_array()) {
        return sources;
    }
    for (const nlohmann::json& video : videos) {
        TransferSource source;
        source.url = video.value("url", std::string());
        if (source.url.empty()) {
            continue;
        }
        const nlohmann::json& headers = video.value("headers", nlohmann::json::object());
        for (auto it = headers.begin(); it != headers.end(); ++it) {
            if (it.value().is_string()) {
                source.headers[it.key()] = it.value().get<std::string>();
            }
        }
        sources.push_back(std::move(source));
    }
    std::stable_partition(sources.begin(), sources.end(),
                          [](const TransferSource& s) { return !IsPlaylistUrl(s.url); });
    return sources;
}

// Swaps the extension of a path for the one the transfer settled on.
std::wstring WithExtension(const std::wstring& path, const std::wstring& extension) {
    if (extension.empty()) {
        return path;
    }
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) {
        return path + extension;
    }
    return path.substr(0, dot) + extension;
}

}  // namespace

Downloader::Downloader(Http& http, const AddonStore& store) : http_(http), store_(store) {}

// Stops every transfer, keeping its parts, and waits for the threads.
Downloader::~Downloader() {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        window_ = nullptr;
        pending_.clear();
        for (auto& [id, job] : running_) {
            job->stop = true;
            jobs.push_back(job);
        }
    }
    for (std::shared_ptr<Job>& job : jobs) {
        if (job->thread.joinable()) {
            job->thread.join();
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    running_.clear();
    for (std::thread& thread : finished_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    finished_.clear();
}

// Names the window that receives the events.
void Downloader::Attach(HWND window, UINT message) {
    message_ = message;
    window_ = window;
}

// Queues an item; it starts as soon as a slot frees up.
void Downloader::Start(const DownloadTask& task) {
    Reap();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                      [&](const DownloadTask& t) { return t.id == task.id; }),
                       pending_.end());
        pending_.push_back(task);
    }
    Pump();
}

// Stops an item and keeps its parts.
void Downloader::Pause(uint64_t id) {
    Reap();
    std::lock_guard<std::mutex> lock(mutex_);
    auto running = running_.find(id);
    if (running != running_.end()) {
        running->second->stop = true;
        return;
    }
    auto pending = std::find_if(pending_.begin(), pending_.end(),
                                [&](const DownloadTask& t) { return t.id == id; });
    if (pending != pending_.end()) {
        pending_.erase(pending);
        DownloadEvent event;
        event.id = id;
        event.status = DownloadStatus::Stopped;
        Post(event);
    }
}

// Stops an item and drops its parts.
void Downloader::Cancel(uint64_t id) {
    Reap();
    std::lock_guard<std::mutex> lock(mutex_);
    auto running = running_.find(id);
    if (running != running_.end()) {
        running->second->discard = true;
        running->second->stop = true;
        return;
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [&](const DownloadTask& t) { return t.id == id; }),
                   pending_.end());
    paths::RemoveTree(paths::PartsDir(id));
}

void Downloader::PauseAll() {
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const DownloadTask& task : pending_) {
            ids.push_back(task.id);
        }
        for (auto& [id, job] : running_) {
            ids.push_back(id);
        }
    }
    for (uint64_t id : ids) {
        Pause(id);
    }
}

void Downloader::CancelAll() {
    std::vector<uint64_t> ids;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const DownloadTask& task : pending_) {
            ids.push_back(task.id);
        }
        for (auto& [id, job] : running_) {
            ids.push_back(id);
        }
    }
    for (uint64_t id : ids) {
        Cancel(id);
    }
}

// Whether the item is queued or running.
bool Downloader::Holds(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.count(id) > 0) {
        return true;
    }
    return std::any_of(pending_.begin(), pending_.end(),
                       [&](const DownloadTask& t) { return t.id == id; });
}

// Launches queued items while slots remain.
void Downloader::Pump() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t slots = static_cast<size_t>(kMaxRunning);
    for (auto it = pending_.begin(); it != pending_.end() && running_.size() < slots;) {
        if (running_.count(it->id) > 0) {
            ++it;
            continue;
        }
        auto job = std::make_shared<Job>();
        job->task = *it;
        it = pending_.erase(it);
        running_[job->task.id] = job;
        job->thread = std::thread([this, job] { Run(job); });
    }
}

// Joins the threads of the items that finished.
void Downloader::Reap() {
    std::vector<std::thread> done;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done.swap(finished_);
    }
    for (std::thread& thread : done) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

// Hands an event to the interface thread, which owns it from then on.
void Downloader::Post(const DownloadEvent& event) const {
    HWND window = window_.load();
    if (window == nullptr) {
        return;
    }
    auto* copy = new DownloadEvent(event);
    if (!PostMessageW(window, message_.load(), 0, reinterpret_cast<LPARAM>(copy))) {
        delete copy;
    }
}

// Takes an item out of the running set and fills the freed slot.
void Downloader::Settle(std::shared_ptr<Job> job) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_.erase(job->task.id);
        finished_.push_back(std::move(job->thread));
    }
    Pump();
}

// Resolves the episode through its source, then fetches and assembles it.
void Downloader::Run(std::shared_ptr<Job> job) {
    const DownloadTask& task = job->task;
    DownloadEvent event;
    event.id = task.id;

    auto fail = [&](DownloadError error, const std::string& detail) {
        event.status = DownloadStatus::Failed;
        event.error = error;
        event.detail = detail;
        Post(event);
        Settle(job);
    };
    auto stopped = [&]() {
        if (job->discard) {
            paths::RemoveTree(paths::PartsDir(task.id));
        }
        event.status = DownloadStatus::Stopped;
        event.speed = 0.0;
        Post(event);
        Settle(job);
    };

    event.status = DownloadStatus::Resolving;
    Post(event);

    std::unique_ptr<Addon> addon =
        Addon::Load(store_.LibraryPath(task.addonId), http_, store_.ReadConfig(task.addonId));
    if (!addon) {
        fail(DownloadError::Source, Addon::LastError());
        return;
    }

    std::string error;
    std::optional<nlohmann::json> hosters =
        addon->Call("adm_hoster_list", {{"url", task.pageUrl}}, &error);
    if (!hosters || !hosters->is_array()) {
        fail(DownloadError::Source, error);
        return;
    }
    if (hosters->empty()) {
        fail(DownloadError::NoPlayer, std::string());
        return;
    }

    std::vector<nlohmann::json> order(hosters->begin(), hosters->end());
    if (!task.player.empty()) {
        std::stable_partition(order.begin(), order.end(), [&](const nlohmann::json& hoster) {
            return SameName(hoster.value("name", std::string()), task.player);
        });
    }

    std::wstring partsDir = paths::PartsDir(task.id);
    if (partsDir.empty()) {
        fail(DownloadError::Disk, std::string());
        return;
    }

    TransferControl control{job->stop, job->discard};
    std::unique_ptr<Transfer> transfer;
    std::string address;
    for (const nlohmann::json& hoster : order) {
        if (job->stop) {
            stopped();
            return;
        }
        std::optional<nlohmann::json> videos = addon->Call("adm_video_list", hoster);
        if (!videos) {
            continue;
        }
        for (const TransferSource& source : SourcesOf(*videos)) {
            if (job->stop) {
                stopped();
                return;
            }
            transfer = OpenTransfer(http_, source, partsDir, control);
            if (transfer) {
                address = HostOf(source.url);
                break;
            }
        }
        if (transfer) {
            break;
        }
    }
    if (!transfer) {
        fail(DownloadError::NoVideo, std::string());
        return;
    }

    event.status = DownloadStatus::Downloading;
    event.address = address;
    event.total = transfer->Total();
    event.outPath = WithExtension(task.outPath, transfer->Extension());
    Post(event);

    bool fetched = transfer->Run(
        kConnections, [&](uint64_t done, uint64_t total, double fraction, double speed) {
            event.done = done;
            event.total = total;
            event.fraction = fraction;
            event.speed = speed;
            Post(event);
        });
    if (job->stop) {
        stopped();
        return;
    }
    if (!fetched) {
        fail(control.error == DownloadError::None ? DownloadError::Network : control.error,
             control.detail);
        return;
    }

    event.status = DownloadStatus::Assembling;
    event.speed = 0.0;
    Post(event);
    if (!transfer->Assemble(event.outPath)) {
        fail(DownloadError::Disk, std::string());
        return;
    }
    paths::RemoveTree(partsDir);

    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (GetFileAttributesExW(event.outPath.c_str(), GetFileExInfoStandard, &data)) {
        event.total = (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
        event.done = event.total;
    }
    event.status = DownloadStatus::Completed;
    event.fraction = 1.0;
    Post(event);
    Settle(job);
}
