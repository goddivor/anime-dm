#include "core/Transfer.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>

#include "core/Cipher.h"
#include "core/Http.h"
#include "core/Playlist.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace {

using Clock = std::chrono::steady_clock;

constexpr uint64_t kOpenEnded = ~static_cast<uint64_t>(0);
constexpr uint64_t kMinSplit = 1024 * 1024;  // never halve below this
constexpr int kAttempts = 5;
constexpr int kRetryDelayMs = 1500;
constexpr int kTickMs = 250;
constexpr int kReportEveryMs = 500;
constexpr int kSaveEveryMs = 3000;
constexpr size_t kCopyChunk = 1024 * 1024;
constexpr uint64_t kPlaylistProbeLimit = 2 * 1024 * 1024;

// Sleeps in slices so a stop request is honoured promptly.
void Backoff(int milliseconds, const std::atomic<bool>& stop) {
    int elapsed = 0;
    while (elapsed < milliseconds && !stop) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        elapsed += 100;
    }
}

// Milliseconds between two instants.
double MillisBetween(Clock::time_point from, Clock::time_point to) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

// One part on disk, written from a given offset onward.
class PartFile {
public:
    ~PartFile() { Close(); }

    // Opens the part and positions it at `offset`, discarding what lay after.
    bool Open(const std::wstring& path, uint64_t offset) {
        Close();
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) {
            handle_ = nullptr;
            return false;
        }
        LARGE_INTEGER position;
        position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(handle_, position, nullptr, FILE_BEGIN) || !SetEndOfFile(handle_)) {
            Close();
            return false;
        }
        return true;
    }

    bool Write(const uint8_t* data, size_t size) {
        DWORD written = 0;
        return handle_ != nullptr &&
               WriteFile(handle_, data, static_cast<DWORD>(size), &written, nullptr) &&
               written == size;
    }

    void Close() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

private:
    HANDLE handle_ = nullptr;
};

