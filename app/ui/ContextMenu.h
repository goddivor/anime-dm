#pragma once

#include <string>
#include <vector>

#include <windows.h>

// What the downloads menu offers under Resume: the players the source lists
// for the episode, and the one in use.
struct DownloadMenuOptions {
    std::vector<std::string> players;
    std::string currentPlayer;  // empty when the source decides
    bool canOpen = false;       // a finished file is selected
    bool canResume = false;     // a stopped, failed or finished item is selected
    bool canStop = false;       // a running item is selected
    bool inScheduler = false;   // the first selected item waits in the scheduler queue
};

// Shows the downloads right-click menu at screen coordinates. A player
// answers ID_PLAYER_FIRST plus its index, the automatic choice
// ID_CTX_PLAYER_AUTO. Returns the chosen command id, or 0 if dismissed.
int ShowDownloadsContextMenu(HWND owner, int x, int y, const DownloadMenuOptions& options);

// What the anime menu offers beyond the fixed entries.
struct AnimeMenuOptions {
    std::vector<std::string> templates;  // folder-icon recipe ids, in order
    std::string currentTemplate;         // the one ticked, if any
    bool offerAniyomi = false;           // when the folder lacks the Aniyomi files
    bool followed = false;               // its new episodes are fetched on their own
};

// Shows the right-click menu of an anime of the categories panel. A recipe
// answers ID_ICON_TEMPLATE_FIRST plus its index.
// Returns the chosen command id, or 0 if the menu was dismissed.
int ShowAnimeContextMenu(HWND owner, int x, int y, const AnimeMenuOptions& options);
