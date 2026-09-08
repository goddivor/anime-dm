#pragma once

#include <vector>

#include "core/Download.h"

// The download list between two sessions, kept in `downloads.json`.
namespace queue {

// Reads the list back. Whatever was running is handed back as stopped, its
// parts still on disk, so the user resumes it.
std::vector<DownloadItem> Load();

// Writes the list, whole.
void Save(const std::vector<DownloadItem>& items);

}  // namespace queue
