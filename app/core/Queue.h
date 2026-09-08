#pragma once

#include <vector>

#include "core/Download.h"

// The download list between two sessions, kept in `downloads.json`.
namespace queue {

// Everything the file holds.
struct State {
    std::vector<DownloadItem> items;
    std::vector<AnimeGroup> groups;
};

// Reads the file back. Whatever was running is handed back as stopped, its
// parts still on disk, so the user resumes it.
State Load();

// Writes the file, whole.
void Save(const State& state);

}  // namespace queue