// The size of a file on disk, zero when absent.
uint64_t SizeOf(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    return (static_cast<uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

// Appends a whole file to an open destination.
bool AppendFile(HANDLE destination, const std::wstring& path, uint64_t limit) {
    HANDLE source = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (source == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::vector<uint8_t> buffer(kCopyChunk);
    uint64_t copied = 0;
    bool ok = true;
    while (copied < limit) {
        DWORD wanted = static_cast<DWORD>(std::min<uint64_t>(buffer.size(), limit - copied));
        DWORD read = 0;
        if (!ReadFile(source, buffer.data(), wanted, &read, nullptr) || read == 0) {
            ok = false;
            break;
        }
        DWORD written = 0;
        if (!WriteFile(destination, buffer.data(), read, &written, nullptr) || written != read) {
            ok = false;
            break;
        }
        copied += read;
    }
    CloseHandle(source);
    return ok;
}

// Creates the final file, with its folder, for the assembly.
HANDLE CreateDestination(const std::wstring& outPath) {
    std::error_code ignored;
    std::filesystem::create_directories(std::filesystem::path(outPath).parent_path(), ignored);
    HANDLE handle = CreateFileW(outPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    return handle == INVALID_HANDLE_VALUE ? nullptr : handle;
}

// Reads the saved positions of a parts folder, if any.
std::optional<nlohmann::json> LoadState(const std::wstring& dir) {
    std::ifstream file(std::filesystem::path(dir + L"\\state.json"), std::ios::binary);
    if (!file) {
        return std::nullopt;
    }
    nlohmann::json state = nlohmann::json::parse(file, nullptr, false);
    if (state.is_discarded() || !state.is_object()) {
        return std::nullopt;
    }
    return state;
}

// Writes the positions so a later run resumes where this one stops.
void SaveState(const std::wstring& dir, const nlohmann::json& state) {
    std::wstring temp = dir + L"\\state.tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file << state.dump();
    }
    MoveFileExW(temp.c_str(), (dir + L"\\state.json").c_str(), MOVEFILE_REPLACE_EXISTING);
}

// Paces the reports and the saves of a running transfer, and measures the
// speed from the bytes that came in between two ticks.
class Pacer {
public:
    explicit Pacer(const TransferProgress& progress) : progress_(progress) {
        Clock::time_point now = Clock::now();
        lastReport_ = now;
        lastSave_ = now;
        lastBytesAt_ = now;
    }

    // Reports and asks for a save when their delays elapsed.
    bool Tick(uint64_t done, uint64_t total, double fraction) {
        Clock::time_point now = Clock::now();
        double elapsed = MillisBetween(lastBytesAt_, now);
        if (elapsed >= kReportEveryMs) {
            double instant = static_cast<double>(done - lastBytes_) * 1000.0 / elapsed;
            speed_ = speed_ <= 0.0 ? instant : speed_ * 0.6 + instant * 0.4;
            lastBytes_ = done;
            lastBytesAt_ = now;
        }
        if (MillisBetween(lastReport_, now) >= kReportEveryMs) {
            progress_(done, total, fraction, speed_);
            lastReport_ = now;
        }
        if (MillisBetween(lastSave_, now) >= kSaveEveryMs) {
            lastSave_ = now;
            return true;
        }
        return false;
    }

    // The first report, as soon as the transfer starts.
    void Begin(uint64_t done, uint64_t total, double fraction) {
        lastBytes_ = done;
        progress_(done, total, fraction, 0.0);
    }

private:
    const TransferProgress& progress_;
    Clock::time_point lastReport_;
    Clock::time_point lastSave_;
    Clock::time_point lastBytesAt_;
    uint64_t lastBytes_ = 0;
    double speed_ = 0.0;
};

// ---------------------------------------------------------------------------
// Byte ranges: the file is cut into parts that several connections fetch at
// once, the largest remaining part being halved whenever a connection frees up.
// ---------------------------------------------------------------------------

struct Segment {
    uint64_t from = 0;
    uint64_t to = kOpenEnded;  // inclusive; open-ended when the size is unknown
    uint64_t done = 0;         // bytes already in the part file
    int part = 0;              // name of the part file
    bool active = false;
    bool finished = false;  // only meaningful for an open-ended segment
};

class RangeTransfer : public Transfer {
public:
    RangeTransfer(Http& http, const TransferSource& source, const std::wstring& dir,
                  TransferControl& control, const HttpProbe& probe)
        : http_(http), source_(source), dir_(dir), control_(control), probe_(probe) {}

    bool Prepare() override {
        total_ = probe_.length;
        ranges_ = probe_.ranges && total_ > 0;

        if (!Resume()) {
            Segment first;
            first.from = 0;
            first.to = total_ > 0 ? total_ - 1 : kOpenEnded;
            first.part = nextPart_++;
            segments_.assign(1, first);
        }
        return true;
    }

    bool Run(int connections, const TransferProgress& progress) override {
        int count = ranges_ ? std::max(1, connections) : 1;
        Pacer pacer(progress);
        pacer.Begin(Done(), total_, Fraction());

        std::vector<std::thread> workers;
        workers_ = count;
        for (int i = 0; i < count; ++i) {
            workers.emplace_back([this] { Worker(); });
        }

        while (workers_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
            if (pacer.Tick(Done(), total_, Fraction())) {
                Save();
            }
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
        Save();
        progress(Done(), total_, Fraction(), 0.0);
        return !control_.stop && !failed_ && Complete();
    }

    bool Assemble(const std::wstring& outPath) override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Segment> ordered = segments_;
        std::sort(ordered.begin(), ordered.end(),
                  [](const Segment& a, const Segment& b) { return a.from < b.from; });

        HANDLE destination = CreateDestination(outPath);
        if (destination == nullptr) {
            return false;
        }
        bool ok = true;
        uint64_t expected = 0;
        for (const Segment& segment : ordered) {
            if (segment.from != expected) {
                ok = false;
                break;
            }
            if (!AppendFile(destination, PartPath(segment.part), segment.done)) {
                ok = false;
                break;
            }
            expected += segment.done;
        }
        CloseHandle(destination);
        return ok && (total_ == 0 || expected == total_);
    }

    std::wstring Extension() const override { return std::wstring(); }
    uint64_t Total() const override { return total_; }

private:
    std::wstring PartPath(int part) const {
        return dir_ + L"\\" + std::to_wstring(part) + L".part";
    }

    // Reloads the segments of an earlier run when the file is still the same.
    bool Resume() {
        std::optional<nlohmann::json> state = LoadState(dir_);
        if (!state || state->value("kind", "") != "ranges" ||
            state->value("total", uint64_t(0)) != total_ || !ranges_) {
            return false;
        }
        std::vector<Segment> segments;
        for (const nlohmann::json& entry : (*state)["segments"]) {
            Segment segment;
            segment.from = entry.value("from", uint64_t(0));
            segment.to = entry.value("to", uint64_t(0));
            segment.done = entry.value("done", uint64_t(0));
            segment.part = entry.value("part", 0);
            uint64_t onDisk = SizeOf(PartPath(segment.part));
            segment.done = std::min(segment.done, onDisk);
            nextPart_ = std::max(nextPart_, segment.part + 1);
            segments.push_back(segment);
        }
        if (segments.empty()) {
            return false;
        }
        segments_ = std::move(segments);
        return true;
    }

    void Save() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!ranges_) {
            return;
        }
        nlohmann::json state;
        state["kind"] = "ranges";
        state["total"] = total_;
        nlohmann::json list = nlohmann::json::array();
        for (const Segment& segment : segments_) {
            list.push_back({{"from", segment.from},
                            {"to", segment.to},
                            {"done", segment.done},
                            {"part", segment.part}});
        }
        state["segments"] = std::move(list);
        SaveState(dir_, state);
    }

    uint64_t Done() {
        std::lock_guard<std::mutex> lock(mutex_);
        uint64_t sum = 0;
        for (const Segment& segment : segments_) {
            sum += segment.done;
        }
        return sum;
    }

    double Fraction() {
        if (total_ == 0) {
            return -1.0;
        }
        return static_cast<double>(Done()) / static_cast<double>(total_);
    }

    bool Complete() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const Segment& segment : segments_) {
            if (segment.to == kOpenEnded ? !segment.finished
                                         : segment.from + segment.done <= segment.to) {
                return false;
            }
        }
        return true;
    }

    // Picks an idle segment, or halves the largest active one, the way IDM
    // hands a freed connection to the slowest transfer.
    std::optional<size_t> Claim() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (size_t i = 0; i < segments_.size(); ++i) {
            const Segment& segment = segments_[i];
            bool pending = segment.to == kOpenEnded ? !segment.finished
                                                    : segment.from + segment.done <= segment.to;
            if (!segment.active && pending) {
                segments_[i].active = true;
                return i;
            }
        }
        if (!ranges_) {
            return std::nullopt;
        }

        size_t largest = segments_.size();
        uint64_t remaining = 0;
        for (size_t i = 0; i < segments_.size(); ++i) {
            const Segment& segment = segments_[i];
            if (!segment.active || segment.to == kOpenEnded) {
                continue;
            }
            uint64_t left = segment.to + 1 - (segment.from + segment.done);
            if (left > remaining) {
                remaining = left;
                largest = i;
            }
        }
        if (largest == segments_.size() || remaining < 2 * kMinSplit) {
            return std::nullopt;
        }

        Segment& victim = segments_[largest];
        uint64_t middle = victim.from + victim.done + remaining / 2;
        Segment fresh;
        fresh.from = middle;
        fresh.to = victim.to;
        fresh.part = nextPart_++;
        fresh.active = true;
        victim.to = middle - 1;
        segments_.push_back(fresh);
        return segments_.size() - 1;
    }

    // How much of an incoming chunk still belongs to a segment. A split only
    // ever moves the end at least a megabyte beyond the current position, so
    // the answer stays valid while the chunk is written outside the lock.
    uint64_t Room(size_t index, size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Segment& segment = segments_[index];
        if (segment.to == kOpenEnded) {
            return size;
        }
        return std::min<uint64_t>(size, segment.to + 1 - (segment.from + segment.done));
    }

    // Records written bytes; false once the segment has all it needs.
    bool Advance(size_t index, uint64_t written) {
        std::lock_guard<std::mutex> lock(mutex_);
        Segment& segment = segments_[index];
        segment.done += written;
        return segment.to == kOpenEnded || segment.from + segment.done <= segment.to;
    }

    // Fetches one segment, retrying on a broken connection.
    void Fetch(size_t index) {
        int attempts = 0;
        while (!control_.stop && !failed_) {
            uint64_t from = 0;
            uint64_t to = 0;
            uint64_t done = 0;
            int part = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                const Segment& segment = segments_[index];
                from = segment.from;
                to = segment.to;
                done = segment.done;
                part = segment.part;
            }
            if (to != kOpenEnded && from + done > to) {
                return;
            }
            if (!ranges_) {
                done = 0;
            }

            PartFile file;
            if (!file.Open(PartPath(part), done)) {
                Fail(DownloadError::Disk, std::string());
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                segments_[index].done = done;
            }

            uint64_t start = from + done;
            bool diskError = false;
            int status = http_.Stream(
                source_.url, source_.headers, start, to == kOpenEnded ? 0 : to,
                [start](int code) { return start == 0 || code == 206; },
                [&](const uint8_t* data, size_t size) {
                    if (control_.stop) {
                        return false;
                    }
                    uint64_t room = Room(index, size);
                    if (room > 0 && !file.Write(data, static_cast<size_t>(room))) {
                        diskError = true;
                        return false;
                    }
                    return Advance(index, room);
                });
            file.Close();

            if (diskError) {
                Fail(DownloadError::Disk, std::string());
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Segment& segment = segments_[index];
                if (segment.to == kOpenEnded) {
                    segment.finished = status >= 200 && status < 300 && !control_.stop;
                    if (segment.finished) {
                        return;
                    }
                } else if (segment.from + segment.done > segment.to) {
                    return;
                }
            }
            if (control_.stop) {
                return;
            }
            if (++attempts >= kAttempts) {
                Fail(DownloadError::Network, status > 0 ? "HTTP " + std::to_string(status)
                                                        : std::string());
                return;
            }
            Backoff(kRetryDelayMs, control_.stop);
        }
    }

    void Worker() {
        while (!control_.stop && !failed_) {
            std::optional<size_t> index = Claim();
            if (!index) {
                break;
            }
            Fetch(*index);
            std::lock_guard<std::mutex> lock(mutex_);
            segments_[*index].active = false;
        }
        --workers_;
    }

    void Fail(DownloadError error, const std::string& detail) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failed_) {
            failed_ = true;
            control_.error = error;
            control_.detail = detail;
        }
    }

    Http& http_;
    TransferSource source_;
    std::wstring dir_;
    TransferControl& control_;
    HttpProbe probe_;
    uint64_t total_ = 0;
    bool ranges_ = false;
    std::mutex mutex_;
    std::vector<Segment> segments_;
    int nextPart_ = 0;
    std::atomic<int> workers_{0};
    std::atomic<bool> failed_{false};
};

