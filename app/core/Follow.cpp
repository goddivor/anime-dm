#include "core/Follow.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "core/Paths.h"
#include "core/Text.h"
#include "third_party/json.hpp"

namespace {

constexpr std::time_t kRetryStep = 3 * 60 * 60;
constexpr std::time_t kRetryWindow = 48 * 60 * 60;

}  // namespace

namespace follow {

// Walks day by day from `after` to the next release day and sets the hour.
std::time_t NextRelease(const FollowedAnime& anime, std::time_t after) {
    std::tm local = {};
    localtime_s(&local, &after);
    local.tm_hour = anime.releaseHour;
    local.tm_min = anime.releaseMinute;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    for (int step = 0; step < 8; ++step) {
        std::tm candidate = local;
        candidate.tm_mday += step;
        candidate.tm_isdst = -1;
        std::time_t moment = mktime(&candidate);
        int monday = (candidate.tm_wday + 6) % 7;
        if (moment > after && monday == anime.releaseDay) {
            return moment;
        }
    }
    return after + 7 * 24 * 60 * 60;
}

// Picks the next check from what the last one found.
void Plan(FollowedAnime* anime, std::time_t now, bool found) {
    if (found) {
        anime->misses = 0;
        anime->lastFound = now;
        anime->nextCheck = NextRelease(*anime, now);
        return;
    }
    anime->misses += 1;
    if (static_cast<std::time_t>(anime->misses) * kRetryStep < kRetryWindow) {
        anime->nextCheck = now + kRetryStep;
    } else {
        anime->misses = 0;
        anime->nextCheck = NextRelease(*anime, now);
    }
}

// Reads the file back; a missing or broken file means no follow.
std::vector<FollowedAnime> Load() {
    std::vector<FollowedAnime> follows;
    std::wstring path = paths::FollowsFile();
    if (path.empty()) {
        return follows;
    }
    std::ifstream file(std::filesystem::path(path), std::ios::binary);
    if (!file) {
        return follows;
    }
    nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
    if (!root.is_array()) {
        return follows;
    }
    for (const nlohmann::json& entry : root) {
        if (!entry.is_object()) {
            continue;
        }
        FollowedAnime anime;
        anime.addonId = entry.value("addonId", std::string());
        anime.animeUrl = entry.value("animeUrl", std::string());
        anime.title = entry.value("title", std::string());
        anime.destination = Widen(entry.value("destination", std::string()));
        anime.queue = entry.value("queue", 1) == 0 ? QueueKind::Main : QueueKind::Scheduler;
        anime.startAtOnce = entry.value("startAtOnce", false);
        anime.releaseDay = entry.value("releaseDay", 0);
        anime.releaseHour = entry.value("releaseHour", 20);
        anime.releaseMinute = entry.value("releaseMinute", 0);
        auto known = entry.find("known");
        if (known != entry.end() && known->is_array()) {
            for (const nlohmann::json& url : *known) {
                if (url.is_string()) {
                    anime.known.push_back(url.get<std::string>());
                }
            }
        }
        anime.lastNumber = entry.value("lastNumber", -1.0);
        anime.nextCheck = entry.value("nextCheck", std::time_t(0));
        anime.lastFound = entry.value("lastFound", std::time_t(0));
        anime.misses = entry.value("misses", 0);
        anime.primed = entry.value("primed", false);
        if (!anime.animeUrl.empty() && !anime.addonId.empty()) {
            follows.push_back(std::move(anime));
        }
    }
    return follows;
}

// Writes the file, whole.
void Save(const std::vector<FollowedAnime>& follows) {
    std::wstring path = paths::FollowsFile();
    if (path.empty()) {
        return;
    }
    nlohmann::json root = nlohmann::json::array();
    for (const FollowedAnime& anime : follows) {
        root.push_back({
            {"addonId", anime.addonId},
            {"animeUrl", anime.animeUrl},
            {"title", anime.title},
            {"destination", Narrow(anime.destination)},
            {"queue", anime.queue == QueueKind::Main ? 0 : 1},
            {"startAtOnce", anime.startAtOnce},
            {"releaseDay", anime.releaseDay},
            {"releaseHour", anime.releaseHour},
            {"releaseMinute", anime.releaseMinute},
            {"known", anime.known},
            {"lastNumber", anime.lastNumber},
            {"nextCheck", anime.nextCheck},
            {"lastFound", anime.lastFound},
            {"misses", anime.misses},
            {"primed", anime.primed},
        });
    }
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

}  // namespace follow
