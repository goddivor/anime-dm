#pragma once

#include <string>
#include <vector>

#include <windows.h>

// Shows the downloads right-click menu at screen coordinates.
// Returns the chosen command id, or 0 if the menu was dismissed.
int ShowDownloadsContextMenu(HWND owner, int x, int y);

// What the anime menu offers beyond the fixed entries.
struct AnimeMenuOptions {
    std::vector<std::string> templates;  // folder-icon recipe ids, in order
    std::string currentTemplate;         // the one ticked, if any
    bool offerAniyomi = false;           // when the folder lacks the Aniyomi files
};

// Shows the right-click menu of an anime of the categories panel. A recipe
// answers ID_ICON_TEMPLATE_FIRST plus its index.
// Returns the chosen command id, or 0 if the menu was dismissed.
int ShowAnimeContextMenu(HWND owner, int x, int y, const AnimeMenuOptions& options);