// ---------------------------------------------------------------------------
// Playlists: every segment is a part; the connections pull them in turn.
// ---------------------------------------------------------------------------

class PlaylistTransfer : public Transfer {
public:
    PlaylistTransfer(Http& http, const TransferSource& source, const std::wstring& dir,
                     TransferControl& control, std::string text)
        : http_(http), source_(source), dir_(dir), control_(control), text_(std::move(text)) {}

    bool Prepare() override {
        std::string url = source_.url;
        std::string text = text_;
        if (playlist::IsMaster(text)) {
            std::vector<playlist::Variant> variants = playlist::ParseMaster(text, url);
            if (variants.empty()) {
                return false;
            }
            url = variants.front().url;
            std::optional<std::string> body = http_.GetText(url, source_.headers);
            if (!body || !playlist::IsPlaylist(*body)) {
                return false;
            }
            text = *body;
        }
        std::optional<playlist::Media> media = playlist::ParseMedia(text, url);
        if (!media) {
            return false;
        }
        media_ = std::move(*media);
        completed_.assign(media_.segments.size(), false);
        Resume();
        return true;
    }

    bool Run(int connections, const TransferProgress& progress) override {
        Pacer pacer(progress);
        pacer.Begin(bytes_, 0, Fraction());

        if (!media_.initUrl.empty() && !initDone_ && !FetchInit()) {
            return false;
        }

        std::vector<std::thread> workers;
        workers_ = std::max(1, connections);
        for (int i = 0; i < std::max(1, connections); ++i) {
            workers.emplace_back([this] { Worker(); });
        }
        while (workers_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kTickMs));
            if (pacer.Tick(bytes_, 0, Fraction())) {
                Save();
            }
        }
        for (std::thread& worker : workers) {
            worker.join();
        }
        Save();
        progress(bytes_, 0, Fraction(), 0.0);
        return !control_.stop && !failed_ && Complete();
    }

    bool Assemble(const std::wstring& outPath) override {
        HANDLE destination = CreateDestination(outPath);
        if (destination == nullptr) {
            return false;
        }
        bool ok = true;
        if (!media_.initUrl.empty()) {
            std::wstring init = InitPath();
            ok = AppendFile(destination, init, SizeOf(init));
        }
        for (size_t i = 0; ok && i < media_.segments.size(); ++i) {
            std::wstring part = PartPath(i);
            ok = AppendFile(destination, part, SizeOf(part));
        }
        CloseHandle(destination);
        return ok;
    }

    std::wstring Extension() const override {
        if (!media_.initUrl.empty()) {
            return L".mp4";
        }
        const std::string& first = media_.segments.front().url;
        size_t query = first.find('?');
        std::string path = first.substr(0, query);
        if (path.size() >= 4 && (path.compare(path.size() - 4, 4, ".m4s") == 0 ||
                                 path.compare(path.size() - 4, 4, ".mp4") == 0)) {
            return L".mp4";
        }
        return L".ts";
    }

    uint64_t Total() const override { return 0; }

