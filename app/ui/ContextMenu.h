#pragma once

#include <windows.h>

// Shows the downloads right-click menu at screen coordinates.
// Returns the chosen command id, or 0 if the menu was dismissed.
int ShowDownloadsContextMenu(HWND owner, int x, int y);
