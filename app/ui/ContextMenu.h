#pragma once

#include <windows.h>

// Shows the downloads right-click menu at screen coordinates.
// Returns the chosen command id, or 0 if the menu was dismissed.
int ShowDownloadsContextMenu(HWND owner, int x, int y);

// Shows the right-click menu of an anime of the categories panel.
// Returns the chosen command id, or 0 if the menu was dismissed.
int ShowAnimeContextMenu(HWND owner, int x, int y);