private:
    std::wstring PartPath(size_t index) const {
        return dir_ + L"\\" + std::to_wstring(index) + L".part";
    }

    std::wstring InitPath() const { return dir_ + L"\\init.part"; }

    void Resume() {
        std::optional<nlohmann::json> state = LoadState(dir_);
        if (!state || state->value("kind", "") != "playlist" ||
            state->value("count", size_t(0)) != media_.segments.size()) {
            return;
        }
        for (const nlohmann::json& entry : (*state)["done"]) {
            size_t index = entry.get<size_t>();
            if (index < completed_.size() && SizeOf(PartPath(index)) > 0) {
                completed_[index] = true;
                bytes_ += SizeOf(PartPath(index));
            }
        }
        initDone_ = media_.initUrl.empty() || SizeOf(InitPath()) > 0;
    }

    void Save() {
        nlohmann::json state;
        state["kind"] = "playlist";
        state["count"] = media_.segments.size();
        nlohmann::json done = nlohmann::json::array();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (size_t i = 0; i < completed_.size(); ++i) {
                if (completed_[i]) {
                    done.push_back(i);
                }
            }
        }
        state["done"] = std::move(done);
        SaveState(dir_, state);
    }

    double Fraction() {
        std::lock_guard<std::mutex> lock(mutex_);
        size_t done = static_cast<size_t>(std::count(completed_.begin(), completed_.end(), true));
        return static_cast<double>(done) / static_cast<double>(completed_.size());
    }

    bool Complete() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::all_of(completed_.begin(), completed_.end(), [](bool b) { return b; });
    }

    // Fetches a URL whole, retrying on a broken connection.
    std::optional<std::vector<uint8_t>> FetchWhole(const std::string& url) {
        for (int attempt = 0; attempt < kAttempts && !control_.stop; ++attempt) {
            std::optional<std::vector<uint8_t>> body = http_.GetBytes(url, source_.headers);
            if (body && !body->empty()) {
                return body;
            }
            Backoff(kRetryDelayMs, control_.stop);
        }
        return std::nullopt;
    }

    bool FetchInit() {
        std::optional<std::vector<uint8_t>> body = FetchWhole(media_.initUrl);
        if (!body) {
            Fail(DownloadError::Network, std::string());
            return false;
        }
        PartFile file;
        if (!file.Open(InitPath(), 0) || !file.Write(body->data(), body->size())) {
            Fail(DownloadError::Disk, std::string());
            return false;
        }
        initDone_ = true;
        return true;
    }

    // The key of a segment, fetched once per URL.
    std::optional<std::vector<uint8_t>> Key(const std::string& url) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = keys_.find(url);
            if (found != keys_.end()) {
                return found->second;
            }
        }
        std::optional<std::vector<uint8_t>> key = FetchWhole(url);
        if (!key || key->size() != 16) {
            return std::nullopt;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        keys_[url] = *key;
        return key;
    }

    // Undoes the AES-128 protection of a segment.
    bool Decrypt(const playlist::Segment& segment, std::vector<uint8_t>& data) {
        std::optional<std::vector<uint8_t>> key = Key(segment.keyUrl);
        if (!key) {
            return false;
        }
        uint8_t iv[16] = {};
        if (segment.iv.empty()) {
            uint64_t sequence = segment.sequence;
            for (int i = 15; i >= 8; --i) {
                iv[i] = static_cast<uint8_t>(sequence & 0xff);
                sequence >>= 8;
            }
        } else {
            std::string hex = segment.iv;
            if (hex.size() < 32) {
                hex.insert(0, 32 - hex.size(), '0');
            }
            for (int i = 0; i < 16; ++i) {
                iv[i] = static_cast<uint8_t>(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
            }
        }
        return cipher::AesCbcDecrypt(*key, iv, data);
    }

    void Worker() {
        while (!control_.stop && !failed_) {
            size_t index = next_++;
            if (index >= media_.segments.size()) {
                break;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (completed_[index]) {
                    continue;
                }
            }
            const playlist::Segment& segment = media_.segments[index];
            std::optional<std::vector<uint8_t>> body = FetchWhole(segment.url);
            if (!body) {
                if (!control_.stop) {
                    Fail(DownloadError::Network, std::string());
                }
                break;
            }
            if (!segment.keyUrl.empty() && !Decrypt(segment, *body)) {
                Fail(DownloadError::Key, std::string());
                break;
            }
            PartFile file;
            if (!file.Open(PartPath(index), 0) || !file.Write(body->data(), body->size())) {
                Fail(DownloadError::Disk, std::string());
                break;
            }
            file.Close();
            bytes_ += body->size();
            std::lock_guard<std::mutex> lock(mutex_);
            completed_[index] = true;
        }
        --workers_;
    }

    void Fail(DownloadError error, const std::string& detail) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!failed_) {
            failed_ = true;
            control_.error = error;
            control_.detail = detail;
        }
    }

    Http& http_;
    TransferSource source_;
    std::wstring dir_;
    TransferControl& control_;
    std::string text_;
    playlist::Media media_;
    std::mutex mutex_;
    std::vector<bool> completed_;
    std::map<std::string, std::vector<uint8_t>> keys_;
    std::atomic<size_t> next_{0};
    std::atomic<uint64_t> bytes_{0};
    std::atomic<int> workers_{0};
    std::atomic<bool> failed_{false};
    bool initDone_ = false;
};

