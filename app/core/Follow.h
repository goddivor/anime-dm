#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "core/Download.h"

// An anime whose new episodes the application fetches on its own: the
// source, the page, the episodes already seen, and when the next one is
// expected.
struct FollowedAnime {
    std::string addonId;
    std::string animeUrl;
    std::string title;
    std::wstring destination;  // the folder the anime folder lives in
    QueueKind queue = QueueKind::Scheduler;
    bool startAtOnce = false;   // otherwise the queue starts the new episodes
    int releaseDay = 0;         // Monday is 0
    int releaseHour = 20;
    int releaseMinute = 0;
    std::vector<std::string> known;  // the pages of the episodes already seen
    double lastNumber = -1.0;        // the highest episode number seen, -1 before any check
    std::time_t nextCheck = 0;
    std::time_t lastFound = 0;
    int misses = 0;  // checks without a new episode since the release moment
    bool primed = false;  // the first check only records what exists
};

namespace follow {

// The moment of the next release after `after`, on the day and hour of the
// follow, in local time.
std::time_t NextRelease(const FollowedAnime& anime, std::time_t after);

// Sets the next check after a check at `now`: the next release when an
// episode came, three hours later while the release day drags on, and the
// next release once two days have passed without one.
void Plan(FollowedAnime* anime, std::time_t now, bool found);

// Reads and writes `follows.json` beside the queue.
std::vector<FollowedAnime> Load();
void Save(const std::vector<FollowedAnime>& follows);

}  // namespace follow
