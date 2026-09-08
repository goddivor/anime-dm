#include "core/Queue.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "core/Paths.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace queue {

// Reads the list back.
std::vector<DownloadItem> Load() {
    std::vector<DownloadItem> items;
    std::wstring path = paths::DownloadsFile();
    if (path.empty()) {
        return items;
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return items;
    }
    nlohmann::json list = nlohmann::json::parse(file, nullptr, false);
    if (!list.is_array()) {
        return items;
    }

    for (const nlohmann::json& entry : list) {
        if (!entry.is_object()) {
            continue;
        }
        DownloadItem item;
        item.id = entry.value("id", uint64_t(0));
        item.addonId = entry.value("addonId", std::string());
        item.animeTitle = entry.value("animeTitle", std::string());
        item.animeUrl = entry.value("animeUrl", std::string());
        item.episodeNumber = entry.value("episodeNumber", 0.0);
        item.pageUrl = entry.value("pageUrl", std::string());
        item.player = entry.value("player", std::string());
        item.outPath = SafePath(Widen(entry.value("outPath", std::string())));
        item.status = static_cast<DownloadStatus>(entry.value("status", 0));
        item.done = entry.value("done", uint64_t(0));
        item.total = entry.value("total", uint64_t(0));
        item.fraction = entry.value("fraction", -1.0);
        item.address = entry.value("address", std::string());
        item.error = static_cast<DownloadError>(entry.value("error", 0));
        item.detail = entry.value("detail", std::string());
        item.addedAt = static_cast<std::time_t>(entry.value("addedAt", int64_t(0)));
        item.lastTry = static_cast<std::time_t>(entry.value("lastTry", int64_t(0)));
        if (item.id == 0 || item.outPath.empty()) {
            continue;
        }
        if (IsActive(item.status)) {
            item.status = DownloadStatus::Stopped;
        }
        items.push_back(std::move(item));
    }
    return items;
}

// Writes the list, whole.
void Save(const std::vector<DownloadItem>& items) {
    std::wstring path = paths::DownloadsFile();
    if (path.empty()) {
        return;
    }
    nlohmann::json list = nlohmann::json::array();
    for (const DownloadItem& item : items) {
        list.push_back({
            {"id", item.id},
            {"addonId", item.addonId},
            {"animeTitle", item.animeTitle},
            {"animeUrl", item.animeUrl},
            {"episodeNumber", item.episodeNumber},
            {"pageUrl", item.pageUrl},
            {"player", item.player},
            {"outPath", Narrow(item.outPath)},
            {"status", static_cast<int>(item.status)},
            {"done", item.done},
            {"total", item.total},
            {"fraction", item.fraction},
            {"address", item.address},
            {"error", static_cast<int>(item.error)},
            {"detail", item.detail},
            {"addedAt", static_cast<int64_t>(item.addedAt)},
            {"lastTry", static_cast<int64_t>(item.lastTry)},
        });
    }

    std::wstring temp = path + L".tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file << list.dump(2);
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace queue