// Whether a URL names a playlist by its path alone.
bool LooksLikePlaylist(const std::string& url) {
    size_t query = url.find('?');
    std::string path = url.substr(0, query);
    return path.size() >= 5 && path.compare(path.size() - 5, 5, ".m3u8") == 0;
}

}  // namespace

// Builds the transfer that fits a video.
std::unique_ptr<Transfer> OpenTransfer(Http& http, const TransferSource& source,
                                       const std::wstring& partsDir, TransferControl& control) {
    std::optional<HttpProbe> probe = http.Probe(source.url, source.headers);
    if (!probe || probe->status < 200 || probe->status >= 300) {
        return nullptr;
    }

    bool small = probe->length == 0 || probe->length < kPlaylistProbeLimit;
    if (LooksLikePlaylist(source.url) || (small && !probe->ranges)) {
        std::optional<std::string> text = http.GetText(source.url, source.headers);
        if (text && playlist::IsPlaylist(*text)) {
            auto transfer =
                std::make_unique<PlaylistTransfer>(http, source, partsDir, control, *text);
            return transfer->Prepare() ? std::move(transfer) : nullptr;
        }
        if (LooksLikePlaylist(source.url)) {
            return nullptr;
        }
    }

    auto transfer = std::make_unique<RangeTransfer>(http, source, partsDir, control, *probe);
    return transfer->Prepare() ? std::move(transfer) : nullptr;
}
