#include "core/Queue.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/Paths.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace queue {

// Reads the file back.
State Load() {
    State state;
    std::wstring path = paths::DownloadsFile();
    if (path.empty()) {
        return state;
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return state;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    nlohmann::json list = nlohmann::json::array();
    nlohmann::json groups = nlohmann::json::array();
    if (root.is_array()) {
        list = root;
    } else if (root.is_object()) {
        list = root.value("items", nlohmann::json::array());
        groups = root.value("groups", nlohmann::json::array());
    }

    for (const nlohmann::json& entry : groups) {
        if (!entry.is_object()) {
            continue;
        }
        AnimeGroup group;
        group.url = entry.value("url", std::string());
        group.title = TidyText(entry.value("title", std::string()));
        group.posterUrl = entry.value("posterUrl", std::string());
        group.iconTemplate = entry.value("iconTemplate", std::string());
        group.expanded = entry.value("expanded", true);
        if (!group.url.empty()) {
            state.groups.push_back(std::move(group));
        }
    }

    for (const nlohmann::json& entry : list) {
        if (!entry.is_object()) {
            continue;
        }
        DownloadItem item;
        item.id = entry.value("id", uint64_t(0));
        item.addonId = entry.value("addonId", std::string());
        item.animeTitle = TidyText(entry.value("animeTitle", std::string()));
        item.animeUrl = entry.value("animeUrl", std::string());
        item.episodeNumber = entry.value("episodeNumber", 0.0);
        item.pageUrl = entry.value("pageUrl", std::string());
        item.player = entry.value("player", std::string());
        item.outPath = SafePath(Widen(entry.value("outPath", std::string())));
        item.movie = entry.value("movie", item.outPath.find(L" - Ep ") == std::wstring::npos);
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
        state.items.push_back(std::move(item));
    }

    // A file written before the groups existed still names every anime.
    for (const DownloadItem& item : state.items) {
        bool known = std::any_of(state.groups.begin(), state.groups.end(),
                                 [&](const AnimeGroup& g) { return g.url == item.animeUrl; });
        if (!known && !item.animeUrl.empty()) {
            AnimeGroup group;
            group.url = item.animeUrl;
            group.title = item.animeTitle;
            state.groups.push_back(std::move(group));
        }
    }
    return state;
}

// Writes the file, whole.
void Save(const State& state) {
    std::wstring path = paths::DownloadsFile();
    if (path.empty()) {
        return;
    }
    nlohmann::json groups = nlohmann::json::array();
    for (const AnimeGroup& group : state.groups) {
        groups.push_back({
            {"url", group.url},
            {"title", group.title},
            {"posterUrl", group.posterUrl},
            {"iconTemplate", group.iconTemplate},
            {"expanded", group.expanded},
        });
    }
    nlohmann::json list = nlohmann::json::array();
    for (const DownloadItem& item : state.items) {
        list.push_back({
            {"id", item.id},
            {"addonId", item.addonId},
            {"animeTitle", item.animeTitle},
            {"animeUrl", item.animeUrl},
            {"episodeNumber", item.episodeNumber},
            {"pageUrl", item.pageUrl},
            {"player", item.player},
            {"movie", item.movie},
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

    nlohmann::json root;
    root["items"] = std::move(list);
    root["groups"] = std::move(groups);

    std::wstring temp = path + L".tmp";
    {
        std::ofstream file(std::filesystem::path(temp), std::ios::binary | std::ios::trunc);
        if (!file) {
            return;
        }
        file << root.dump(2);
    }
    MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace queue
