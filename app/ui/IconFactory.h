#pragma once

#include <windows.h>
#include <commctrl.h>

// Toolbar glyph indices; the order matches the image list built below.
enum ToolbarIcon {
    ICON_ADD,
    ICON_RESUME,
    ICON_STOP,
    ICON_REMOVE,
    ICON_SETTINGS,
    ICON_COUNT,
};

// Builds a 16x16 image list with the toolbar glyphs, drawn with GDI (no assets).
// The caller owns the returned list and must ImageList_Destroy it.
HIMAGELIST CreateToolbarImageList();
